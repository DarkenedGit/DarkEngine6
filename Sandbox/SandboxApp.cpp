#include "SandboxApp.h"

#include "ECS/Components.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Geometry/MeshGen.h"
#include "Input/InputCodes.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Render/Frustum3f.h"
#include "Terrain/SplatMap.h"
#include "Water/WaterWaves.h"

#include <cstring>
#include <filesystem>
#include <memory>

using namespace Dark;
using namespace Math;
using namespace Geometry;
using namespace Terrain;

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
    a.bindButton("speed_down", GamepadButton::X);

    // Cube yaw/pitch: A/D W/S, arrows, D-pad (left stick is camera move).
    a.bindKeyAsAxis("yaw", Key::A, -1.0f);
    a.bindKeyAsAxis("yaw", Key::D, 1.0f);
    a.bindKeyAsAxis("yaw", Key::Left, -1.0f);
    a.bindKeyAsAxis("yaw", Key::Right, 1.0f);
    a.bindButtonAsAxis("yaw", GamepadButton::DPadLeft, -1.0f);
    a.bindButtonAsAxis("yaw", GamepadButton::DPadRight, 1.0f);

    a.bindKeyAsAxis("pitch", Key::W, 1.0f);
    a.bindKeyAsAxis("pitch", Key::S, -1.0f);
    a.bindKeyAsAxis("pitch", Key::Up, 1.0f);
    a.bindKeyAsAxis("pitch", Key::Down, -1.0f);
    a.bindButtonAsAxis("pitch", GamepadButton::DPadUp, 1.0f);
    a.bindButtonAsAxis("pitch", GamepadButton::DPadDown, -1.0f);

    a.bindKeyAsAxis("fly_forward", Key::I, 1.0f);
    a.bindKeyAsAxis("fly_forward", Key::K, -1.0f);
    a.bindAxis("fly_forward", GamepadAxis::LeftY, 1.0f);

    a.bindKeyAsAxis("fly_strafe", Key::J, -1.0f);
    a.bindKeyAsAxis("fly_strafe", Key::L, 1.0f);
    a.bindAxis("fly_strafe", GamepadAxis::LeftX, 1.0f);

    a.bindKeyAsAxis("fly_climb", Key::U, 1.0f);
    a.bindKeyAsAxis("fly_climb", Key::O, -1.0f);
    a.bindAxis("fly_climb", GamepadAxis::RightTrigger, 1.0f);
    a.bindAxis("fly_climb", GamepadAxis::LeftTrigger, -1.0f);

    a.bindAxis("look_yaw", GamepadAxis::RightX, 1.0f);
    a.bindAxis("look_pitch", GamepadAxis::RightY, 1.0f);

    a.bindButton("sprint", GamepadButton::LeftThumb);
    a.bindButton("sprint", GamepadButton::LeftShoulder);

    a.bindKey("time_back", Key::LeftBracket);
    a.bindKey("time_fwd", Key::RightBracket);
    a.bindKey("time_toggle", Key::Digit0);
    a.bindKey("weather_clear", Key::Digit1);
    a.bindKey("weather_partly", Key::Digit2);
    a.bindKey("weather_overcast", Key::Digit3);
    a.bindKey("weather_storm", Key::Digit4);
    a.bindKey("debug_shadows", Key::F8);
    a.bindKey("debug_depth", Key::F9);

    DE_LOG_INFO(
        "Input: quit(Esc/Back) pause(Space/A) reset(R/Y) speed(+/- / RB) "
        "cube yaw/pitch(A/D W/S / D-pad) fly(IJKL U/O, LS move, RS look, triggers climb, "
        "LB/L3 sprint, RMB look) time([/]) weather(1-4) F8 shadow maps F9 depth");
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

    if (input().actionPressed("time_back"))
    {
        m_env.timeOfDay -= 0.75f;
        if (m_env.timeOfDay < 0.0f)
            m_env.timeOfDay += 24.0f;
        m_env.evaluate();
        DE_LOG_INFO("Sky: time {:.2f}h  elev {:.1f} deg", m_env.timeOfDay, m_env.sunElevation() * 57.2958f);
    }
    if (input().actionPressed("time_fwd"))
    {
        m_env.timeOfDay += 0.75f;
        if (m_env.timeOfDay >= 24.0f)
            m_env.timeOfDay -= 24.0f;
        m_env.evaluate();
        DE_LOG_INFO("Sky: time {:.2f}h  elev {:.1f} deg", m_env.timeOfDay, m_env.sunElevation() * 57.2958f);
    }
    if (input().actionPressed("time_toggle"))
    {
        m_env.timeScale = (m_env.timeScale == 0.0f) ? 0.35f : 0.0f;
        DE_LOG_INFO("Sky: time scale = {:.2f} h/s", m_env.timeScale);
    }
    if (input().actionPressed("weather_clear"))
    {
        m_env.weather = Sky::WeatherState::Clear();
        m_env.evaluate();
        DE_LOG_INFO("Sky: weather clear");
    }
    if (input().actionPressed("weather_partly"))
    {
        m_env.weather = Sky::WeatherState::PartlyCloudy();
        m_env.evaluate();
        DE_LOG_INFO("Sky: weather partly cloudy");
    }
    if (input().actionPressed("weather_overcast"))
    {
        m_env.weather = Sky::WeatherState::Overcast();
        m_env.evaluate();
        DE_LOG_INFO("Sky: weather overcast");
    }
    if (input().actionPressed("weather_storm"))
    {
        m_env.weather = Sky::WeatherState::Storm();
        m_env.evaluate();
        DE_LOG_INFO("Sky: weather storm");
    }
    if (input().actionPressed("debug_shadows"))
    {
        m_showShadowMaps = !m_showShadowMaps;
        DE_LOG_INFO("Sandbox: shadow map overlay = {}", m_showShadowMaps);
    }
    if (input().actionPressed("debug_depth"))
    {
        m_showDepth = !m_showDepth;
        DE_LOG_INFO("Sandbox: depth overlay = {}", m_showDepth);
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

    updateFlyCamera(dt);

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

        xf->position.y = m_terrain.heightAtWorld(xf->position.x, xf->position.z) + 0.5f;
    }
}

void SandboxApp::updateFlyCamera(float dt)
{
    const float forward = input().actionAxis("fly_forward");
    const float strafe  = input().actionAxis("fly_strafe");
    const float climb   = input().actionAxis("fly_climb");
    const bool  sprint  = input().keyDown(Key::LeftShift) || input().actionDown("sprint");
    const float speed   = sprint ? 48.0f : 18.0f;

    if (forward != 0.0f)
        m_viewCamera.Walk(forward * speed * dt);
    if (strafe != 0.0f)
        m_viewCamera.Strafe(strafe * speed * dt);
    if (climb != 0.0f)
        m_viewCamera.Climb(climb * speed * dt);

    if (input().mouseDown(MouseButton::Right))
    {
        const float sens = 0.0045f;
        m_viewCamera.RotateY(static_cast<float>(input().mouseDeltaX()) * sens);
        m_viewCamera.Pitch(static_cast<float>(input().mouseDeltaY()) * -sens);
    }

    constexpr float kPadLook = 2.1f;
    const float lookYaw   = input().actionAxis("look_yaw");
    const float lookPitch = input().actionAxis("look_pitch");
    if (lookYaw != 0.0f)
        m_viewCamera.RotateY(lookYaw * kPadLook * dt);
    if (lookPitch != 0.0f)
        m_viewCamera.Pitch(lookPitch * kPadLook * dt);

    if (auto* xf = world().get<TransformComponent>(m_camera))
        xf->position = m_viewCamera.GetPosition();
}

void SandboxApp::syncTerrainLod()
{
    m_terrain.updateLod(m_viewCamera.GetPosition());
    const bool terrainDirty = m_terrain.needsRebuild();
    const bool waterDirty   = m_water.needsRebuild();
    if (!terrainDirty && !waterDirty)
        return;

    renderer().waitForGpu();
    if (terrainDirty)
    {
        m_terrain.rebuildDirtyCpuMeshes();
        if (!m_terrain.uploadDirty(renderer()))
            DE_LOG_ERROR("SandboxApp: terrain upload failed");
    }
    if (waterDirty)
    {
        m_water.rebuildDirtyCpuMeshes();
        if (!m_water.uploadDirty(renderer()))
            DE_LOG_ERROR("SandboxApp: water upload failed");
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
    if (!m_terrainPipeline.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: TerrainPipeline create failed");
        return;
    }
    if (!m_waterPipeline.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: WaterPipeline create failed");
        return;
    }
    if (!m_skyPipeline.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: SkyPipeline create failed");
        return;
    }
    if (!m_shadows.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: ShadowSystem create failed");
        return;
    }
    if (!m_debugOverlay.create(renderer().device()))
    {
        DE_LOG_WARN("SandboxApp: DebugOverlay create failed — F8/F9 disabled");
    }

    m_env.timeOfDay = 16.2f;
    m_env.weather   = Sky::WeatherState::PartlyCloudy();
    m_env.evaluate();

    {
        Terrain::TerrainDesc terrainDesc;
        terrainDesc.chunkCells       = 16;
        terrainDesc.lodDistanceCount = 5;
        terrainDesc.lodDistances[0]  = 40.0f;
        terrainDesc.lodDistances[1]  = 80.0f;
        terrainDesc.lodDistances[2]  = 160.0f;
        terrainDesc.lodDistances[3]  = 320.0f;
        terrainDesc.lodDistances[4]  = 640.0f;

        HeightMap base;
        HeightMap detail;
        if (!base.createFbm(129, 129, 1337u, 6, 3.5f, 1.0f, 2.1f, 0.48f, 2.0f, 22.0f)
            || !detail.createFbm(129, 129, 9001u, 3, 18.0f, 0.12f, 2.0f, 0.5f, 2.0f, 22.0f)
            || !base.addLayer(detail, 1.0f))
        {
            DE_LOG_FATAL("SandboxApp: height map create failed");
            return;
        }
        const float extent = 128.0f * 2.0f;
        base.setOrigin(Vector3f{ -0.5f * extent, 0.0f, -0.5f * extent });
        terrainDesc.heightMap = std::move(base);

        Terrain::SplatMap splat;
        if (!splat.generateFromHeight(terrainDesc.heightMap))
        {
            DE_LOG_FATAL("SandboxApp: splat generate failed");
            return;
        }
        if (!m_terrain.create(std::move(terrainDesc)))
        {
            DE_LOG_FATAL("SandboxApp: terrain create failed");
            return;
        }
        if (!m_terrainMaterial.createDefault(renderer(), splat))
        {
            DE_LOG_FATAL("SandboxApp: terrain material create failed");
            return;
        }
        m_terrain.updateLod(Vector3f{ 0.0f, 50.0f, -80.0f });
        if (!m_terrain.createGpu(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: terrain GPU upload failed");
            return;
        }

        const Aabb3f terrainBox = m_terrain.bounds();
        const float waterLevel = Lerp(terrainBox.Min.y, terrainBox.Max.y, 0.38f);
        Water::WaterDesc waterDesc;
        waterDesc.chunkCells       = 16;
        waterDesc.waterLevel       = waterLevel;
        waterDesc.lodDistanceCount = 5;
        waterDesc.lodDistances[0]  = 40.0f;
        waterDesc.lodDistances[1]  = 80.0f;
        waterDesc.lodDistances[2]  = 160.0f;
        waterDesc.lodDistances[3]  = 320.0f;
        waterDesc.lodDistances[4]  = 640.0f;
        waterDesc.params           = Water::defaultWaterParams(waterLevel);
        waterDesc.params.flowDir   = Vector2f(1.0f, 0.35f);
        if (!m_water.create(m_terrain.heightMap(), waterDesc))
        {
            DE_LOG_FATAL("SandboxApp: water create failed");
            return;
        }
        m_water.updateLod(Vector3f{ 0.0f, 50.0f, -80.0f });
        if (!m_water.createGpu(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: water GPU upload failed");
            return;
        }
        DE_LOG_INFO("SandboxApp: water level {:.2f}, {} wet chunks", waterLevel, m_water.wetChunkCount());
    }

    const MeshData cubeData = CreateCube(1.0f);
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

    m_terrainMaterial.setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_cubeMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());

    const float aspect = (renderer().height() > 0)
        ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height())
        : 1.0f;
    m_viewCamera.SetLens(/*fovY*/ 1.04719755f /*60deg*/, aspect, 0.5f, 2000.0f);
    m_viewCamera.LookAt(Vector3f(0.0f, 48.0f, -86.0f), Vector3f(0.0f, 8.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    m_camera = world().createEntity();
    world().emplace<TagComponent>(m_camera, "Main Camera");
    world().emplace<TransformComponent>(m_camera, m_viewCamera.GetPosition(), Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    world().emplace<CameraComponent>(m_camera, /* fovDeg */ 60.0f, /* near */ 0.5f, /* far */ 2000.0f, /* primary */ true);

    const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
    m_cube = world().createEntity();
    world().emplace<TagComponent>(m_cube, "Cube");
    world().emplace<TransformComponent>(m_cube, Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    auto& meshComp       = world().emplace<MeshComponent>(m_cube);
    meshComp.meshAssetID = NULL_ASSET;
    meshComp.matAssetID  = matId;
    meshComp.castShadow  = true;

    DE_LOG_INFO(
        "SandboxApp: cube mesh {} verts / {} indices, aspect {:.3f}, material id={}, albedo {}x{}, terrain {}x{} chunks",
        m_cubeMesh.vertexCount(),
        m_cubeMesh.indexCount(),
        aspect,
        matId,
        m_cubeMaterial->albedo().width(),
        m_cubeMaterial->albedo().height(),
        m_terrain.chunksX(),
        m_terrain.chunksZ());
}

void SandboxApp::onUpdate(float dt)
{
    handleRuntimeCommands(dt);
    m_env.tick(dt);
    m_water.tick(dt);
    m_water.updateLod(m_viewCamera.GetPosition());
    syncTerrainLod();
}

void SandboxApp::onRender()
{
    renderer().beginFrame();

    auto* cmd = renderer().commandList();

    Aabb3f sceneBounds = m_terrain.bounds();
    if (auto* xf = world().get<TransformComponent>(m_cube))
        sceneBounds.ExpandToInclude(xf->position);
    m_shadows.update(
        m_viewCamera,
        m_env.lightDir(),
        sceneBounds,
        m_env.sunElevation(),
        m_env.weather.cloudCoverage,
        renderer().frameIndex());

    if (m_shadows.isValid() && m_shadows.enabled())
    {
        m_shadows.beginCapture(cmd);
        for (int i = 0; i < m_shadows.cascadeCount(); ++i)
        {
            m_shadows.beginCascade(cmd, i);
            m_terrain.drawDepth(cmd);
            if (auto* xf = world().get<TransformComponent>(m_cube))
            {
                const Matrix4f worldMat = makeWorldMatrix(*xf);
                const Matrix4f wvp      = worldMat * m_shadows.cascade(i).viewProj;
                m_shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
                m_cubeMesh.draw(cmd);
            }
        }
        m_shadows.endCapture(cmd);
        renderer().bindSceneTargets();
    }
    else if (m_shadows.isValid())
    {
        m_shadows.endCapture(cmd);
    }

    const Frustum3f frustum(m_viewCamera.GetViewProj());
    m_skyPipeline.draw(cmd, m_viewCamera, m_env);
    m_terrain.draw(cmd, m_terrainPipeline, m_terrainMaterial, m_viewCamera, &frustum, &m_env, &m_shadows);

    m_meshPipeline.bind(cmd);

    AssetRef<Material> material;
    if (auto* meshComp = world().get<MeshComponent>(m_cube))
        material = assets().getAs<Material>(meshComp->matAssetID);

    if (!material)
        material = m_cubeMaterial;

    if (material && material->isValid())
        material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
    m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);

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

    const Vector3f camPos = m_viewCamera.GetPosition();
    cb.lightDirWS[0]   = m_env.lightDir().x;
    cb.lightDirWS[1]   = m_env.lightDir().y;
    cb.lightDirWS[2]   = m_env.lightDir().z;
    cb.ambientScale    = 0.22f;
    cb.lightColor[0]   = m_env.lightColor().x;
    cb.lightColor[1]   = m_env.lightColor().y;
    cb.lightColor[2]   = m_env.lightColor().z;
    cb.cameraPos[0]    = camPos.x;
    cb.cameraPos[1]    = camPos.y;
    cb.cameraPos[2]    = camPos.z;

    m_meshPipeline.setConstants(cmd, cb);
    m_cubeMesh.draw(cmd);

    m_water.draw(cmd, m_waterPipeline, m_viewCamera, &frustum, &m_env);

    renderer().stats().drawCalls = m_terrain.lastDrawCalls() + m_water.lastDrawCalls() + 2;
    renderer().stats().triangles =
        m_terrain.lastTriangles() + m_water.lastTriangles() + m_cubeMesh.indexCount() / 3;

    drawDebugOverlays(cmd);
    renderer().endFrame();
}

void SandboxApp::drawDebugOverlays(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_debugOverlay.isValid())
        return;
    if (!m_showShadowMaps && !m_showDepth)
        return;

    // Unbind the DSV so we can sample the scene depth. Do not rebind it afterwards
    // while it remains PIXEL_SHADER_RESOURCE (endFrame does not write depth).
    renderer().bindColorTargetOnly();
    m_debugOverlay.beginFrame(renderer().frameIndex());

    const LONG sw = static_cast<LONG>(renderer().width());
    const LONG sh = static_cast<LONG>(renderer().height());
    const LONG pad = 12;
    LONG tile = sh / 5;
    if (tile < 96)
        tile = 96;
    if (tile > 220)
        tile = 220;

    if (m_showDepth && renderer().depthResource())
    {
        renderer().transitionDepth(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const LONG x = pad;
        const LONG y = sh - pad - tile;
        m_debugOverlay.draw2D(
            cmd, renderer().device(), renderer().depthSrvCpu(), x, y, tile, tile, 24.0f, false);
    }

    if (m_showShadowMaps && m_shadows.isValid())
    {
        const int n = m_shadows.cascadeCount();
        LONG x0 = pad;
        if (m_showDepth)
            x0 += tile + pad;
        const LONG y = sh - pad - tile;
        const LONG gap = 8;
        LONG tw = tile;
        const LONG need = n * tw + (n - 1) * gap;
        if (x0 + need > sw - pad && n > 0)
        {
            const LONG avail = sw - pad - x0 - (n - 1) * gap;
            if (avail > 64)
                tw = avail / n;
        }
        const float slice0 = static_cast<float>(m_shadows.debugSliceOffset());
        for (int i = 0; i < n; ++i)
        {
            const LONG x = x0 + i * (tw + gap);
            m_debugOverlay.drawArray(
                cmd,
                renderer().device(),
                m_shadows.srvCpu(),
                x,
                y,
                tw,
                tile,
                slice0 + static_cast<float>(i),
                1.25f,
                true);
        }
    }
}

void SandboxApp::onShutdown()
{
    renderer().waitForGpu();
    if (m_cubeMaterial)
        assets().unload(m_cubeMaterial->id);
    m_cubeMaterial.reset();
    m_water = Water::WaterWorld{};
    m_terrainMaterial = TerrainMaterial{};
    m_terrain = Terrain::TerrainWorld{};
    m_shadows = ShadowSystem{};
    m_debugOverlay = DebugOverlay{};
    DE_LOG_INFO("SandboxApp: shutdown");
}
