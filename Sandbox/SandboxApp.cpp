#include "SandboxApp.h"

#include "ECS/Components.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Input/InputCodes.h"
#include "Collision/Collision.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Network/NetTypes.h"
#include "Network/Replication.h"
#include "Render/Frustum3f.h"
#include "Render/TaaJitter.h"
#include "Render/MeshGen.h"
#include "Terrain/SplatMap.h"
#include "Water/WaterWaves.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace Dark;
using namespace Math;
using namespace Terrain;
using namespace Audio;

MeshPass liveMeshPass(const Renderer& r)
{
    return r.scenePath() == ScenePath::HybridDeferred ? MeshPass::GBuffer : MeshPass::ForwardUnorm;
}

TerrainPass liveTerrainPass(const Renderer& r)
{
    return r.scenePath() == ScenePath::HybridDeferred ? TerrainPass::GBuffer : TerrainPass::ForwardUnorm;
}

bool useAcesTonemap(const Renderer& r)
{
    return r.hasSceneBuffers() && r.debugState().aces && r.debugState().lighting;
}

SkyPass liveSkyPass(const Renderer& r)
{
    return r.scenePath() == ScenePath::HybridDeferred ? SkyPass::DeferredLast : SkyPass::ForwardFirst;
}

void mountContentRoots(AssetManager& assets)
{
    namespace fs = std::filesystem;

    const std::vector<fs::path> candidates = contentRootCandidates();

    bool any = false;
    for (const fs::path& c : candidates)
    {
        std::error_code ec;
        if (!c.empty() && fs::exists(c, ec) && !ec && fs::is_directory(c, ec) && !ec)
        {
            assets.mountDirectory(c);
            any = true;
        }
    }

    if (!any)
    {
        std::string listed;
        for (const fs::path& c : candidates)
        {
            if (!listed.empty())
                listed += " | ";
            listed += c.string();
        }
        DE_LOG_ERROR("SandboxApp: no content directory found. Tried: {}", listed.empty() ? std::string("<none>") : listed);
    }
}

constexpr float kDeathSeconds      = 2.5f;
constexpr float kSpawnFocusSeconds = 1.75f;

void copyMatrix(float dst[16], const Math::Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

void fillMeshGBufferXforms(MeshGBufferConstants& cb, const Matrix4f& world, const Matrix4f& viewProj, const Matrix4f& prevViewProj, const Matrix4f& prevWorld)
{
    copyMatrix(cb.worldViewProj, world * viewProj);
    copyMatrix(cb.world, world);
    copyMatrix(cb.prevWorldViewProj, prevWorld * prevViewProj);
}

Math::Matrix4f makeWorldMatrix(const TransformComponent& xf)
{
    const Matrix4f S = Matrix4f::ScaleMatrixXYZ(xf.scale.x, xf.scale.y, xf.scale.z);
    const Matrix4f R = xf.rotation.ToMatrix4();
    const Matrix4f T = Matrix4f::TranslationMatrix(xf.position.x, xf.position.y, xf.position.z);
    return S * R * T;
}

Math::Matrix4f healthPackWorldMatrix(const Vector3f& pos, float spin, float bob)
{
    const Quaternion rot = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, spin);
    const float      y   = pos.y + 0.08f * std::sinf(bob * 2.6f);
    return Matrix4f::ScaleMatrixXYZ(0.9f, 0.9f, 0.9f) * rot.ToMatrix4() * Matrix4f::TranslationMatrix(pos.x, y, pos.z);
}

void drawShadowCaster(ID3D12GraphicsCommandList* cmd, const ShadowSystem& shadows, int cascade, const Matrix4f& world, const Mesh& mesh)
{
    const Matrix4f wvp = world * shadows.cascade(cascade).viewProj;
    shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
    mesh.draw(cmd);
}

const char* netRoleName(NetRole role)
{
    switch (role)
    {
    case NetRole::Idle:
        return "Idle";
    case NetRole::Joining:
        return "Joining";
    case NetRole::Host:
        return "Host";
    case NetRole::Client:
        return "Client";
    default:
        return "?";
    }
}

uint32_t pawnPaletteColor(ClientId id)
{
    static constexpr uint32_t kPalette[8] = {
        0x3DA6F2FFu, 0xE85D4CFFu, 0x5BD96CFFu, 0xF2C14EFFu, 0xC86BFFFFu, 0xF28C3CFFu, 0x4CD4E8FFu, 0xE8E8E8FFu,
    };
    const unsigned i = static_cast<unsigned>(id);
    return kPalette[i < 8u ? i : 0u];
}

void unpackRgba8(uint32_t rgba, float out[4])
{
    out[0] = static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f;
    out[1] = static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f;
    out[2] = static_cast<float>((rgba >> 8) & 0xFFu) / 255.0f;
    out[3] = static_cast<float>(rgba & 0xFFu) / 255.0f;
}

void SandboxApp::registerDefaultActions()
{
    ActionMap& a = input().actions();
    a.clear();

    a.bindKey("quit", Key::Escape);
    a.bindButton("quit", GamepadButton::Back);

    a.bindKey("pause", Key::P);
    a.bindButton("pause", GamepadButton::Start);
    a.bindKey("step", Key::O);

    a.bindKey("jump", Key::Space);
    a.bindButton("jump", GamepadButton::A);
    a.bindKey("attack", Key::F);
    a.bindButton("attack", GamepadButton::B);

    a.bindKey("reset", Key::R);
    a.bindButton("reset", GamepadButton::Y);

    a.bindKey("speed_up", Key::Equal);
    a.bindButton("speed_up", GamepadButton::RightShoulder);
    a.bindKey("speed_down", Key::Minus);
    a.bindButton("speed_down", GamepadButton::X);

    // Cube yaw/pitch: WASD only (host). Arrows + D-pad drive the local pawn XZ.
    a.bindKeyAsAxis("yaw", Key::Q, -1.0f);
    a.bindKeyAsAxis("yaw", Key::E, 1.0f);
    a.bindKeyAsAxis("pitch", Key::Z, 1.0f);
    a.bindKeyAsAxis("pitch", Key::C, -1.0f);

    a.bindKeyAsAxis("pawn_x", Key::Left, -1.0f);
    a.bindKeyAsAxis("pawn_x", Key::Right, 1.0f);
    a.bindButtonAsAxis("pawn_x", GamepadButton::DPadLeft, -1.0f);
    a.bindButtonAsAxis("pawn_x", GamepadButton::DPadRight, 1.0f);
    a.bindKeyAsAxis("pawn_z", Key::Up, 1.0f);
    a.bindKeyAsAxis("pawn_z", Key::Down, -1.0f);
    a.bindButtonAsAxis("pawn_z", GamepadButton::DPadUp, 1.0f);
    a.bindButtonAsAxis("pawn_z", GamepadButton::DPadDown, -1.0f);

    a.bindKeyAsAxis("move_x", Key::A, -1.0f);
    a.bindKeyAsAxis("move_x", Key::D, 1.0f);
    a.bindAxis("move_x", GamepadAxis::LeftX, 1.0f);
    a.bindKeyAsAxis("move_z", Key::W, 1.0f);
    a.bindKeyAsAxis("move_z", Key::S, -1.0f);
    a.bindAxis("move_z", GamepadAxis::LeftY, 1.0f);
    a.bindKey("sprint", Key::LeftShift);

    a.bindKeyAsAxis("fly_forward", Key::W, 1.0f);
    a.bindKeyAsAxis("fly_forward", Key::S, -1.0f);
    a.bindAxis("fly_forward", GamepadAxis::LeftY, 1.0f);
    a.bindKeyAsAxis("fly_strafe", Key::A, -1.0f);
    a.bindKeyAsAxis("fly_strafe", Key::D, 1.0f);
    a.bindAxis("fly_strafe", GamepadAxis::LeftX, 1.0f);
    a.bindKeyAsAxis("fly_climb", Key::Q, 1.0f);
    a.bindKeyAsAxis("fly_climb", Key::Space, 1.0f);
    a.bindKeyAsAxis("fly_climb", Key::Z, -1.0f);
    a.bindKeyAsAxis("fly_climb", Key::LeftControl, -1.0f);
    a.bindAxis("fly_climb", GamepadAxis::RightTrigger, 1.0f);
    a.bindAxis("fly_climb", GamepadAxis::LeftTrigger, -1.0f);

    a.bindAxis("look_yaw", GamepadAxis::RightX, 1.0f);
    a.bindAxis("look_pitch", GamepadAxis::RightY, 1.0f);

    a.bindButton("sprint", GamepadButton::LeftThumb);
    a.bindButton("sprint", GamepadButton::LeftShoulder);

    a.bindKey("dev_tools", Key::M);

    DE_LOG_INFO(
        "Input: quit(Esc/Back) pause(P/Start) freeze gameplay + fly cam  step(O)  reset(R/Y) speed(+/- / RB) "
        "possessed WASD/LS move, mouse+RS look, Space/A jump (tap again quickly for a higher jump), LMB/F/B attack, Shift/LB sprint, swim in water, "
        "M dev tools  -forward for UNORM forward");
}

void SandboxApp::handleRuntimeCommands(float dt)
{
    handleNetHotkeys();
    applyNetRole();

    const bool uiKeys = m_showDevTools && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    if (!uiKeys && input().actionPressed("dev_tools"))
    {
        m_showDevTools = !m_showDevTools;
        audio().play2D(m_sfxClick, 0.5f);
        DE_LOG_INFO("Sandbox: dev tools = {}", m_showDevTools);
    }

    if (input().actionPressed("quit"))
    {
        DE_LOG_INFO("Command: quit");
        requestQuit();
        return;
    }

    if (!uiKeys && input().actionPressed("pause"))
    {
        m_gameplayPaused = !m_gameplayPaused;
        m_stepGameplay   = false;
        audio().play2D(m_sfxClick, 0.5f);
        DE_LOG_INFO("Sandbox: gameplay paused = {}", m_gameplayPaused);
    }
    if (!uiKeys && m_gameplayPaused && input().actionPressed("step"))
        m_stepGameplay = true;

    if (!uiKeys && input().actionPressed("reset"))
    {
        m_spinSpeed  = 0.8f;
        Vector3f pos{};
        if (m_cube.valid())
        {
            if (auto* xf = world().get<TransformComponent>(m_cube))
            {
                xf->rotation = Quaternion::IDENTITY;
                pos          = xf->position;
            }
        }
        audio().play3D(m_sfxReset, pos, 0.7f);
        DE_LOG_INFO("Command: reset cube");
    }

    if (!uiKeys && input().actionPressed("speed_up"))
    {
        m_spinSpeed += 0.2f;
        if (m_spinSpeed > 5.0f)
            m_spinSpeed = 5.0f;
        DE_LOG_INFO("Command: spin speed = {:.2f}", m_spinSpeed);
    }

    if (!uiKeys && input().actionPressed("speed_down"))
    {
        m_spinSpeed -= 0.2f;
        if (m_spinSpeed < 0.0f)
            m_spinSpeed = 0.0f;
        DE_LOG_INFO("Command: spin speed = {:.2f}", m_spinSpeed);
    }

    window().setCursorCaptured(window().isFocused() && !m_showDevTools);
    if (m_gameplayPaused)
        updateFlyCamera(dt);
    else
    {
        updatePossessed(dt);
        updateShoulderCamera();
    }

    if (m_gameplayPaused && !m_stepGameplay)
        return;

    if (auto* xf = m_cube.valid() ? world().get<TransformComponent>(m_cube) : nullptr)
    {
        const NetRole role = network().role();
        if (role == NetRole::Host)
        {
            constexpr float kTurnRate = 1.8f; // rad/s
            const float     yawCmd    = input().actionAxis("yaw");
            const float     pitchCmd  = input().actionAxis("pitch");
            if (yawCmd != 0.0f || pitchCmd != 0.0f)
            {
                const Quaternion yawQ   = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, yawCmd * kTurnRate * dt);
                const Quaternion pitchQ = Quaternion::FromAxisAngle(Vector3f::X_AXIS, pitchCmd * kTurnRate * dt);
                xf->rotation            = yawQ * pitchQ * xf->rotation;
                xf->rotation.Normalize();
            }
        }

        // Host (and offline Idle) still spin the cube. Clients must not.
        if (role == NetRole::Host || role == NetRole::Idle)
        {
            const Quaternion spin = Quaternion::FromAxisAngle(Vector3f::Y_AXIS, m_spinSpeed * dt);
            xf->rotation          = spin * xf->rotation;
            xf->rotation.Normalize();
        }

        xf->position.y = m_terrain.heightAtWorld(xf->position.x, xf->position.z) + 0.5f;
    }
}

void SandboxApp::updateFlyCamera(float dt)
{
    const bool uiKeys  = m_showDevTools && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    const bool uiMouse = m_showDevTools && m_imgui.isReady() && m_imgui.wantCaptureMouse();

    if (!uiKeys)
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
    }

    constexpr float kSens    = 0.0045f;
    constexpr float kPadLook = 2.1f;
    if (!uiMouse)
    {
        if (!m_showDevTools || input().mouseDown(MouseButton::Right))
        {
            m_viewCamera.RotateY(static_cast<float>(input().mouseDeltaX()) * kSens);
            m_viewCamera.Pitch(static_cast<float>(input().mouseDeltaY()) * kSens);
        }
    }

    const float lookYaw   = input().actionAxis("look_yaw");
    const float lookPitch = input().actionAxis("look_pitch");
    if (lookYaw != 0.0f)
        m_viewCamera.RotateY(lookYaw * kPadLook * dt);
    if (lookPitch != 0.0f)
        m_viewCamera.Pitch(lookPitch * kPadLook * dt);

    if (auto* xf = world().get<TransformComponent>(m_camera))
        xf->position = m_viewCamera.GetPosition();
}

void SandboxApp::devNetHost()
{
    m_netBrowsing    = false;
    m_browseLogCount = ~0u;
    if (network().host(kNetDefaultPort))
        DE_LOG_INFO(LogCategory::Networking, "Sandbox: hosting on port {}", kNetDefaultPort);
}

void SandboxApp::devNetJoin(const Address& addr)
{
    m_netBrowsing    = false;
    m_browseLogCount = ~0u;
    if (network().join(addr))
    {
        const uint32_t ip = addr.ipv4;
        DE_LOG_INFO(LogCategory::Networking, "Sandbox: joining {}.{}.{}.{}:{}", (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u, addr.port);
    }
}

void SandboxApp::devNetDisconnect()
{
    m_netBrowsing    = false;
    m_browseLogCount = ~0u;
    network().disconnect();
    DE_LOG_INFO(LogCategory::Networking, "Sandbox: disconnect");
}

void SandboxApp::devNetBrowse()
{
    if (network().role() != NetRole::Idle)
        DE_LOG_WARN(LogCategory::Networking, "Sandbox: browse requires Idle (disconnect first)");
    else if (network().browse())
    {
        m_netBrowsing    = true;
        m_browseLogCount = ~0u;
        DE_LOG_INFO(LogCategory::Networking, "Sandbox: browsing LAN :{} (same-PC two binds of :{} is unreliable; join by IP)", kNetBeaconPort, kNetBeaconPort);
    }
    else
        DE_LOG_WARN(LogCategory::Networking, "Sandbox: browse bind failed; typed IP / CLI still work");
}

void SandboxApp::devToggleListen()
{
    if (debug().isListening())
    {
        debug().shutdown();
        DE_LOG_INFO(LogCategory::Debug, "Sandbox: Visual Debugger listen stopped");
    }
    else if (debug().listen(kDebugDefaultPort))
        DE_LOG_INFO(LogCategory::Debug, "Sandbox: Visual Debugger listening TCP {}", debug().boundAddress().port);
    else
        DE_LOG_ERROR(LogCategory::Debug, "Sandbox: Visual Debugger listen failed");
}

void SandboxApp::handleNetHotkeys()
{
    if (m_netBrowsing && network().role() == NetRole::Idle)
    {
        const uint32_t n = network().sessionCount();
        if (n != m_browseLogCount)
        {
            m_browseLogCount = n;
            DE_LOG_INFO(LogCategory::Networking, "Sandbox: {} LAN session(s)", n);
            for (uint32_t i = 0; i < n; ++i)
            {
                NetSessionInfo s{};
                if (!network().sessionAt(i, s))
                    continue;
                const uint32_t ip = s.address.ipv4;
                DE_LOG_INFO(LogCategory::Networking, "Sandbox: session '{}' {}.{}.{}.{}:{} peers {} mode {}",
                            s.name[0] ? s.name : "(unnamed)",
                            (ip >> 24) & 255u, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u,
                            s.address.port, s.peerCount, s.sceneMode);
            }
        }
    }
    else if (network().role() != NetRole::Idle)
        m_netBrowsing = false;
}

void SandboxApp::applyNetRole()
{
    const NetRole role = network().role();
    if (role != m_netRole)
    {
        DE_LOG_INFO(
            LogCategory::Networking,
            "Sandbox: role {} peers {} rtt {:.1f}ms pkts in/out {}/{}",
            netRoleName(role),
            network().peerCount(),
            network().rttMs(network().localClientId()),
            network().packetsIn(),
            network().packetsOut());
        m_netRole = role;
    }

    // Idle-tagged replicas (netId=0) must not sit beside the host's spawned cube/pawns.
    if (role == NetRole::Client)
    {
        std::vector<Entity> stale;
        world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
            if (nc.netId == NULL_NET_ID)
                stale.push_back(e);
        });
        for (Entity e : stale)
            network().unregisterEntity(world(), e);
        m_cube = {};
    }

    if (role == NetRole::Host && !network().localPawn().valid())
        spawnOwnedPawn(ClientId::Host, -2.0f);

    if (role == NetRole::Idle)
        ensureLocalCube();
}

void SandboxApp::updatePawnMotion(float dt)
{
    const Entity pawn = network().localPawn();
    if (!pawn.valid())
        return;
    TransformComponent* xf = world().get<TransformComponent>(pawn);
    if (!xf)
        return;

    const float ax = input().actionAxis("pawn_x");
    const float az = input().actionAxis("pawn_z");
    if (ax != 0.0f || az != 0.0f)
    {
        Vector3f delta{ ax, 0.0f, az };
        const float mag = delta.Magnitude();
        if (mag > 1.0f)
            delta *= (1.0f / mag);
        xf->position += delta * (kNetPawnMaxSpeed * dt);
    }

    xf->position.y = m_terrain.heightAtWorld(xf->position.x, xf->position.z) + 0.5f;
}

Entity SandboxApp::possessedBody()
{
    const Entity pawn = network().localPawn();
    if (pawn.valid())
        return pawn;
    return m_chase.walker();
}

void SandboxApp::updatePossessed(float dt)
{
    const Entity body = possessedBody();
    TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    if (!xf)
        return;

    constexpr float kMouseSens = 0.0045f;
    constexpr float kPadLook   = 2.1f;
    constexpr float kRadius    = 0.45f;

    if (!m_showDevTools)
    {
        m_lookYaw += static_cast<float>(input().mouseDeltaX()) * kMouseSens;
        // mouseDeltaY is already up-positive; add it so mouse-up looks up.
        m_lookPitch += static_cast<float>(input().mouseDeltaY()) * kMouseSens;
    }
    m_lookYaw += input().actionAxis("look_yaw") * kPadLook * dt;
    m_lookPitch += input().actionAxis("look_pitch") * kPadLook * dt;
    m_lookYaw   = Math::WrapPi(m_lookYaw);
    m_lookPitch = Math::Clamp(m_lookPitch, -0.96f, 0.96f);

    Vector3f look{ std::sinf(m_lookYaw) * std::cosf(m_lookPitch), std::sinf(m_lookPitch), std::cosf(m_lookYaw) * std::cosf(m_lookPitch) };
    Vector3f flat = look;
    flat.y = 0.0f;
    if (flat.MagnitudeSqrd() > 1.0e-6f)
        flat.Normalize();
    Vector3f right = Vector3f{ 0.0f, 1.0f, 0.0f }.Cross(flat);
    if (right.MagnitudeSqrd() > 1.0e-6f)
        right.Normalize();

    const bool uiKeys = m_showDevTools && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    const float mx = uiKeys ? 0.0f : input().actionAxis("move_x");
    const float mz = uiKeys ? 0.0f : input().actionAxis("move_z");
    Vector3f wish = right * mx + flat * mz;
    const float mag = wish.Magnitude();
    if (mag > 1.0f)
        wish *= (1.0f / mag);

    if (!m_havePlayerSpawn)
    {
        m_playerSpawn     = xf->position;
        m_havePlayerSpawn = true;
    }

    PlayerMotorInput motorIn{};
    motorIn.wish        = m_playerHealth.alive() ? wish : Vector3f{ 0.0f, 0.0f, 0.0f };
    motorIn.sprint      = m_playerHealth.alive() && !uiKeys && input().actionDown("sprint");
    motorIn.jumpPressed = m_playerHealth.alive() && !uiKeys && input().actionPressed("jump");

    struct HeightCtx
    {
        Terrain::TerrainWorld* terrain;
    };
    HeightCtx ctx{ &m_terrain };
    PlayerGroundQuery ground{};
    ground.user = &ctx;
    ground.waterY = m_water.params().waterLevel;
    ground.heightAt = [](void* user, float x, float z) -> float {
        return static_cast<HeightCtx*>(user)->terrain->heightAtWorld(x, z);
    };

    const Vector3f before = xf->position;
    const PlayerMotorResult motorOut = m_motor.tick(xf->position, motorIn, dt, ground);

    Vector3f delta{ xf->position.x - before.x, 0.0f, xf->position.z - before.z };
    if (delta.MagnitudeSqrd() > 1.0e-10f)
    {
        Sphere3f ball{ Vector3f{ before.x, xf->position.y, before.z }, kRadius };
        for (const Aabb3f& cube : m_chase.cubes())
        {
            const Dark::Collision::SweptHit3D hit = Dark::Collision::SweptIntersects(ball, delta, cube);
            if (hit.hit && hit.t < 1.0f)
            {
                delta *= Math::Max(0.0f, hit.t - 0.02f);
                break;
            }
        }
        xf->position.x = before.x + delta.x;
        xf->position.z = before.z + delta.z;
        if (dt > 1.0e-4f)
            m_motor.setHorizontalVelocity(delta.x / dt, delta.z / dt);
    }

    m_playerWet = m_motor.state() == PlayerMoveState::Swimming;

    if (motorOut.jumped)
        audio().play2D(m_sfxGrunt, 0.7f);
    if (motorOut.landed)
        audio().play2D(m_sfxLand, 0.75f);
    if (motorOut.splashed)
        audio().play2D(m_sfxSplash, 0.8f);
    if (motorOut.landed || motorOut.splashed)
        m_footstepAcc = 0.0f;

    const float stepSpeed = Vector3f{ delta.x, 0.0f, delta.z }.Magnitude() / Math::Max(dt, 1.0e-4f);
    const bool  stepping  = (m_motor.state() == PlayerMoveState::Grounded || m_motor.state() == PlayerMoveState::Swimming) && stepSpeed > 2.0f;
    if (stepping)
    {
        const float cadence = m_motor.state() == PlayerMoveState::Swimming ? 0.55f : 0.35f;
        m_footstepAcc += dt * (stepSpeed * cadence);
        if (m_footstepAcc >= 1.0f)
        {
            m_footstepAcc = 0.0f;
            if (m_motor.state() == PlayerMoveState::Swimming)
                audio().play2D(m_sfxSplash, 0.42f);
            else
                audio().play2D(m_sfxStep, 0.35f);
        }
    }
    else
        m_footstepAcc = 0.0f;

    if (m_playerWet)
    {
        if (m_waterVoice == 0 || !audio().isPlaying(m_waterVoice))
            m_waterVoice = audio().play2D(m_sfxWater, 0.28f, true);
    }
    else if (m_waterVoice != 0)
    {
        audio().stop(m_waterVoice);
        m_waterVoice = 0;
    }
}

void SandboxApp::respawnPlayer()
{
    const Entity body = possessedBody();
    if (TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr)
        xf->position = m_playerSpawn;
    m_motor.reset();
    m_playerHealth.revive();
    m_playerWet        = false;
    m_playerDeadTimer  = 0.0f;
    m_spawnAge         = 0.0f;
    m_attackCooldown   = 0.0f;
    m_hurtSoundTimer   = 0.0f;
    DE_LOG_INFO("Player: respawned");
}

TonemapSettings SandboxApp::playerPostFx()
{
    TonemapSettings s{};
    s.nearZ      = m_viewCamera.GetNearZ();
    s.farZ       = m_viewCamera.GetFarZ();
    s.focusRange = 12.0f;

    const Entity body = possessedBody();
    if (const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr)
    {
        const Vector3f focusPt{ xf->position.x, xf->position.y + 1.15f, xf->position.z };
        s.focusZ = (focusPt - m_viewCamera.GetPosition()).Magnitude();
    }

    if (!m_playerHealth.alive())
    {
        const float t    = Clamp(m_playerDeadTimer / kDeathSeconds, 0.0f, 1.0f);
        s.blur           = SmoothStep(0.0f, 0.55f, t);
        s.uniformBlur    = SmoothStep(0.0f, 0.70f, t);
        s.fade           = SmoothStep(0.30f, 1.00f, t);
        return s;
    }

    const float u    = SmoothStep(0.0f, kSpawnFocusSeconds, m_spawnAge);
    s.blur           = 1.0f - u;
    s.uniformBlur    = 1.0f - u;
    s.fade           = 0.0f;
    return s;
}

void SandboxApp::updateCombat(float dt)
{
    if (m_hurtSoundTimer > 0.0f)
        m_hurtSoundTimer -= dt;
    if (m_attackCooldown > 0.0f)
        m_attackCooldown -= dt;

    m_playerHealth.tick(dt);

    if (!m_playerHealth.alive())
    {
        m_playerDeadTimer += dt;
        if (m_playerDeadTimer >= kDeathSeconds)
            respawnPlayer();
        return;
    }

    if (!m_chaseOk)
        return;

    constexpr float kStandoff = 2.25f;
    constexpr float kContactDps = 12.0f;
    const Entity body = possessedBody();
    const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    if (!xf)
        return;

    const float before = m_playerHealth.hp();
    for (int i = 0; i < m_chase.hunterCount(); ++i)
    {
        if (!m_chase.hunterAlive(i))
            continue;
        const Vector3f& hp = m_chase.hunterPos(i);
        const float dx = hp.x - xf->position.x;
        const float dz = hp.z - xf->position.z;
        if (dx * dx + dz * dz > kStandoff * kStandoff)
            continue;
        if (m_playerHealth.applyDamage(kContactDps * dt) )
        {
            DE_LOG_INFO("Player: down");
            audio().play2D(m_sfxReset, 0.55f);
        }
    }
    if (m_playerHealth.hp() < before && m_hurtSoundTimer <= 0.0f)
    {
        audio().play2D(m_sfxPain, 0.7f);
        m_hurtSoundTimer = 0.40f;
    }

    const bool attack = input().actionPressed("attack") || (!m_showDevTools && input().mousePressed(MouseButton::Left));
    if (!m_playerHealth.alive() || !attack || m_attackCooldown > 0.0f)
        return;

    m_attackCooldown = 0.45f;
    audio().play2D(m_sfxClick, 0.4f);

    Vector3f look{ std::sinf(m_lookYaw), 0.0f, std::cosf(m_lookYaw) };
    if (look.MagnitudeSqrd() > 1.0e-6f)
        look.Normalize();
    else
        look = Vector3f{ 0.0f, 0.0f, 1.0f };

    constexpr float kMeleeRange = 2.7f;
    constexpr float kMeleeDot   = 0.25f;
    constexpr float kMeleeDmg   = 16.0f;
    for (int i = 0; i < m_chase.hunterCount(); ++i)
    {
        if (!m_chase.hunterAlive(i))
            continue;
        Vector3f to = m_chase.hunterPos(i) - xf->position;
        to.y        = 0.0f;
        const float dist = to.Magnitude();
        if (dist > kMeleeRange || dist < 1.0e-4f)
            continue;
        to *= (1.0f / dist);
        if (look.Dot(to) < kMeleeDot)
            continue;
        const bool wasAlive = m_chase.hunterAlive(i);
        if (m_chase.applyHunterDamage(i, kMeleeDmg))
        {
            const Vector3f hit = m_chase.hunterPos(i);
            audio().play3D(m_sfxPain, hit, 0.75f);
            spawnHunterBlood(hit);
            if (wasAlive && !m_chase.hunterAlive(i))
                m_bloodSplats.spawn(hit.x, hit.z, m_terrain.heightMap());
        }
    }
}

void SandboxApp::spawnHunterBlood(const Vector3f& pos)
{
    m_blood.setTransform(Vector3f{ pos.x, pos.y + 0.45f, pos.z });
    m_blood.emitBurst(12);
}

void SandboxApp::placeHealthPacks()
{
    m_healthPackCount = 0;
    const Vector3f spots[] = {
        { 5.0f, 0.0f, 5.0f },
        { -7.0f, 0.0f, 7.0f },
        { 8.0f, 0.0f, -8.0f },
        { -4.0f, 0.0f, -7.0f },
    };
    const float waterY = m_water.params().waterLevel;
    for (const Vector3f& s : spots)
    {
        if (m_healthPackCount >= kMaxHealthPacks)
            break;
        const float gy = m_terrain.heightAtWorld(s.x, s.z);
        if (gy < waterY - 0.2f)
            continue;
        HealthPack& p = m_healthPacks[m_healthPackCount++];
        p.pos       = Vector3f{ s.x, gy + 0.95f, s.z };
        p.active    = true;
        p.respawnIn = 0.0f;
    }
    DE_LOG_INFO("SandboxApp: {} health packs", m_healthPackCount);
}

void SandboxApp::updateHealthPacks(float dt)
{
    m_packSpin += 1.85f * dt;
    if (m_packSpin > Math::TwoPi)
        m_packSpin -= Math::TwoPi;
    m_packBob += dt;

    const Entity body = possessedBody();
    TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;

    constexpr float kPickupR  = 1.2f;
    constexpr float kHeal     = 50.0f;
    constexpr float kRespawn  = 16.0f;
    const float     r2        = kPickupR * kPickupR;

    for (int i = 0; i < m_healthPackCount; ++i)
    {
        HealthPack& p = m_healthPacks[i];
        if (!p.active)
        {
            p.havePrevWorld = false;
            p.respawnIn -= dt;
            if (p.respawnIn <= 0.0f)
                p.active = true;
            continue;
        }
        if (!xf || !m_playerHealth.alive() || m_playerHealth.hp() >= m_playerHealth.maxHp())
            continue;
        const float dx = xf->position.x - p.pos.x;
        const float dy = xf->position.y - p.pos.y;
        const float dz = xf->position.z - p.pos.z;
        if (dx * dx + dy * dy + dz * dz > r2)
            continue;
        m_playerHealth.heal(kHeal);
        audio().play2D(m_sfxHeal, 0.7f);
        p.active    = false;
        p.respawnIn = kRespawn;
        DE_LOG_INFO("Player: health pack +{:.0f} ({:.0f}/{:.0f})", kHeal, m_playerHealth.hp(), m_playerHealth.maxHp());
    }
}

void SandboxApp::drawHealthPacks(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, MeshFrameConstants& cb)
{
    if (!cmd || !m_crossMesh.valid() || m_healthPackCount <= 0)
        return;

    m_meshPipeline.bind(cmd, renderer().debugState().fill);
    m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
    if (m_packMaterial && m_packMaterial->isValid())
        m_packMaterial->bind(cmd, MeshPipeline::kRootAlbedoSrv);

    cb.color[0] = 1.0f;
    cb.color[1] = 0.12f;
    cb.color[2] = 0.14f;
    cb.color[3] = 1.0f;

    for (int i = 0; i < m_healthPackCount; ++i)
    {
        const HealthPack& p = m_healthPacks[i];
        if (!p.active)
            continue;
        const Matrix4f world = healthPackWorldMatrix(p.pos, m_packSpin, m_packBob);
        copyMatrix(cb.worldViewProj, world * viewProj);
        copyMatrix(cb.world, world);
        m_meshPipeline.setConstants(cmd, cb);
        m_crossMesh.draw(cmd, renderer().debugState().fill == DebugFill::Points);
    }
}

void SandboxApp::drawHealthPacksGBuffer(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, const Matrix4f& prevViewProj)
{
    if (!cmd || !m_crossMesh.valid() || m_healthPackCount <= 0)
        return;

    const DebugFill fill = renderer().debugState().fill;
    m_meshPipeline.bind(cmd, fill);
    if (m_packMaterial && m_packMaterial->isValid())
        m_packMaterial->bind(cmd, MeshPipeline::kRootAlbedoSrv);

    MeshGBufferConstants cb{};
    cb.color[0] = 1.0f;
    cb.color[1] = 0.12f;
    cb.color[2] = 0.14f;
    cb.color[3] = 1.0f;

    for (int i = 0; i < m_healthPackCount; ++i)
    {
        HealthPack& p = m_healthPacks[i];
        if (!p.active)
            continue;
        const Matrix4f world     = healthPackWorldMatrix(p.pos, m_packSpin, m_packBob);
        const Matrix4f prevWorld = p.havePrevWorld ? p.prevWorld : world;
        fillMeshGBufferXforms(cb, world, viewProj, prevViewProj, prevWorld);
        m_meshPipeline.setGBufferConstants(cmd, cb);
        m_crossMesh.draw(cmd, fill == DebugFill::Points);
        p.prevWorld     = world;
        p.havePrevWorld = true;
    }
}

void SandboxApp::drawHealthPacksDepth(ID3D12GraphicsCommandList* cmd, int cascade)
{
    if (!cmd || !m_crossMesh.valid() || m_healthPackCount <= 0)
        return;
    for (int i = 0; i < m_healthPackCount; ++i)
    {
        const HealthPack& p = m_healthPacks[i];
        if (!p.active)
            continue;
        drawShadowCaster(cmd, m_shadows, cascade, healthPackWorldMatrix(p.pos, m_packSpin, m_packBob), m_crossMesh);
    }
}

void SandboxApp::updateShoulderCamera()
{
    const Entity body = possessedBody();
    const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    if (!xf)
        return;

    const Vector3f target{ xf->position.x, xf->position.y + 1.15f, xf->position.z };
    Vector3f look{ std::sinf(m_lookYaw) * std::cosf(m_lookPitch), std::sinf(m_lookPitch), std::cosf(m_lookYaw) * std::cosf(m_lookPitch) };
    look.Normalize();
    Vector3f right = look.Cross(Vector3f{ 0.0f, 1.0f, 0.0f });
    if (right.MagnitudeSqrd() < 1.0e-6f)
        right = Vector3f::X_AXIS;
    right.Normalize();

    constexpr float kBoom = 2.4f;
    constexpr float kMinBoom = 0.28f;
    Vector3f boom = look * -kBoom + right * 0.42f + Vector3f{ 0.0f, 0.28f, 0.0f };
    float boomLen = boom.Magnitude();
    if (boomLen < 1.0e-3f)
        boomLen = kBoom;
    Vector3f boomDir = boom * (1.0f / boomLen);

    Ray3f ray{ target, boomDir };
    const Dark::Collision::RayHit3D hit = m_terrain.raycast(ray, boomLen);
    if (hit.hit && hit.t < boomLen)
        boomLen = Math::Max(kMinBoom, hit.t - 0.12f);
    boomLen = Math::Max(kMinBoom, boomLen);

    Vector3f cam = target + boomDir * boomLen;
    if (m_playerWet)
        cam.y = Math::Max(cam.y, m_water.params().waterLevel + 0.45f);

    m_viewCamera.SetLens(1.04719755f, m_viewCamera.GetAspect(), 0.18f, 2000.0f);
    m_viewCamera.LookAt(cam, target, Vector3f{ 0.0f, 1.0f, 0.0f });
    if (auto* cxf = world().get<TransformComponent>(m_camera))
        cxf->position = m_viewCamera.GetPosition();
}

Entity SandboxApp::findPawn(ClientId owner)
{
    Entity found{};
    world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
        if (!found.valid() && nc.prefab == NetPrefab::PlayerPawn && nc.owner == owner)
            found = e;
    });
    return found;
}

void SandboxApp::spawnOwnedPawn(ClientId owner, float offsetX)
{
    if (findPawn(owner).valid())
        return;

    Vector3f pos{ offsetX, 0.0f, 0.0f };
    if (m_cube.valid())
    {
        if (const TransformComponent* xf = world().get<TransformComponent>(m_cube))
        {
            pos.x = xf->position.x + offsetX;
            pos.z = xf->position.z;
        }
    }
    pos.y = m_terrain.heightAtWorld(pos.x, pos.z) + 0.5f;

    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, "PlayerPawn");
    world().emplace<TransformComponent>(e, pos, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    if (!network().registerEntity(world(), e, NetPrefab::PlayerPawn, owner, pawnPaletteColor(owner)))
    {
        world().destroyEntity(e);
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register pawn for client {}", static_cast<unsigned>(owner));
    }
}

void SandboxApp::ensureLocalCube()
{
    if (m_cube.valid() && world().alive(m_cube))
        return;

    const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
    m_cube              = world().createEntity();
    world().emplace<TagComponent>(m_cube, "Cube");
    world().emplace<TransformComponent>(m_cube, Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    auto& meshComp       = world().emplace<MeshComponent>(m_cube);
    meshComp.meshAssetID = NULL_ASSET;
    meshComp.matAssetID  = m_cubeMatId;
    meshComp.castShadow  = true;
    network().registerEntity(world(), m_cube, NetPrefab::Cube);
}

bool SandboxApp::onNetSpawn(World&, Entity, NetPrefab, const TransformComponent&, uint32_t, void*)
{
    return true;
}

void SandboxApp::onNetDespawn(World&, Entity e, NetId, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (app && app->m_cube == e)
        app->m_cube = {};
}

void SandboxApp::onNetPeer(const NetPeerInfo& info, NetPeerEvent event, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;

    if (event == NetPeerEvent::Joined && info.wantsPawn)
        app->spawnOwnedPawn(info.id, 2.0f * static_cast<float>(static_cast<uint8_t>(info.id)));
    else if (event == NetPeerEvent::Left)
    {
        const Entity pawn = app->findPawn(info.id);
        if (pawn.valid())
            app->network().unregisterEntity(app->world(), pawn);
    }
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

    m_sfxReset = audio().loadOrBlip(assets(), "audio/whoosh.wav", 180.0f, 0.22f, 0.35f);
    m_sfxClick = audio().loadOrBlip(assets(), "audio/ui_click.wav", 1400.0f, 0.06f, 0.35f);
    m_sfxStep  = audio().loadOrBlip(assets(), "audio/place.wav", 90.0f, 0.05f, 0.4f);
    m_sfxWater  = audio().loadOrBlip(assets(), "audio/whoosh.wav", 70.0f, 0.8f, 0.25f);
    m_sfxGrunt  = audio().loadOrBlip(assets(), "audio/grunt.wav", 140.0f, 0.18f, 0.5f);
    m_sfxLand   = audio().loadOrBlip(assets(), "audio/land.wav", 70.0f, 0.12f, 0.55f);
    m_sfxSplash = audio().loadOrBlip(assets(), "audio/splash.wav", 220.0f, 0.22f, 0.45f);
    m_sfxPain   = audio().loadOrBlip(assets(), "audio/pain.wav", 380.0f, 0.12f, 0.5f);
    m_sfxHeal   = audio().loadOrBlip(assets(), "audio/coin.wav", 880.0f, 0.16f, 0.4f);
    m_music    = audio().loadWav(assets(), "audio/ambient_loop.wav");
    if (!m_music)
        m_music = audio().createTone(110.0f, 2.0f, 0.12f);
    audio().setMasterVolume(0.85f);

    if (!renderer().enableSceneBuffers(config().scenePath))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: SceneBuffers enable failed; SwapChainForward");

    if (!m_meshPipeline.create(renderer().device(), liveMeshPass(renderer())))
    {
        DE_LOG_FATAL("SandboxApp: MeshPipeline create failed");
        requestQuit();
        return;
    }
    if (!m_healthHud.create(renderer()))
        DE_LOG_ERROR("SandboxApp: health HUD failed");
    if (!m_particles.create(renderer()))
        DE_LOG_ERROR("SandboxApp: particle renderer failed");
    if (!m_bloodSplats.create(renderer()))
        DE_LOG_ERROR("SandboxApp: blood splat pool failed");
    {
        ParticleEmitterDesc blood{};
        blood.name          = "Blood";
        blood.maxParticles  = 128;
        blood.emissionRate  = 0.0f;
        blood.duration      = 0.0f;
        blood.looping       = false;
        blood.lifetime      = { 0.22f, 0.50f };
        blood.startSpeed    = { 1.8f, 4.2f };
        blood.startSize     = { 0.07f, 0.14f };
        blood.endSize       = { 0.02f, 0.05f };
        blood.startColor[0] = 0.72f;
        blood.startColor[1] = 0.04f;
        blood.startColor[2] = 0.06f;
        blood.startColor[3] = 0.95f;
        blood.endColor[0]   = 0.28f;
        blood.endColor[1]   = 0.00f;
        blood.endColor[2]   = 0.01f;
        blood.endColor[3]   = 0.00f;
        blood.gravity       = Vector3f{ 0.0f, -11.0f, 0.0f };
        blood.direction     = Vector3f{ 0.0f, 1.0f, 0.0f };
        blood.spreadDegrees = 75.0f;
        blood.shape         = ParticleEmitterDesc::Shape::Sphere;
        blood.shapeSize     = Vector3f{ 0.12f, 0.0f, 0.0f };
        blood.additiveBlend = false;
        m_blood.setDesc(blood);
        m_blood.stop(true);
    }
    if (!pumpBootFrame())
        return;
    if (!m_terrainPipeline.create(renderer().device(), liveTerrainPass(renderer())))
    {
        DE_LOG_FATAL("SandboxApp: TerrainPipeline create failed");
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;
    if (!m_waterPipeline.create(renderer().device(), renderer().sceneColorFormat()))
    {
        DE_LOG_FATAL("SandboxApp: WaterPipeline create failed");
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;
    if (!m_skyPipeline.create(renderer().device(), liveSkyPass(renderer()), renderer().sceneColorFormat()))
    {
        DE_LOG_FATAL("SandboxApp: SkyPipeline create failed");
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;
    if (!m_shadows.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: ShadowSystem create failed");
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;
    if (!m_debugOverlay.create(renderer().device()))
    {
        DE_LOG_WARN("SandboxApp: DebugOverlay create failed — depth/shadow tiles disabled");
    }
    if (!m_imgui.init(window(), renderer(), "sandbox_imgui.ini", false))
        DE_LOG_WARN("SandboxApp: ImGui init failed — Dev Tools (M) disabled");
    if (renderer().hasSceneBuffers() && !m_tonemap.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: TonemapPipeline create failed");
        requestQuit();
        return;
    }
    if (renderer().scenePath() == ScenePath::HybridDeferred && !m_lighting.create(renderer().device()))
    {
        DE_LOG_FATAL("SandboxApp: DeferredLightingPipeline create failed");
        requestQuit();
        return;
    }
    if (renderer().scenePath() == ScenePath::HybridDeferred && !m_motionBlur.create(renderer().device()))
        DE_LOG_WARN(LogCategory::Render, "SandboxApp: MotionBlurPipeline create failed — motion blur disabled");
    if (renderer().scenePath() == ScenePath::HybridDeferred && !m_taa.create(renderer().device()))
        DE_LOG_WARN(LogCategory::Render, "SandboxApp: TaaPipeline create failed — TAA disabled");
    if (!pumpBootFrame())
        return;

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
            requestQuit();
            return;
        }
        const float extent = 128.0f * 2.0f;
        base.setOrigin(Vector3f{ -0.5f * extent, 0.0f, -0.5f * extent });
        terrainDesc.heightMap = std::move(base);

        Terrain::SplatMap splat;
        if (!splat.generateFromHeight(terrainDesc.heightMap))
        {
            DE_LOG_FATAL("SandboxApp: splat generate failed");
            requestQuit();
            return;
        }
        if (!m_terrain.create(std::move(terrainDesc)))
        {
            DE_LOG_FATAL("SandboxApp: terrain create failed");
            requestQuit();
            return;
        }
        if (!m_terrainMaterial.createDefault(renderer(), splat))
        {
            DE_LOG_FATAL("SandboxApp: terrain material create failed");
            requestQuit();
            return;
        }
        if (!pumpBootFrame())
            return;
        m_terrain.updateLod(Vector3f{ 0.0f, 50.0f, -80.0f });
        if (!m_terrain.createGpu(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: terrain GPU upload failed");
            requestQuit();
            return;
        }
        if (!pumpBootFrame())
            return;

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
            requestQuit();
            return;
        }
        m_water.updateLod(Vector3f{ 0.0f, 50.0f, -80.0f });
        if (!m_water.createGpu(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: water GPU upload failed");
            requestQuit();
            return;
        }
        if (!pumpBootFrame())
            return;
        DE_LOG_INFO("SandboxApp: water level {:.2f}, {} wet chunks", waterLevel, m_water.wetChunkCount());
    }

    MeshData cubeData;
    CreateCube(cubeData, 1.0f);
    m_cubeMesh = Mesh::Create(renderer(), cubeData);
    if (!m_cubeMesh.valid())
    {
        DE_LOG_FATAL("SandboxApp: cube mesh upload failed");
        requestQuit();
        return;
    }
    MeshData crossData;
    if (!CreateCross(crossData, 1.0f, 0.30f, 0.22f) || !Mesh::tryCreate(renderer(), crossData, m_crossMesh))
    {
        DE_LOG_FATAL("SandboxApp: health pack mesh failed");
        requestQuit();
        return;
    }

    m_cubeMaterial = std::make_shared<Material>();
    if (!m_cubeMaterial->createFromAlbedoPath( renderer(), assets(), "textures/dark_engine_cube.png", /*fallback*/ 64, 166, 242, 255))
    {
        DE_LOG_FATAL("SandboxApp: material create failed");
        requestQuit();
        return;
    }

    if (!pumpBootFrame())
        return;

    m_treeTrunkMaterial = std::make_shared<Material>();
    if (!m_treeTrunkMaterial->createSolid(renderer(), assets(), 118, 78, 38, 255))
    {
        DE_LOG_FATAL("SandboxApp: tree trunk material failed");
        requestQuit();
        return;
    }
    m_treeMaterial = std::make_shared<Material>();
    if (!m_treeMaterial->createSolid(renderer(), assets(), 46, 140, 62, 255))
    {
        DE_LOG_FATAL("SandboxApp: tree canopy material failed");
        requestQuit();
        return;
    }
    m_aiMaterial = std::make_shared<Material>();
    if (!m_aiMaterial->createSolid(renderer(), assets(), 220, 90, 40, 255))
    {
        DE_LOG_FATAL("SandboxApp: ai material failed");
        requestQuit();
        return;
    }
    m_packMaterial = std::make_shared<Material>();
    if (!m_packMaterial->createSolid(renderer(), assets(), 214, 28, 36, 255))
    {
        DE_LOG_FATAL("SandboxApp: health pack material failed");
        requestQuit();
        return;
    }

    const AssetID matId = assets().registerAsset(m_cubeMaterial);
    if (matId == NULL_ASSET)
    {
        DE_LOG_FATAL("SandboxApp: material register failed");
        requestQuit();
        return;
    }
    m_cubeMatId = matId;

    m_terrainMaterial.setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_cubeMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_treeTrunkMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_treeMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_aiMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_packMaterial->setShadowSrv(renderer().device(), m_shadows.srvCpu());
    renderer().setShadowSrv(m_shadows.srvCpu());

    const float aspect = (renderer().height() > 0) ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height()) : 1.0f;
    m_viewCamera.SetLens(/*fovY*/ 1.04719755f /*60deg*/, aspect, 0.18f, 2000.0f);
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

    network().setWantsPawn(true);
    network().setSceneMode(0);
    network().setPlayerName("Sandbox");
    network().setSpawnCallback(&SandboxApp::onNetSpawn, this);
    network().setDespawnCallback(&SandboxApp::onNetDespawn, this);
    network().setPeerCallback(&SandboxApp::onNetPeer, this);
    network().registerEntity(world(), m_cube, NetPrefab::Cube);

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
    DE_LOG_INFO(LogCategory::Networking, "Sandbox net: Sandbox.exe -host   and   Sandbox.exe -join 127.0.0.1");
    DE_LOG_INFO(LogCategory::Networking, "Sandbox net: M opens Dev Tools (host / join / browse / debugger)");

    m_chaseOk = m_chase.init(renderer(), m_terrain, m_water, world(), m_cubeMesh, m_treeTrunkMaterial, m_treeMaterial, m_aiMaterial);
    if (!m_chaseOk)
        DE_LOG_ERROR(LogCategory::AI, "SandboxApp: path chase init failed");

    HealthSettings playerHp;
    playerHp.maxHp       = 100.0f;
    playerHp.regenPerSec = 10.0f;
    playerHp.regenDelay  = 3.5f;
    m_playerHealth       = Health{ playerHp };
    placeHealthPacks();
}

void SandboxApp::onSplashFinished()
{
    m_spawnAge = 0.0f;
    if (m_music)
        audio().setMusic(m_music, 0.10f);
}

void SandboxApp::onUpdate(float dt)
{
    handleRuntimeCommands(dt);
    if (!m_gameplayPaused || m_stepGameplay)
    {
        m_env.tick(dt);
        m_water.tick(dt);
        if (m_chaseOk)
            m_chase.tick(dt, world(), input(), m_terrain, possessedBody(), m_playerWet);
        updateCombat(dt);
        updateHealthPacks(dt);
        m_blood.update(dt);
        if (m_playerHealth.alive())
            m_spawnAge += dt;
        m_stepGameplay = false;
    }
    m_water.updateLod(m_viewCamera.GetPosition());
    syncTerrainLod();

    AudioListener lis{};
    lis.position = m_viewCamera.GetPosition();
    lis.forward  = m_viewCamera.GetLook();
    lis.up       = m_viewCamera.GetUp();
    audio().setListener(lis);
}

void SandboxApp::onRender()
{
    if (!renderer().beginFrame())
    {
        requestQuit();
        return;
    }

    if (m_imgui.isReady())
        m_imgui.beginFrame();

    auto* cmd = renderer().commandList();

    Aabb3f sceneBounds = m_terrain.bounds();
    world().each<NetworkedComponent>([&](Entity e, NetworkedComponent&) {
        if (const TransformComponent* xf = world().get<TransformComponent>(e))
            sceneBounds.ExpandToInclude(xf->position);
    });
    if (m_chaseOk)
        m_chase.expandBounds(sceneBounds);
    for (int i = 0; i < m_healthPackCount; ++i)
    {
        if (m_healthPacks[i].active)
            sceneBounds.ExpandToInclude(m_healthPacks[i].pos);
    }
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
            const Frustum3f casterFrustum(m_shadows.cascade(i).viewProj);
            // Opaque casters only — same set as G-buffer / forward color. Water, particles, blood, lines stay out.
            m_terrain.drawDepth(cmd, &casterFrustum);
            world().each<NetworkedComponent>([&](Entity e, NetworkedComponent&) {
                const TransformComponent* xf = world().get<TransformComponent>(e);
                if (!xf)
                    return;
                drawShadowCaster(cmd, m_shadows, i, makeWorldMatrix(*xf), m_cubeMesh);
            });
            if (m_chaseOk)
                m_chase.drawDepth(cmd, m_shadows, i, m_cubeMesh);
            drawHealthPacksDepth(cmd, i);
        }
        m_shadows.endCapture(cmd);
    }
    else if (m_shadows.isValid())
    {
        m_shadows.endCapture(cmd);
    }

    const bool deferred = renderer().scenePath() == ScenePath::HybridDeferred;
    m_viewCamera.ClearSubpixelJitter();
    const bool useTaa = deferred && renderer().debugState().taa && m_taa.isValid();
    if (useTaa)
    {
        float jx = 0.0f, jy = 0.0f;
        taaHaltonJitter(renderer().frameIndex(), jx, jy);
        m_viewCamera.SetSubpixelJitter(jx, jy, renderer().width(), renderer().height());
    }
    if (deferred)
    {
        renderer().bindGBuffer();
        renderer().clearGBuffer();
    }
    else if (renderer().hasSceneBuffers())
    {
        renderer().bindHdr(true);
        renderer().clearHdr();
    }
    else
    {
        renderer().bindSceneTargets();
    }

    const Frustum3f frustum(m_viewCamera.GetViewProj());
    const DebugFill fill     = renderer().debugState().fill;
    const Vector3f  camPos   = m_viewCamera.GetPosition();
    const Matrix4f  viewProj = m_viewCamera.GetViewProj();
    const Matrix4f  prevViewProj = m_havePrevViewProj ? m_prevViewProj : viewProj;
    uint32_t        meshDraws = 0;

    const float skyExposure = useAcesTonemap(renderer()) ? 1.0f : m_env.exposure();
    if (!deferred)
        m_skyPipeline.draw(cmd, m_viewCamera, m_env, skyExposure);

    AssetRef<Material> material = m_cubeMaterial;
    if (m_cube.valid())
    {
        if (auto* meshComp = world().get<MeshComponent>(m_cube))
            material = assets().getAs<Material>(meshComp->matAssetID);
    }
    if (!material)
        material = m_cubeMaterial;

    if (deferred)
    {
        m_terrain.drawGBuffer(cmd, m_terrainPipeline, m_terrainMaterial, m_viewCamera, &frustum, &renderer().debugState(), &prevViewProj);
        m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
        MeshGBufferConstants gcb{};
        if (material)
            material->applySurface(gcb.color);
        else
        {
            gcb.color[0] = 1.0f;
            gcb.color[1] = 1.0f;
            gcb.color[2] = 1.0f;
            gcb.color[3] = 1.0f;
        }
        world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            const Matrix4f prevWorld = m_prevWorldByEntity.count(e.id()) ? m_prevWorldByEntity[e.id()] : worldMat;
            fillMeshGBufferXforms(gcb, worldMat, viewProj, prevViewProj, prevWorld);
            unpackRgba8(nc.colorRgba8, gcb.color);
            m_meshPipeline.setGBufferConstants(cmd, gcb);
            m_cubeMesh.draw(cmd, fill == DebugFill::Points);
            m_prevWorldByEntity[e.id()] = worldMat;
            ++meshDraws;
        });
        if (m_chaseOk)
            m_chase.drawMeshesGBuffer(cmd, m_meshPipeline, m_viewCamera, prevViewProj, m_cubeMesh, fill);
        drawHealthPacksGBuffer(cmd, viewProj, prevViewProj);

        renderer().bindHdr(false);
        renderer().clearHdr();
        LightingConstants lc{};
        copyMatrix(lc.invViewProj, viewProj.Inverse());
        lc.cameraPos[0]     = camPos.x;
        lc.cameraPos[1]     = camPos.y;
        lc.cameraPos[2]     = camPos.z;
        lc.fogDensity       = m_env.fogDensity();
        lc.lightDirWS[0]    = m_env.lightDir().x;
        lc.lightDirWS[1]    = m_env.lightDir().y;
        lc.lightDirWS[2]    = m_env.lightDir().z;
        lc.lighting         = renderer().debugState().lighting ? 1.0f : 0.0f;
        lc.lightColor[0]    = m_env.lightColor().x;
        lc.lightColor[1]    = m_env.lightColor().y;
        lc.lightColor[2]    = m_env.lightColor().z;
        lc.ambientColor[0]  = m_env.ambientColor().x;
        lc.ambientColor[1]  = m_env.ambientColor().y;
        lc.ambientColor[2]  = m_env.ambientColor().z;
        lc.fogColor[0]      = m_env.fogColor().x;
        lc.fogColor[1]      = m_env.fogColor().y;
        lc.fogColor[2]      = m_env.fogColor().z;
        m_lighting.draw(cmd, renderer(), m_shadows, lc);

        renderer().bindHdr(true);
        m_skyPipeline.draw(cmd, m_viewCamera, m_env, skyExposure);
    }
    else
    {
        m_terrain.draw(
            cmd, m_terrainPipeline, m_terrainMaterial, m_viewCamera, &frustum, &m_env, &m_shadows,
            &renderer().debugState());

        m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
        m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);

        MeshFrameConstants cb{};
        if (material)
            material->applySurface(cb);
        else
        {
            cb.color[0] = 1.0f;
            cb.color[1] = 1.0f;
            cb.color[2] = 1.0f;
            cb.color[3] = 1.0f;
        }
        cb.lightDirWS[0] = m_env.lightDir().x;
        cb.lightDirWS[1] = m_env.lightDir().y;
        cb.lightDirWS[2] = m_env.lightDir().z;
        cb.ambientScale  = 0.22f;
        cb.lightColor[0] = m_env.lightColor().x;
        cb.lightColor[1] = m_env.lightColor().y;
        cb.lightColor[2] = m_env.lightColor().z;
        cb.cameraPos[0]  = camPos.x;
        cb.cameraPos[1]  = camPos.y;
        cb.cameraPos[2]  = camPos.z;
        cb.lighting      = renderer().debugState().lighting ? 1.0f : 0.0f;

        world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            copyMatrix(cb.worldViewProj, worldMat * viewProj);
            copyMatrix(cb.world, worldMat);
            unpackRgba8(nc.colorRgba8, cb.color);
            m_meshPipeline.setConstants(cmd, cb);
            m_cubeMesh.draw(cmd, fill == DebugFill::Points);
            ++meshDraws;
        });

        if (m_chaseOk)
            m_chase.drawMeshes(cmd, m_meshPipeline, m_shadows, m_viewCamera, cb, m_cubeMesh, fill);
        drawHealthPacks(cmd, viewProj, cb);
    }

    m_water.draw(cmd, m_waterPipeline, m_viewCamera, &frustum, &m_env, &renderer().debugState());

    if (m_chaseOk)
        m_chase.drawPaths(cmd, renderer(), viewProj);

    if (m_blood.aliveCount() > 0)
        m_particles.draw(cmd, m_viewCamera, m_blood, false);

    m_bloodSplats.draw(cmd, m_viewCamera);

    bool usedPostHdr = false;
    if (useTaa)
    {
        if (renderer().width() != m_taaHistoryW || renderer().height() != m_taaHistoryH)
        {
            m_taaHistoryValid = false;
            m_taaHistoryW     = renderer().width();
            m_taaHistoryH     = renderer().height();
        }
        TaaSettings taa{};
        copyMatrix(taa.invViewProj, viewProj.Inverse());
        copyMatrix(taa.prevViewProj, prevViewProj);
        taa.blend = 0.1f;
        taa.reset = !m_taaHistoryValid;
        m_taa.draw(cmd, renderer(), taa);
        m_taaHistoryValid = true;
        usedPostHdr       = true;
    }
    const bool useMb = deferred && renderer().debugState().motionBlur && m_motionBlur.isValid();
    if (useMb)
    {
        MotionBlurSettings mb{};
        copyMatrix(mb.invViewProj, viewProj.Inverse());
        copyMatrix(mb.prevViewProj, prevViewProj);
        mb.strength  = 1.0f;
        mb.maxPixels = 40.0f;
        mb.readPost  = useTaa;
        m_motionBlur.draw(cmd, renderer(), mb);
        usedPostHdr = !useTaa;
    }

    if (renderer().hasSceneBuffers())
    {
        renderer().bindColorTargetOnly();
        const bool aces = useAcesTonemap(renderer());
        TonemapSettings post = playerPostFx();
        post.mode       = aces ? 1.0f : 0.0f;
        post.exposure   = aces ? m_env.exposure() : 1.0f;
        post.usePostHdr = usedPostHdr;
        m_tonemap.draw(cmd, renderer(), post);
    }

    m_prevViewProj       = viewProj;
    m_havePrevViewProj   = true;
    m_viewCamera.ClearSubpixelJitter();

    m_healthHud.draw(cmd, renderer().width(), renderer().height(), m_playerHealth.ratio());

    renderer().stats().drawCalls = m_terrain.lastDrawCalls() + m_water.lastDrawCalls() + meshDraws + 1;
    renderer().stats().triangles =
        m_terrain.lastTriangles() + m_water.lastTriangles() + meshDraws * (m_cubeMesh.indexCount() / 3);

    drawDebugOverlays(cmd);
    if (m_imgui.isReady())
    {
        if (m_gameplayPaused)
            drawPauseOverlay();
        if (m_showDevTools)
            drawDevTools();
        m_imgui.render(renderer());
    }
    renderer().endFrame();
}

void SandboxApp::drawDebugOverlays(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_debugOverlay.isValid())
        return;
    if (!m_showShadowMaps && !m_showDepth && !m_showGBuffer && !m_showVelocity)
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

    if (m_showGBuffer && renderer().hasGBuffer())
    {
        const LONG y = pad;
        const D3D12_CPU_DESCRIPTOR_HANDLE albedo = renderer().albedoSrvCpu();
        const D3D12_CPU_DESCRIPTOR_HANDLE attrib = renderer().attribSrvCpu();
        if (albedo.ptr != 0)
            m_debugOverlay.drawColor(cmd, renderer().device(), albedo, pad, y, tile, tile);
        if (attrib.ptr != 0)
            m_debugOverlay.drawColor(cmd, renderer().device(), attrib, pad + tile + 8, y, tile, tile);
    }
    if ((m_showGBuffer || m_showVelocity) && renderer().hasGBuffer())
    {
        renderer().transitionVelocity(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const D3D12_CPU_DESCRIPTOR_HANDLE velocity = renderer().velocitySrvCpu();
        if (velocity.ptr != 0)
        {
            LONG x = pad;
            if (m_showGBuffer)
                x = pad + 2 * (tile + 8);
            m_debugOverlay.drawVelocity(cmd, renderer().device(), velocity, x, pad, tile, tile, 24.0f);
        }
    }

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
    network().shutdown();
    renderer().waitForGpu();
    m_imgui.shutdown(renderer());
    if (m_cubeMaterial)
        assets().unload(m_cubeMaterial->id);
    m_cubeMaterial.reset();
    m_water = Water::WaterWorld{};
    m_terrainMaterial = TerrainMaterial{};
    m_terrain = Terrain::TerrainWorld{};
    m_shadows = ShadowSystem{};
    m_debugOverlay = DebugOverlay{};
    audio().stopAll();
    m_sfxReset.reset();
    m_sfxClick.reset();
    m_sfxStep.reset();
    m_sfxWater.reset();
    m_sfxGrunt.reset();
    m_sfxLand.reset();
    m_sfxSplash.reset();
    m_sfxPain.reset();
    m_sfxHeal.reset();
    m_particles.destroy(renderer());
    m_bloodSplats.destroy(renderer());
    m_music.reset();
    DE_LOG_INFO("SandboxApp: shutdown");
}
