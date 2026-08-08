#include "SandboxApp.h"

#include "ECS/Components.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Geometry/MeshGen.h"
#include "Input/InputCodes.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cstring>
#include <filesystem>
#include <memory>

using namespace Dark;
using namespace Math;

void mountContentRoots(AssetManager& assets)
{
    namespace fs = std::filesystem;

    const fs::path exeDir = executableDirectory();
    const fs::path cwd    = fs::current_path();

    const fs::path candidates[] = {
        exeDir / "content",
        cwd / "content",
        exeDir / ".." / ".." / ".." / "content",
        cwd / ".." / ".." / ".." / "content",
    };

    bool any = false;
    for (const fs::path& c : candidates)
    {
        if (c.empty())
            continue;
        std::error_code ec;
        if (fs::exists(c, ec) && !ec && fs::is_directory(c, ec) && !ec)
        {
            assets.mountDirectory(c);
            any = true;
        }
    }

    if (!any)
    {
        DE_LOG_ERROR(
            "SandboxApp: no content directory found. Tried next to exe ('{}') and cwd ('{}').",
            exeDir.string(),
            cwd.string());
    }
}

void copyMatrix(float dst[16], const Math::Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

Math::Matrix4f makeWorldMatrix(const TransformComponent& xf)
{
    const Matrix4f S = Matrix4f::ScaleMatrixXYZ(xf.scale.x, xf.scale.y, xf.scale.z);
    const Matrix4f R = xf.rotation.ToMatrix4();
    const Matrix4f T = Matrix4f::TranslationMatrix(xf.position.x, xf.position.y, xf.position.z);
    return S * R * T;
}

void SandboxApp::registerDefaultActions()
{
    ActionMap& a = input().actions();
    a.clear();

    a.bindKey("quit", Key::Escape);
    a.bindButton("quit", GamepadButton::Back);

    a.bindKey("pause", Key::Space);
    a.bindButton("pause", GamepadButton::A);

    a.bindKey("reset", Key::R);
    a.bindButton("reset", GamepadButton::Y);

    a.bindKey("speed_up", Key::Equal);
    a.bindButton("speed_up", GamepadButton::RightShoulder);
    a.bindKey("speed_down", Key::Minus);
    a.bindButton("speed_down", GamepadButton::LeftShoulder);

    // Yaw: A/D, arrows, left stick X, D-pad L/R
    a.bindKeyAsAxis("yaw", Key::A, -1.0f);
    a.bindKeyAsAxis("yaw", Key::D, 1.0f);
    a.bindKeyAsAxis("yaw", Key::Left, -1.0f);
    a.bindKeyAsAxis("yaw", Key::Right, 1.0f);
    a.bindAxis("yaw", GamepadAxis::LeftX, 1.0f);
    a.bindButtonAsAxis("yaw", GamepadButton::DPadLeft, -1.0f);
    a.bindButtonAsAxis("yaw", GamepadButton::DPadRight, 1.0f);

    // Pitch: W/S, arrows, left stick Y, D-pad U/D
    a.bindKeyAsAxis("pitch", Key::W, 1.0f);
    a.bindKeyAsAxis("pitch", Key::S, -1.0f);
    a.bindKeyAsAxis("pitch", Key::Up, 1.0f);
    a.bindKeyAsAxis("pitch", Key::Down, -1.0f);
    a.bindAxis("pitch", GamepadAxis::LeftY, 1.0f);
    a.bindButtonAsAxis("pitch", GamepadButton::DPadUp, 1.0f);
    a.bindButtonAsAxis("pitch", GamepadButton::DPadDown, -1.0f);

    DE_LOG_INFO(
        "Input: quit(Esc/Back) pause(Space/A) reset(R/Y) speed(+/- / shoulders) "
        "yaw(A/D / stick X) pitch(W/S / stick Y)");
}

void SandboxApp::handleRuntimeCommands(float dt)
{
    if (input().actionPressed("quit"))
    {
        DE_LOG_INFO("Command: quit");
        requestQuit();
        return;
    }

    if (input().actionPressed("pause"))
    {
        m_spinPaused = !m_spinPaused;
        DE_LOG_INFO("Command: pause spin = {}", m_spinPaused);
    }

    if (input().actionPressed("reset"))
    {
        m_spinSpeed  = 0.8f;
        m_spinPaused = false;
        if (auto* xf = world().get<TransformComponent>(m_cube))
            xf->rotation = Quaternion::IDENTITY;
        DE_LOG_INFO("Command: reset cube");
    }

    if (input().actionPressed("speed_up"))
    {
        m_spinSpeed += 0.2f;
        if (m_spinSpeed > 5.0f)
            m_spinSpeed = 5.0f;
        DE_LOG_INFO("Command: spin speed = {:.2f}", m_spinSpeed);
    }

    if (input().actionPressed("speed_down"))
    {
        m_spinSpeed -= 0.2f;
        if (m_spinSpeed < 0.0f)
            m_spinSpeed = 0.0f;
        DE_LOG_INFO("Command: spin speed = {:.2f}", m_spinSpeed);
    }

    // Continuous axes
    const float yawCmd   = input().actionAxis("yaw");
    const float pitchCmd = input().actionAxis("pitch");
    constexpr float kTurnRate = 1.8f; // rad/s

    if (auto* xf = world().get<TransformComponent>(m_cube))
    {
        if (yawCmd != 0.0f || pitchCmd != 0.0f)
        {
            const Quaternion yawQ   = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, yawCmd * kTurnRate * dt);
            const Quaternion pitchQ = Quaternion::FromAxisAngle(Vector3f::X_AXIS, pitchCmd * kTurnRate * dt);
            xf->rotation = yawQ * pitchQ * xf->rotation;
            xf->rotation.Normalize();
        }

        if (!m_spinPaused)
        {
            const Quaternion spin = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, m_spinSpeed * dt);
            xf->rotation = spin * xf->rotation;
            xf->rotation.Normalize();
        }
    }
}

void SandboxApp::onInit()
{
    DE_LOG_INFO("SandboxApp: init");

    mountContentRoots(assets());
    registerDefaultActions();

    if (!m_meshPipeline.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: MeshPipeline create failed");
        return;
    }

    const MeshGen::MeshData cubeData = MeshGen::CreateCube(1.0f);
    m_cubeMesh                       = Mesh::Create(renderer(), cubeData);

    m_cubeMaterial = std::make_shared<Material>();
    if (!m_cubeMaterial->createFromAlbedoPath(
            renderer(),
            assets(),
            "textures/dark_engine_cube.png",
            /*fallback*/ 64, 166, 242, 255))
    {
        DE_LOG_FATAL("SandboxApp: material create failed");
        return;
    }

    const AssetID matId = assets().registerAsset(m_cubeMaterial);
    if (matId == NULL_ASSET)
    {
        DE_LOG_FATAL("SandboxApp: material register failed");
        return;
    }

    const float aspect = (renderer().height() > 0)
        ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height())
        : 1.0f;
    m_viewCamera.SetLens(/*fovY*/ 1.04719755f /*60deg*/, aspect, 0.1f, 1000.0f);
    m_viewCamera.LookAt(Vector3f(2.5f, 2.0f, -4.0f), Vector3f(0.0f, 0.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    m_camera = world().createEntity();
    world().emplace<TagComponent>(m_camera, "Main Camera");
    world().emplace<TransformComponent>(m_camera, Vector3f{ 2.5f, 2.0f, -4.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    world().emplace<CameraComponent>(m_camera, /* fovDeg */ 60.0f, /* near */ 0.1f, /* far */ 1000.0f, /* primary */ true);

    m_cube = world().createEntity();
    world().emplace<TagComponent>(m_cube, "Cube");
    world().emplace<TransformComponent>(m_cube);
    auto& meshComp       = world().emplace<MeshComponent>(m_cube);
    meshComp.meshAssetID = NULL_ASSET;
    meshComp.matAssetID  = matId;
    meshComp.castShadow  = true;

    DE_LOG_INFO(
        "SandboxApp: cube mesh {} verts / {} indices, aspect {:.3f}, material id={}, albedo {}x{}",
        m_cubeMesh.vertexCount(),
        m_cubeMesh.indexCount(),
        aspect,
        matId,
        m_cubeMaterial->albedo().width(),
        m_cubeMaterial->albedo().height());
}

void SandboxApp::onUpdate(float dt)
{
    handleRuntimeCommands(dt);
}

void SandboxApp::onRender()
{
    renderer().beginFrame();

    auto* cmd = renderer().commandList();
    m_meshPipeline.bind(cmd);

    AssetRef<Material> material;
    if (auto* meshComp = world().get<MeshComponent>(m_cube))
        material = assets().getAs<Material>(meshComp->matAssetID);

    if (!material)
        material = m_cubeMaterial;

    if (material && material->isValid())
        material->bind(cmd, MeshPipeline::kRootAlbedoSrv);

    MeshFrameConstants cb{};
    if (auto* xf = world().get<TransformComponent>(m_cube))
    {
        const Matrix4f worldMat = makeWorldMatrix(*xf);
        const Matrix4f viewProj = m_viewCamera.GetViewProj();
        const Matrix4f wvp      = worldMat * viewProj;

        copyMatrix(cb.worldViewProj, wvp);
        copyMatrix(cb.world, worldMat);
    }

    if (material)
        material->applySurface(cb);
    else
    {
        cb.color[0] = 1.0f;
        cb.color[1] = 1.0f;
        cb.color[2] = 1.0f;
        cb.color[3] = 1.0f;
    }

    cb.lightDirWS[0] = 0.4f;
    cb.lightDirWS[1] = 0.8f;
    cb.lightDirWS[2] = -0.3f;
    cb.pad0          = 0.0f;

    m_meshPipeline.setConstants(cmd, cb);
    m_cubeMesh.draw(cmd);

    renderer().stats().drawCalls = 1;
    renderer().stats().triangles = m_cubeMesh.indexCount() / 3;

    renderer().endFrame();
}

void SandboxApp::onShutdown()
{
    renderer().waitForGpu();
    if (m_cubeMaterial)
        assets().unload(m_cubeMaterial->id);
    m_cubeMaterial.reset();
    DE_LOG_INFO("SandboxApp: shutdown");
}
