#include "SandboxApp.h"

#include "ECS/Components.h"
#include "Water/WaterStream.h"

#include <format>
#include <memory>
#include "Core/ContentRoots.h"
#include "Core/EntityPins.h"
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
#include "Render/DebugRenderState.h"
#include "Render/DecalBasis.h"
#include "Render/Frustum3f.h"
#include "Render/TaaJitter.h"
#include "Render/MeshGen.h"
#include "Render/ScenePath.h"
#include "Render/Fog.h"
#include "Render/PbrLighting.h"
#include "Render/ModelDraw.h"
#include "Render/Profile.h"
#include "Render/MaterialSurface.h"
#include "Render/GpuResourceCache.h"
#include "Render/GpuUpload.h"
#include "Save/PersistentId.h"
#include "Save/ProgressComponents.h"
#include "Assets/Image.h"
#include "Terrain/FoliageSpawn.h"
#include "Terrain/WorldEngineMap.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/Locomotion.h"
#include "AI/AiComponents.h"
#include "AI/Brain.h"
#include "AI/HsmGraph.h"
#include "Scene/EntityMaster.h"
#include "Physics/PhysicsBind.h"
#include "Physics/PhysicsComponent.h"
#include "Physics/PhysicsFile.h"
#include "AI/HsmGraphComponent.h"
#include "Audio/SoundComponents.h"
#include "Animation/AnimNotify.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/ArmorView.h"
#include "Character/ShieldView.h"
#include "Character/SkillSense.h"
#include "Character/SkillXp.h"
#include "Combat/CombatSystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/Shield.h"
#include "Combat/DamageEvent.h"
#include "Combat/PlayerMode.h"
#include "Combat/JumpAttackComponent.h"
#include "Combat/JumpAttackDef.h"
#include "Combat/JumpAttackResolve.h"
#include "Combat/PoiseComponent.h"
#include "Combat/StatusDot.h"
#include "Combat/StatusEffectComponent.h"
#include "Particles/ParticleComponents.h"
#include "Particles/ParticleTick.h"
#include "Particles/StatusFxDriver.h"
#include "Ui/HudTagComponent.h"
#include "Weapons/HittableComponent.h"
#include "Animation/SkeletonDebug.h"
#include "Render/LinePipeline.h"
#include "Scene/SceneFile.h"
#include "Sky/CloudVolume.h"
#include "Terrain/SplatMap.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainTileFile.h"
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

// SceneRenderer owns shared pipelines; keep existing call sites stable.
#define m_meshPipeline m_scene.meshPipeline()
#define m_meshTransparentPipeline m_scene.meshTransparentPipeline()
#define m_skinnedPipeline m_scene.skinnedPipeline()
#define m_skinnedTransparentPipeline m_scene.skinnedTransparentPipeline()
#define m_skinnedShadowPipeline m_scene.skinnedShadowPipeline()
#define m_skinRing m_scene.skinRing()
#define m_tonemap m_scene.tonemap()
#define m_lighting m_scene.lighting()
#define m_localLightVolumes m_scene.localLightVolumes()
#define m_localLightGpu m_scene.localLightGpu()
#define m_pointVolumeMesh m_scene.pointVolumeMesh()
#define m_spotVolumeMesh m_scene.spotVolumeMesh()
#define m_bloom m_scene.bloom()
#define m_motionBlur m_scene.motionBlur()
#define m_taa m_scene.taa()
#define m_shadows m_scene.shadows()
#define m_debugOverlay m_scene.debugOverlay()
#define m_terrainPipeline m_scene.terrainPipeline()
#define m_waterPipeline m_scene.waterPipeline()
#define m_skyPipeline m_scene.skyPipeline()


bool useAcesTonemap(const Renderer& r)
{
    return r.hasSceneBuffers() && r.debugState().aces && r.debugState().lightingActive();
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

AssetRef<Image> loadTerrainLayerMap(AssetManager& assets, const std::string& virtualPath, Dark::Color::ColorSpace space)
{
    AssetRef<Image> img = assets.loadImage(virtualPath);
    if (!img || !img->valid())
        return {};
    img->setColorSpace(space);
    return img;
}

bool tryCreateTerrainFromContent(Renderer& renderer, AssetManager& assets, const Terrain::SplatMap& splat, TerrainMaterial& material)
{
    static constexpr const char* kLayerNames[Terrain::kMaxTerrainLayers] = { "dirt", "grass", "rock", "snow" };
    static constexpr float       kTiling[Terrain::kMaxTerrainLayers]     = { 24.0f, 20.0f, 16.0f, 12.0f };

    Terrain::TerrainSurfaceDesc desc{};
    for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
    {
        desc.layers[i].tiling      = kTiling[i];
        const std::string prefix   = std::string("terrain/") + kLayerNames[i] + "/";
        desc.albedo[i]             = loadTerrainLayerMap(assets, prefix + "albedo.png", Dark::Color::ColorSpace::sRGB);
        desc.normal[i]             = loadTerrainLayerMap(assets, prefix + "normal.png", Dark::Color::ColorSpace::Linear);
        desc.orm[i]                = loadTerrainLayerMap(assets, prefix + "orm.png", Dark::Color::ColorSpace::Linear);
        if (!desc.albedo[i] || !desc.albedo[i]->valid())
            return false;
    }

    Image     splatImg;
    Texture2D splatTex;
    if (!splatImg.createFromRGBA(splat.rgba(), splat.width(), splat.height(), splat.width() * 4u)
        || !splatTex.createFromImage(renderer, splatImg, Dark::Color::TextureUsage::Data))
    {
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: splat upload failed");
        return false;
    }
    return material.create(renderer, desc, std::move(splatTex));
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

AssetID loadSandboxClip(Audio::AudioSystem& audio, AssetManager& assets, const char* path, float freq, float dur, float amp)
{
    const auto clip = audio.loadOrBlip(assets, path, freq, dur, amp);
    return (clip && clip->id != NULL_ASSET) ? clip->id : NULL_ASSET;
}

void attachCameraSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity camera)
{
    SoundBankComponent bank;
    addSoundCue(bank, "click", loadSandboxClip(audio, assets, "audio/ui_click.wav", 1400.0f, 0.06f, 0.35f), 0.5f, false);
    addSoundCue(bank, "reset", loadSandboxClip(audio, assets, "audio/whoosh.wav", 180.0f, 0.22f, 0.35f), 0.7f, false);
    auto music = audio.loadWav(assets, "audio/ambient_loop.wav");
    if (!music)
        music = audio.createTone(assets, 110.0f, 2.0f, 0.12f);
    if (music && music->id != NULL_ASSET)
        addSoundCue(bank, "music", music->id, 0.10f, false);
    setSoundBank(world, pins, assets, camera, std::move(bank));
}

void addGroundFootsteps(SoundBankComponent& bank, Audio::AudioSystem& audio, AssetManager& assets, const Terrain::TerrainGround& ground)
{
    for (int i = 0; i < ground.layerCount(); ++i)
    {
        const Terrain::GroundContact& layer = ground.layer(i);
        if (!layer.cue || !layer.cue[0])
            continue;
        addSoundCue(bank, layer.cue, loadSandboxClip(audio, assets, layer.footstep ? layer.footstep : "audio/foot_dirt.wav", layer.blipHz, 0.05f, 0.4f), 0.4f, true);
    }
}

void attachPlayerSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
{
    SoundBankComponent bank;
    addGroundFootsteps(bank, audio, assets, ground);
    addSoundCue(bank, "step", loadSandboxClip(audio, assets, "audio/foot_grass.wav", 160.0f, 0.05f, 0.4f), 0.35f, true);
    addSoundCue(bank, "jump", loadSandboxClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.7f, false);
    addSoundCue(bank, "land", loadSandboxClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, false);
    addSoundCue(bank, "splash", loadSandboxClip(audio, assets, "audio/splash.wav", 220.0f, 0.22f, 0.45f), 0.8f, false);
    addSoundCue(bank, "swim_step", loadSandboxClip(audio, assets, "audio/splash.wav", 220.0f, 0.22f, 0.45f), 0.42f, false);
    addSoundCue(bank, "pain", loadSandboxClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
    addSoundCue(bank, "heal", loadSandboxClip(audio, assets, "audio/coin.wav", 880.0f, 0.16f, 0.4f), 0.7f, false);
    addSoundCue(bank, "fire", loadSandboxClip(audio, assets, "audio/whoosh.wav", 520.0f, 0.12f, 0.45f), 0.45f, true);
    addSoundCue(bank, "impact", loadSandboxClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f), 0.5f, true);
    addSoundCue(bank, "death", loadSandboxClip(audio, assets, "audio/whoosh.wav", 180.0f, 0.22f, 0.35f), 0.55f, false);
    setSoundBank(world, pins, assets, e, std::move(bank));

    const AssetID waterId = loadSandboxClip(audio, assets, "audio/whoosh.wav", 70.0f, 0.8f, 0.25f);
    if (waterId != NULL_ASSET && !world.has<SoundEmitterComponent>(e))
    {
        SoundEmitterComponent se{};
        se.clipId  = waterId;
        se.volume  = 0.28f;
        se.looping = true;
        se.spatial = false;
        se.play    = false;
        world.emplace<SoundEmitterComponent>(e, se);
        pinSoundEmitter(pins, assets, se);
    }

    if (WeaponLoadoutComponent* wlc = world.get<WeaponLoadoutComponent>(e); wlc && wlc->loadout)
    {
        AssetID fireId   = NULL_ASSET;
        AssetID impactId = NULL_ASSET;
        if (const SoundBankComponent* cues = world.get<SoundBankComponent>(e))
        {
            if (const SoundCueDesc* c = findSoundCue(*cues, "fire"))
                fireId = c->clipId;
            if (const SoundCueDesc* c = findSoundCue(*cues, "impact"))
                impactId = c->clipId;
        }
        wlc->loadout->projectile().setAudio(&audio, &assets, fireId, impactId);
    }
}

void attachHunterSounds(World& world, AssetPinTable& pins, AssetManager& assets, Audio::AudioSystem& audio, Entity e, const Terrain::TerrainGround& ground)
{
    SoundBankComponent bank;
    addGroundFootsteps(bank, audio, assets, ground);
    addSoundCue(bank, "pain", loadSandboxClip(audio, assets, "audio/pain.wav", 380.0f, 0.12f, 0.5f), 0.75f, true);
    addSoundCue(bank, "grunt", loadSandboxClip(audio, assets, "audio/grunt.wav", 140.0f, 0.18f, 0.5f), 0.95f, true);
    addSoundCue(bank, "growl", loadSandboxClip(audio, assets, "audio/growl.wav", 90.0f, 0.55f, 0.5f), 0.9f, true);
    const AssetID impactClip = loadSandboxClip(audio, assets, "audio/place.wav", 180.0f, 0.10f, 0.5f);
    addSoundCue(bank, "impact", impactClip, 0.5f, true);
    // Catalog Poison/Ignite applyCue is "fire"; hunters have no weapon fire row — alias impact so one-shots aren't silent.
    addSoundCue(bank, "fire", impactClip, 0.5f, true);
    addSoundCue(bank, "land", loadSandboxClip(audio, assets, "audio/land.wav", 70.0f, 0.12f, 0.55f), 0.75f, true);
    setSoundBank(world, pins, assets, e, std::move(bank));
}

bool playSoundCueFallback(World& world, Audio::AudioSystem& audio, AssetManager& assets, Entity e, const char* primary, const char* fallback)
{
    if (playSoundCue(world, audio, assets, e, primary))
        return true;
    return fallback && playSoundCue(world, audio, assets, e, fallback);
}

void tryJumpAttackAnim(World& world, Entity e)
{
    AnimGraphComponent* ag = e.valid() ? world.get<AnimGraphComponent>(e) : nullptr;
    if (ag)
        ag->graph.setTrigger("jump_attack");
}

void drawShadowCaster(ID3D12GraphicsCommandList* cmd, const ShadowPipeline& pipeline, const Matrix4f& lightViewProj, const Matrix4f& world, const Mesh& mesh)
{
    const Matrix4f wvp = world * lightViewProj;
    pipeline.setWvp(cmd, wvp.m_afEntry);
    mesh.draw(cmd);
}

void drawShadowCaster(ID3D12GraphicsCommandList* cmd, const ShadowSystem& shadows, int cascade, const Matrix4f& world, const Mesh& mesh)
{
    drawShadowCaster(cmd, shadows.pipeline(), shadows.cascade(cascade).viewProj, world, mesh);
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

const Mesh* sandboxPrimitiveMesh(PrimitiveMesh p, const Mesh& cube, const Mesh& cross)
{
    switch (p)
    {
    case PrimitiveMesh::Cube:
        return cube.valid() ? &cube : nullptr;
    case PrimitiveMesh::Cross:
        return cross.valid() ? &cross : nullptr;
    case PrimitiveMesh::Sphere:
    case PrimitiveMesh::None:
    default:
        return nullptr;
    }
}

void attachSandboxCubeModel(World& world, AssetPinTable& pins, AssetManager& assets, Entity e, AssetID modelId)
{
    ModelComponent mc{};
    mc.modelAssetID = modelId;
    mc.castShadow   = true;
    setModelComponent(world, pins, assets, e, mc);
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
    a.bindKey("weapon_1", Key::Digit1);
    a.bindKey("weapon_2", Key::Digit2);
    a.bindKey("flashlight", Key::L);
    a.bindKey("shield", Key::V);
    a.bindKey("toggle_lighting", Key::F2);

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
    a.bindKeyAsAxis("pitch", Key::X, -1.0f);
    a.bindKey("camouflage", Key::C);

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
    a.bindKey("crouch", Key::LeftControl);
    a.bindKey("crouch", Key::RightControl);

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
    a.bindKey("anim_walk", Key::T);
    a.bindButton("anim_walk", GamepadButton::RightThumb);

    DE_LOG_INFO(
        "Input: quit(Esc/Back) pause(P/Start) freeze gameplay + fly cam  step(O)  reset(R/Y) speed(+/- / RB) "
        "possessed WASD/arrows/LS move, double-tap a direction to dodge, mouse+RS look, Space/A jump (tap again quickly for a higher jump), LMB/F/B attack, 1 melee  2 rifle, "
        "hold attack to charge, release to swing (tap = normal). Hold RMB/V/LT shield; hold longer then release to parry-slam. Ctrl crouch, C camouflage, L flashlight, Shift/LB sprint, swim in water, "
        "T/R3 walk the wiggle demo  F2 lighting  M dev tools  -forward linear albedo + sRGB encode (legacyUnormAlbedo restores old sampling)  -no-menu skip scene picker");
}

void SandboxApp::populateMainMenu()
{
    m_menu.clearEntries();
    m_menu.setTitle(config().hostName ? config().hostName : "Sandbox");
    m_menu.setAccent(UiAccent::Sandbox);
    m_menu.addBuiltIn("sandbox", "Play", "Default sandbox");
    m_menu.addQuit();
}

void SandboxApp::handleRuntimeCommands(float dt)
{
    if (m_menu.visible())
    {
        window().setCursorCaptured(false);
        m_menu.update(input(), renderer().width(), renderer().height());
        switch (m_menu.pollResult())
        {
        case MainMenuResult::Confirm:
            m_menu.hide();
            break;
        case MainMenuResult::Quit:
            DE_LOG_INFO("Command: quit");
            requestQuit();
            return;
        default:
            break;
        }
        return;
    }

    handleNetHotkeys();
    applyNetRole();

    const bool uiKeys = m_showDevTools && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    if (!uiKeys)
        handleWeaponSwitch();
    if (!uiKeys && input().actionPressed("dev_tools"))
    {
        m_showDevTools = !m_showDevTools;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
        DE_LOG_INFO("Sandbox: dev tools = {}", m_showDevTools);
    }

    if (!uiKeys && input().actionPressed("camouflage"))
    {
        m_camouflage = !m_camouflage;
        DE_LOG_INFO("Sandbox: camouflage = {}", m_camouflage);
    }
    if (!uiKeys && input().actionPressed("toggle_lighting"))
    {
        DebugRenderState& dbg = renderer().debugState();
        dbg.lighting          = !dbg.lighting;
        DE_LOG_INFO("Sandbox: lighting = {}", dbg.lighting);
    }

    if (input().actionPressed("quit"))
    {
        if (config().showMainMenu && m_menu.isReady() && network().role() == NetRole::Idle)
        {
            m_menu.show();
            window().setCursorCaptured(false);
            return;
        }
        DE_LOG_INFO("Command: quit");
        requestQuit();
        return;
    }

    if (!uiKeys && input().keyPressed(Key::F5))
    {
        const Save::SaveResult result = m_save.requestSave(Save::SaveKind::Quick, "Quick Save");
        DE_LOG_INFO("Sandbox: quicksave {}", Save::toString(result));
    }
    if (!uiKeys && input().keyPressed(Key::F9))
    {
        const Save::SaveResult result = m_save.requestLoadNewest(Save::SaveKind::Quick);
        DE_LOG_INFO("Sandbox: quickload {}", Save::toString(result));
    }

    if (!uiKeys && input().actionPressed("pause"))
    {
        m_gameplayPaused = !m_gameplayPaused;
        m_stepGameplay   = false;
        playSoundCue(world(), audio(), assets(), m_camera, "click");
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
        playSoundCueAt(world(), audio(), assets(), m_camera, "reset", pos);
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

    window().setCursorCaptured(window().isFocused() && !m_showDevTools && !m_menu.visible());
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
        const float speed   = (sprint ? 2.5f : 1.0f) * m_flySpeed;

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

bool SandboxApp::camouflageHides(Entity e)
{
    if (!m_camouflage || !e.valid())
        return false;
    const Entity body = possessedBody();
    return body.valid() && e.id() == body.id();
}

void SandboxApp::attachReplicaCombat(Entity e)
{
    if (!e.valid() || !world().alive(e))
        return;
    if (!world().has<HealthComponent>(e))
    {
        HealthSettings playerHp;
        playerHp.maxHp       = 100.0f;
        playerHp.regenPerSec = 10.0f;
        playerHp.regenDelay  = 3.5f;
        HealthComponent hc{};
        hc.health = Health{ playerHp };
        world().emplace<HealthComponent>(e, std::move(hc));
    }
    if (!world().has<HitReactionComponent>(e))
    {
        HitReactionSettings hit{};
        hit.stunSeconds       = 0.28f;
        hit.knockbackDistance = 1.1f;
        hit.knockbackSeconds  = 0.14f;
        hit.horizontalOnly    = true;
        HitReactionComponent hr{};
        hr.hit.setSettings(hit);
        world().emplace<HitReactionComponent>(e, std::move(hr));
    }
    if (!world().has<HittableComponent>(e))
    {
        HittableComponent h{};
        h.halfExtents = Vector3f{ 0.35f, 0.7f, 0.35f };
        world().emplace<HittableComponent>(e, h);
    }
    const EntityMaster playerMaster = loadEntityMasterType("player");
    const std::string  playerGltf = playerMaster.gltf.empty() ? std::string("models/human.gltf") : playerMaster.gltf;
    attachAnimatedCharacter(e, playerGltf.c_str());
}

void SandboxApp::attachLocalPlayer(Entity e)
{
    attachReplicaCombat(e);
    if (!world().has<PlayerMotorComponent>(e))
        world().emplace<PlayerMotorComponent>(e);
    if (!world().has<HsmGraphComponent>(e))
    {
        HsmGraphComponent hsm{};
        const EntityMaster playerMaster = loadEntityMasterType("player");
        const std::string  hsmPath = playerMaster.hsm.empty() ? std::string("ai/player.hsm.json") : playerMaster.hsm;
        hsm.def      = assets().tryLoadHsmGraph(hsmPath);
        hsm.instance = std::make_unique<HsmGraphInstance>();
        const bool built = hsm.def ? hsm.instance->build(*hsm.def) : hsm.instance->build(makePlayerHsmGraph());
        if (!built)
            DE_LOG_WARN(LogCategory::AI, "SandboxApp: player HSM build failed");
        else
        {
            hsm.instance->start();
            world().emplace<HsmGraphComponent>(e, std::move(hsm));
        }
    }
    if (!world().has<HudTagComponent>(e))
    {
        HudTagComponent hud{};
        hud.kind = HudKind::HealthBar;
        world().emplace<HudTagComponent>(e, hud);
    }
    if (!world().has<WeaponLoadoutComponent>(e))
    {
        auto& wlc   = world().emplace<WeaponLoadoutComponent>(e);
        wlc.loadout = std::make_unique<WeaponLoadout>();
        wlc.loadout->setHitListener(&SandboxApp::onWeaponHitThunk, this);
        wlc.slot = wlc.loadout->slot();
    }
    if (!world().has<JumpAttackComponent>(e))
    {
        auto& jac = world().emplace<JumpAttackComponent>(e);
        Combat::JumpAttackDef def = jac.jump.def();
        def.telegraphSeconds = 0.0f;
        def.cooldown         = 1.25f;
        def.connectFlags     = Combat::DamageFlags::CanBlock | Combat::DamageFlags::HardCc | Combat::DamageFlags::Knockdown;
        jac.jump.setDef(def);
    }
    if (!world().has<Combat::StatusEffectComponent>(e))
        world().emplace<Combat::StatusEffectComponent>(e);
    if (!world().has<Combat::PoiseComponent>(e))
        world().emplace<Combat::PoiseComponent>(e);
    Combat::equipPlayerShield(world(), e);
    attachPlayerSounds(world(), pins(), assets(), audio(), e, m_ground);
    applySkillProfile(world(), e, "player");
    ensurePlayerProgress(e);
}

bool SandboxApp::attachAnimatedCharacter(Entity e, const char* gltfPath)
{
    if (!e.valid() || !world().alive(e) || !gltfPath || gltfPath[0] == '\0')
        return false;
    if (world().has<AnimGraphComponent>(e) && world().has<ModelComponent>(e))
        return true;

    AssetRef<Model> model = loadAndUploadModel(renderer(), assets(), gltfPath);
    if (!model || !model->valid())
    {
        DE_LOG_WARN("SandboxApp: animated character '{}' failed to load", gltfPath);
        return false;
    }
    if (!model->skeleton())
    {
        DE_LOG_WARN("SandboxApp: '{}' has no skeleton", gltfPath);
        return false;
    }

    AssetRef<AnimGraphDef> graph = assets().tryLoadAnimGraphForModel(gltfPath);
    if (!graph)
        DE_LOG_WARN("SandboxApp: '{}' has no anim graph sidecar", gltfPath);

    if (const ModelComponent* old = world().get<ModelComponent>(e))
        unpinModelComponent(pins(), *old);
    ModelComponent mc{};
    mc.modelAssetID = model->id;
    mc.castShadow   = model->hasOpaque();
    world().emplace<ModelComponent>(e, mc);
    pinModelComponent(pins(), assets(), mc);

    if (MeshComponent* mesh = world().get<MeshComponent>(e))
    {
        MeshComponent next = *mesh;
        next.primitive     = PrimitiveMesh::None;
        setMeshComponent(world(), pins(), assets(), e, next);
    }

    if (TransformComponent* xf = world().get<TransformComponent>(e))
        xf->scale = Vector3f{ 1.0f, 1.0f, 1.0f };

    AnimGraphComponent ag;
    ag.model    = model;
    ag.animSet  = model->animationSet();
    ag.graphDef = graph;
    if (graph)
        ag.graph.bind(graph.get(), model->skeleton());
    else if (ag.animSet)
    {
        ag.graph.player().bind(model->skeleton(), ag.animSet.get());
        if (!ag.graph.player().play("Idle", 0.0f))
            ag.graph.player().playIndex(0, 0.0f);
    }
    ag.graph.setApplyRootMotion(false);
    world().emplace<AnimGraphComponent>(e, std::move(ag));
    DE_LOG_INFO("SandboxApp: attached '{}' to entity graph={}", gltfPath, graph ? "yes" : "no");
    return true;
}

void SandboxApp::updateCharacterAnims(float dt)
{
    const Entity body = possessedBody();
    if (AnimGraphComponent* ag = body.valid() ? world().get<AnimGraphComponent>(body) : nullptr)
    {
        if (ag->graphDef)
        {
            AimLocomotion aim{};
            const Health* hp = localHealth();
            const TransformComponent* xf = world().get<TransformComponent>(body);
            const bool alive = !(hp && !hp->alive());
            const PlayerMotor* motor = localMotor();
            if (xf && alive && motor)
                aim = aimLocomotion(motor->velocity(), xf->rotation);
            const bool crouched = motor && motor->state() == PlayerMoveState::Crouch;
            if (crouched)
            {
                aim.strafe   = 0.0f;
                aim.backward = false;
            }
            ag->graph.setFloat("speed", aim.speed);
            ag->graph.setFloat("strafe", aim.strafe);
            ag->graph.setBool("backward", aim.backward);
            ag->graph.setBool("crouch", crouched);
            m_lowerBodyYaw = approachAngle(m_lowerBodyYaw, aim.lowerYaw, 10.0f, dt);
            ag->graph.player().setLowerBodyYaw(m_lowerBodyYaw);
            ag->graph.player().setChargeWindup(m_chargeWindup.attack, m_chargeWindup.block, m_chargeWindup.blockStrike);
            const float playScale = (motor && motor->state() == PlayerMoveState::Dodge) ? motor->settings().dodgeAnimSpeed : 1.0f;
            ag->graph.setPlaybackScale(playScale);
        }
    }

    world().each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
        AnimGraphComponent* ag = world().get<AnimGraphComponent>(e);
        if (!ag || !ag->graphDef)
            return;
        const HealthComponent* hp = world().get<HealthComponent>(e);
        const bool alive = hp && hp->health.alive();
        ag->graph.setBool("dead", !alive);

        TransformComponent* xf = world().get<TransformComponent>(e);
        if (alive && xf)
        {
            Vector3f fwd = ai.forward;
            fwd.y        = 0.0f;
            if (fwd.MagnitudeSqrd() > 1.0e-6f)
            {
                fwd.Normalize();
                xf->rotation = Quaternion::FromLookRotation(fwd, Vector3f::Y_AXIS);
            }
        }

        const Entity player = possessedBody();
        const TransformComponent* pxf = player.valid() ? world().get<TransformComponent>(player) : nullptr;
        bool standoff = false;
        if (alive && xf && pxf)
        {
            const float dx = xf->position.x - pxf->position.x;
            const float dz = xf->position.z - pxf->position.z;
            standoff = (dx * dx + dz * dz) <= (2.25f * 2.25f);
        }

        const BrainComponent* brain = world().get<BrainComponent>(e);
        const AI::Leaf leaf = (brain && brain->brain) ? brain->brain->leaf() : AI::Leaf::Wander;
        LocomotionSample loco{};
        if (alive && xf)
            loco = locomotionSample(ai.planarVelocity, xf->rotation);
        ag->graph.setFloat("speed", loco.speed);
        ag->graph.setFloat("strafe", loco.strafe);

        if (alive && standoff && leaf == AI::Leaf::Chase)
        {
            const char* clip = ag->graph.player().clipName();
            if (!clip
                || (std::strcmp(clip, "SwingSword") != 0 && std::strcmp(clip, "Bite") != 0 && std::strcmp(clip, "Die") != 0
                    && std::strcmp(clip, "Jump") != 0))
                ag->graph.setTrigger("swing");
        }
    });
}

Health* SandboxApp::localHealth()
{
    const Entity body = possessedBody();
    HealthComponent* hc = body.valid() ? world().get<HealthComponent>(body) : nullptr;
    return hc ? &hc->health : nullptr;
}

HitReaction* SandboxApp::localHit()
{
    const Entity body = possessedBody();
    HitReactionComponent* hr = body.valid() ? world().get<HitReactionComponent>(body) : nullptr;
    return hr ? &hr->hit : nullptr;
}

PlayerMotor* SandboxApp::localMotor()
{
    const Entity body = possessedBody();
    PlayerMotorComponent* pm = body.valid() ? world().get<PlayerMotorComponent>(body) : nullptr;
    return pm ? &pm->motor : nullptr;
}

WeaponLoadout* SandboxApp::localWeapons()
{
    const Entity body = possessedBody();
    WeaponLoadoutComponent* wlc = body.valid() ? world().get<WeaponLoadoutComponent>(body) : nullptr;
    return (wlc && wlc->loadout) ? wlc->loadout.get() : nullptr;
}

void SandboxApp::updatePossessed(float dt)
{
    const Entity body = possessedBody();
    SkillComponent* skills = body.valid() ? world().get<SkillComponent>(body) : nullptr;
    if (skills)
        tickSkill(*skills, dt);
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

    Health*      hp    = localHealth();
    HitReaction* hitRx = localHit();
    PlayerMotor* motor = localMotor();
    JumpAttackComponent* jac = body.valid() ? world().get<JumpAttackComponent>(body) : nullptr;
    Combat::JumpAttack*  jump = jac ? &jac->jump : nullptr;
    Combat::StatusEffectComponent* status = body.valid() ? world().get<Combat::StatusEffectComponent>(body) : nullptr;
    Combat::PoiseComponent*        poise  = body.valid() ? world().get<Combat::PoiseComponent>(body) : nullptr;
    if (jump && (!hp || !hp->alive()))
        jump->cancel(Combat::JumpAttackCancel::NoPound);
    const bool ccLocked   = (hitRx && hitRx->stunned()) || (status && status->hasHardCc());
    const bool jumpBusy   = jump && jump->busy();
    const bool inAirCommit = jump && jump->inAirCommit();
    const bool canSteer   = hp && hp->alive() && !ccLocked && !(jump && jump->phase() == Combat::JumpAttackPhase::Pound);
    const bool uiMouse = m_showDevTools && m_imgui.isReady() && m_imgui.wantCaptureMouse();
    const bool holdShield = canSteer
        && ((input().mouseDown(MouseButton::Right) && !uiMouse) || (!uiKeys && input().actionDown("shield"))
            || input().axis(GamepadAxis::LeftTrigger) > 0.45f);
    const bool toggleLight = canSteer && !uiKeys && input().actionPressed("flashlight");
    if (toggleLight)
    {
        if (SkillComponent* sk = world().get<SkillComponent>(body))
            sk->seeArmed = true;
    }
    const bool attackDown = canSteer && !uiKeys
        && (input().actionDown("attack") || (!m_showDevTools && input().mouseDown(MouseButton::Left)));
    const bool airborneNow = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
    Combat::DefenseComponent* defense = world().get<Combat::DefenseComponent>(body);
    if (defense)
        defense->tick(dt);
    Combat::PlayerChargeInput chargeIn{};
    chargeIn.dt              = dt;
    chargeIn.attackDown      = attackDown;
    chargeIn.canChargeAttack = canSteer && !airborneNow && !jumpBusy;
    chargeIn.holdShield      = holdShield;
    chargeIn.canHoldShield   = canSteer;
    chargeIn.chargedParryUp  = defense && defense->chargedParry && defense->inParryWindow();
    Combat::PlayerChargeStep charge{};
    Combat::stepPlayerCharge(m_attackCharge, m_blockCharge, m_chargeSettings, chargeIn, charge);
    m_fireQuick   = charge.fireQuick;
    m_fireCharged = charge.fireCharged;
    const bool wasBlocking = m_offhand.shield.blocking();
    m_offhand.tick(dt, charge.wantShield, toggleLight);
    if (defense)
    {
        if (charge.openChargedParry)
            Combat::openShieldParry(*defense, true, m_chargeSettings);
        else if (!wasBlocking && m_offhand.shield.blocking() && !m_blockCharge.isCharged())
            Combat::openShieldParry(*defense, false, m_chargeSettings);
        Combat::syncShieldDefense(*defense, m_offhand.shield, m_lookYaw);
    }

    PlayerMotorInput motorIn{};
    motorIn.wish            = canSteer ? wish : Vector3f{ 0.0f, 0.0f, 0.0f };
    motorIn.sprint          = canSteer && !jumpBusy && !uiKeys && input().actionDown("sprint");
    if (m_crouchLatch && input().actionPressed("crouch"))
        m_crouchLatch = false;
    motorIn.crouch          = canSteer && !jumpBusy && !uiKeys && (m_crouchLatch || input().actionDown("crouch"));
    motorIn.jumpPressed     = canSteer && !jumpBusy && !uiKeys && input().actionPressed("jump");
    motorIn.allowDoubleJump = !inAirCommit;
    motorIn.allowJumpBuffer = !jumpBusy;
    motorIn.airControlScale = inAirCommit ? jump->def().airControlScale : 1.0f;
    const bool onGround = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Crouch || motor->state() == PlayerMoveState::Dodge);
    const Terrain::GroundContact groundSurf = (onGround && xf) ? groundContactAt(xf->position.x, xf->position.z) : Terrain::GroundContact{};
    motorIn.speedScale      = (status ? status->moveSpeedScale() : 1.0f) * m_offhand.shield.speedScale() * (onGround ? groundSurf.moveSpeed : 1.0f);
    if (skills)
    {
        motorIn.runScale  = skillScalar(SkillId::Run, SkillScalar::RunScale, skills->level(SkillId::Run));
        motorIn.swimScale = skillScalar(SkillId::Swim, SkillScalar::SwimScale, skills->level(SkillId::Swim));
        motorIn.jumpScale = skillScalar(SkillId::Jump, SkillScalar::JumpScale, skills->level(SkillId::Jump));
    }
    motorIn.allowDodge      = canSteer && !jumpBusy && !uiKeys;
    if (motorIn.allowDodge)
    {
        motorIn.dodgeTap = moveCardinalFromEdges(
            input().keyPressed(Key::W) || input().keyPressed(Key::Up),
            input().keyPressed(Key::S) || input().keyPressed(Key::Down),
            input().keyPressed(Key::A) || input().keyPressed(Key::Left),
            input().keyPressed(Key::D) || input().keyPressed(Key::Right));
        motorIn.dodgeTapWish = moveCardinalWish(motorIn.dodgeTap, flat, right);
    }

    m_groundProbeY = xf->position.y;
    m_groundIgnore = m_physics.valid() ? m_physics.bodyOf(body) : Physics::kNullPhysicsBody;

    PlayerGroundQuery ground{};
    ground.user = this;
    ground.waterY = m_water.params().waterLevel;
    ground.heightAt = [](void* user, float x, float z) -> float {
        return static_cast<const SandboxApp*>(user)->playerGroundHeight(x, z);
    };

    const Vector3f before = xf->position;
    PlayerMotorResult motorOut{};
    if (motor)
        motorOut = motor->tick(xf->position, motorIn, dt, ground);
    if (jump && jump->inAirCommit() && motor && !motorOut.landed && !motorOut.splashed)
    {
        Vector3f vel = motor->velocity();
        Vector3f targetPos{};
        const Vector3f* targetPtr = nullptr;
        bool            hasTarget = false;
        if (jump->phase() == Combat::JumpAttackPhase::Connected && jump->connectedTarget().valid())
        {
            if (const TransformComponent* txf = world().get<TransformComponent>(jump->connectedTarget()))
            {
                targetPos = txf->position;
                targetPtr = &targetPos;
                hasTarget = true;
            }
        }
        jump->applyAirSteering(vel, xf->position, flat, targetPtr, hasTarget, dt);
        motor->setHorizontalVelocity(vel.x, vel.z);
        xf->position.x = before.x + vel.x * dt;
        xf->position.z = before.z + vel.z * dt;
    }
    if (jump && jump->phase() == Combat::JumpAttackPhase::Connected && !motorOut.landed && !motorOut.splashed)
    {
        if (const TransformComponent* txf = jump->connectedTarget().valid() ? world().get<TransformComponent>(jump->connectedTarget()) : nullptr)
            jump->applyConnectSnap(xf->position, txf->position, dt);
    }
    Vector3f hitSlide{};
    if (hitRx)
    {
        hitSlide = hitRx->tick(dt);
        xf->position += hitSlide;
    }

    Physics::syncFoliageCollision(m_physics, m_foliageCollision, m_terrain, m_foliage.empty() ? nullptr : &m_foliage, xf->position.x, xf->position.z);

    Vector3f delta{ xf->position.x - before.x, 0.0f, xf->position.z - before.z };
    if (delta.MagnitudeSqrd() > 1.0e-10f)
    {
        Sphere3f ball{ Vector3f{ before.x, xf->position.y, before.z }, kRadius };
        float    bestT = 1.0f;
        for (const AABox3f& cube : m_chase.cubes())
        {
            if (Dark::Collision::Intersects(ball, cube))
                continue;
            const Dark::Collision::SweptHit3D hit = Dark::Collision::SweptIntersects(ball, delta, cube);
            if (hit.hit && hit.t < bestT)
                bestT = hit.t;
        }
        if (m_physics.valid())
        {
            Physics::PhysicsWorld::MoverCast cast;
            cast.origin      = Vector3f{ before.x, xf->position.y, before.z };
            cast.radius      = kRadius;
            cast.bottom      = -kRadius;
            cast.top         = 1.35f;
            cast.translation = delta;
            cast.ignore      = m_groundIgnore;
            cast.ignore2     = m_physicsGround;
            cast.staticOnly  = true;
            const float physicsT = m_physics.clipMover(cast);
            if (physicsT < bestT)
                bestT = physicsT;
        }
        if (bestT < 1.0f)
            delta *= Math::Max(0.0f, bestT - 0.02f);
        xf->position.x = before.x + delta.x;
        xf->position.z = before.z + delta.z;
        if (dt > 1.0e-4f && motor)
            motor->setHorizontalVelocity(delta.x / dt, delta.z / dt);
    }
    if (motor && skills)
    {
        MotorXpSample sample{};
        sample.state        = motor->state();
        sample.sprint       = motorIn.sprint;
        sample.crouch       = motorIn.crouch;
        sample.dodged       = motorOut.dodged;
        sample.jumped       = motorOut.jumped;
        sample.doubleJump   = motorOut.doubleJumped;
        sample.jumpBusy     = jump && jump->busy();
        sample.planarMetres = locomotionMetres(xf->position - before, hitSlide);
        noteMotorXp(*skills, sample);
    }
    if (motor && motor->state() == PlayerMoveState::Grounded)
        xf->position.y = playerGroundHeight(xf->position.x, xf->position.z) + motor->settings().groundOffset;

    m_playerWet = motor && motor->state() == PlayerMoveState::Swimming;

    bool skipLandCue = false;
    if (jump)
    {
        jump->tick(dt);
        if (hp && hp->alive())
        {
            if (motorOut.splashed)
                jump->onSplashed();
            else if (motorOut.landed)
            {
                jump->onLanded(xf->position);
                if (jump->phase() == Combat::JumpAttackPhase::Pound)
                {
                    Combat::DamageEvent poundEvents[8]{};
                    const int n = jump->tryPound(makeWeaponQuery(), xf->position, poundEvents, 8);
                    DE_LOG_INFO("Player: ground pound");
                    // Optional pound cue; otherwise the motor land cue below is the fallback.
                    if (playSoundCue(world(), audio(), assets(), body, "pound"))
                        skipLandCue = true;
                    resolveJumpAttackAndFx(poundEvents, n);
                }
            }
            else if (jump->phase() == Combat::JumpAttackPhase::Leap)
            {
                Combat::DamageEvent connectEv{};
                if (jump->tryConnect(makeWeaponQuery(), xf->position, flat, connectEv))
                {
                    DE_LOG_INFO("Player: pounce connect");
                    playSoundCueFallback(world(), audio(), assets(), body, "pounce", "impact");
                    resolveJumpAttackAndFx(&connectEv, 1);
                }
            }
        }
        if (poise)
            poise->hyperArmor = jump->inAirCommit();
    }

    if (HsmGraphComponent* hsm = world().get<HsmGraphComponent>(body))
    {
        if (hsm->instance)
        {
            const uint8_t motorState = motor ? static_cast<uint8_t>(motor->state()) : 0;
            syncPlayerHsm(*hsm->instance, motorState, hp && hp->alive());
        }
    }

    if (canSteer && flat.MagnitudeSqrd() > 1.0e-6f)
        xf->rotation = Quaternion::FromLookRotation(flat, Vector3f::Y_AXIS);
    const bool blockStriking = defense && defense->chargedParry && defense->inParryWindow();
    stepChargeWindup(m_chargeWindup, dt,
        m_attackCharge.phase() != Combat::HoldChargePhase::Idle, m_attackCharge.heldSeconds(), m_chargeSettings.attackWindowSeconds,
        m_blockCharge.phase() != Combat::HoldChargePhase::Idle, m_blockCharge.heldSeconds(), m_chargeSettings.blockWindowSeconds,
        blockStriking);
    if (TransformComponent* shieldXf = m_shield.valid() ? world().get<TransformComponent>(m_shield) : nullptr)
        placePlayerShield(*shieldXf, *xf, m_offhand.shield.alpha(), m_chargeWindup.block, m_chargeWindup.blockStrike);

    if (motorOut.jumped)
        playSoundCue(world(), audio(), assets(), body, "jump");
    if (motorOut.landed && !skipLandCue)
        playSoundCue(world(), audio(), assets(), body, "land");
    if (motorOut.splashed)
        playSoundCue(world(), audio(), assets(), body, "splash");
    if (motorOut.landed || motorOut.splashed)
        m_footstepAcc = 0.0f;

    const float stepSpeed = Vector3f{ delta.x, 0.0f, delta.z }.Magnitude() / Math::Max(dt, 1.0e-4f);
    const bool  stepping  = motor && (motor->state() == PlayerMoveState::Grounded || motor->state() == PlayerMoveState::Swimming) && stepSpeed > 2.0f;
    if (stepping)
    {
        const float cadence = motor->state() == PlayerMoveState::Swimming ? 0.55f : 0.35f;
        m_footstepAcc += dt * (stepSpeed * cadence);
        if (m_footstepAcc >= 1.0f)
        {
            m_footstepAcc = 0.0f;
            if (motor->state() == PlayerMoveState::Swimming)
                playSoundCue(world(), audio(), assets(), body, "swim_step");
            else
            {
                const Terrain::GroundContact stepGround = groundContactAt(xf->position.x, xf->position.z);
                const char* cue = (stepGround.cue && stepGround.cue[0]) ? stepGround.cue : "step";
                Vector3f facing = flat;
                if (facing.MagnitudeSqrd() <= 1.0e-6f)
                    facing = xf->rotation.Rotate(Vector3f::Z_AXIS);
                spawnFootmark(xf->position, facing);
                if (!playSoundCue(world(), audio(), assets(), body, cue))
                    playSoundCue(world(), audio(), assets(), body, "step");
            }
        }
    }
    else
        m_footstepAcc = 0.0f;

    if (SoundEmitterComponent* se = world().get<SoundEmitterComponent>(body))
        se->play = m_playerWet;
}

void SandboxApp::respawnPlayer()
{
    const Entity body = possessedBody();
    if (TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr)
        xf->position = m_playerSpawn;
    if (PlayerMotor* motor = localMotor())
        motor->reset();
    m_lowerBodyYaw = 0.0f;
    if (AnimGraphComponent* ag = body.valid() ? world().get<AnimGraphComponent>(body) : nullptr)
        ag->graph.player().setLowerBodyYaw(0.0f);
    if (Health* hp = localHealth())
        hp->revive();
    if (HitReaction* hit = localHit())
        hit->reset();
    if (JumpAttackComponent* jac = body.valid() ? world().get<JumpAttackComponent>(body) : nullptr)
        jac->jump.cancel(Combat::JumpAttackCancel::ForceIdle);
    if (Combat::StatusEffectComponent* st = body.valid() ? world().get<Combat::StatusEffectComponent>(body) : nullptr)
        st->reset();
    if (body.valid())
        clearStatusFx(world(), &audio(), body);
    if (Combat::PoiseComponent* poise = body.valid() ? world().get<Combat::PoiseComponent>(body) : nullptr)
    {
        poise->reset();
        poise->hyperArmor = false;
    }
    m_playerWet         = false;
    m_playerDeadTimer   = 0.0f;
    m_spawnAge          = 0.0f;
    m_hurtSoundTimer    = 0.0f;
    m_jumpAttackBuffer  = 0.0f;
    m_offhand.reset();
    if (SkillComponent* sk = body.valid() ? world().get<SkillComponent>(body) : nullptr)
        sk->seeArmed = false;
    m_attackCharge.reset();
    m_blockCharge.reset();
    m_chargeWindup = {};
    m_fireQuick   = false;
    m_fireCharged = false;
    if (WeaponLoadout* w = localWeapons())
        w->clear();
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

    Health* hpFx = localHealth();
    if (hpFx && !hpFx->alive())
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

void SandboxApp::handleWeaponSwitch()
{
    WeaponLoadout* w = localWeapons();
    if (!w)
        return;
    WeaponLoadoutComponent* wlc = world().get<WeaponLoadoutComponent>(possessedBody());
    if (input().actionPressed("weapon_1") && w->selectMelee())
    {
        if (wlc)
            wlc->slot = w->slot();
        playSoundCue(world(), audio(), assets(), m_camera, "click");
        DE_LOG_INFO("Player: weapon melee");
    }
    if (input().actionPressed("weapon_2") && w->selectProjectile())
    {
        if (wlc)
            wlc->slot = w->slot();
        playSoundCue(world(), audio(), assets(), m_camera, "click");
        DE_LOG_INFO("Player: weapon {}", w->projectile().name());
    }
}

WeaponWorldQuery SandboxApp::makeWeaponQuery()
{
    m_weaponTargets.clear();
    const Entity self = possessedBody();
    world().each<HittableComponent>([&](Entity e, HittableComponent& h) {
        if (self.valid() && e.id() == self.id())
            return;
        const TransformComponent* xf = world().get<TransformComponent>(e);
        const HealthComponent*    hp = world().get<HealthComponent>(e);
        if (!xf)
            return;
        WeaponTargetScratch t{};
        t.entity      = e;
        t.center      = xf->position;
        t.halfExtents = h.halfExtents;
        t.alive       = hp && hp->health.alive();
        m_weaponTargets.push_back(t);
    });

    WeaponWorldQuery q{};
    q.terrainUser = this;
    q.raycastTerrain = [](void* user, const Ray3f& ray, float maxDistance) -> Collision::RayHit3D {
        return static_cast<SandboxApp*>(user)->m_terrain.raycast(ray, maxDistance);
    };
    q.heightAt = [](void* user, float x, float z) {
        return static_cast<SandboxApp*>(user)->m_terrain.heightAtWorld(x, z);
    };
    q.targetUser = this;
    q.targetCount = [](void* user) {
        return static_cast<int>(static_cast<SandboxApp*>(user)->m_weaponTargets.size());
    };
    q.targetAlive = [](void* user, int i) {
        auto* app = static_cast<SandboxApp*>(user);
        return i >= 0 && i < static_cast<int>(app->m_weaponTargets.size()) && app->m_weaponTargets[static_cast<size_t>(i)].alive;
    };
    q.targetCenter = [](void* user, int i) {
        auto* app = static_cast<SandboxApp*>(user);
        if (i < 0 || i >= static_cast<int>(app->m_weaponTargets.size()))
            return Vector3f{ 0.0f, 0.0f, 0.0f };
        return app->m_weaponTargets[static_cast<size_t>(i)].center;
    };
    q.targetHalfExtentsAt = [](void* user, int i) {
        auto* app = static_cast<SandboxApp*>(user);
        if (i < 0 || i >= static_cast<int>(app->m_weaponTargets.size()))
            return Vector3f{ 1.0f, 1.0f, 1.0f };
        return app->m_weaponTargets[static_cast<size_t>(i)].halfExtents;
    };
    q.targetEntityAt = [](void* user, int i) {
        auto* app = static_cast<SandboxApp*>(user);
        if (i < 0 || i >= static_cast<int>(app->m_weaponTargets.size()))
            return Entity{};
        return app->m_weaponTargets[static_cast<size_t>(i)].entity;
    };
    q.targetHalfExtents = Vector3f{ 1.0f, 1.0f, 1.0f };
    q.maxRange          = 90.0f;
    return q;
}

void SandboxApp::onWeaponHitThunk(void* user, const WeaponHit& hit)
{
    if (auto* app = static_cast<SandboxApp*>(user))
        app->onWeaponHit(hit);
}

void SandboxApp::onWeaponHit(const WeaponHit& hit)
{
    // Terrain hits are !hitTarget. The mark still lands; damage stays behind that gate.
    spawnWeaponImpact(hit);
    if (!hit.hitTarget)
        return;
    Entity victim = hit.targetEntity;
    if (!victim.valid() && hit.targetIndex >= 0 && hit.targetIndex < static_cast<int>(m_weaponTargets.size()))
        victim = m_weaponTargets[static_cast<size_t>(hit.targetIndex)].entity;
    if (!victim.valid() || !world().alive(victim))
        return;
    HealthComponent* hp = world().get<HealthComponent>(victim);
    const bool wasAlive = hp && hp->health.alive();
    if (world().has<AiAgentComponent>(victim))
    {
        if (!m_chaseOk)
            return;
        float               damage = hit.damage;
        Combat::ArmorBreak  broke;
        if (Combat::ArmorPiecesComponent* armor = world().get<Combat::ArmorPiecesComponent>(victim))
        {
            if (const TransformComponent* vxf = world().get<TransformComponent>(victim))
                damage = Combat::absorbArmorHit(*armor, vxf->position, vxf->rotation, hit.point, hit.direction, damage, broke);
        }
        if (broke.index >= 0)
            knockOffArmor(world(), m_physics, victim, broke);
        if (!m_chase.ai().applyHunterDamage(world(), victim, damage))
            return;
        m_chase.ai().applyHunterHitReaction(world(), victim, hit.direction);
        playSoundCueAt(world(), audio(), assets(), victim, "pain", hit.point);
        playSoundCueAt(world(), audio(), assets(), victim, "grunt", hit.point);
        const TransformComponent* playerXf = possessedBody().valid() ? world().get<TransformComponent>(possessedBody()) : nullptr;
        const Vector3f playerPos = playerXf ? playerXf->position : Vector3f{};
        m_chase.ai().onHunterAttacked(world(), victim, playerPos);
        spawnHunterBlood(hit.point);
        spawnLivingBloodDecal(victim, hit.point, hit.normal);
        hp = world().get<HealthComponent>(victim);
        if (wasAlive && hp && !hp->health.alive())
        {
            spawnDeathBloodDecal(hit.point, hit.normal);
            if (!deferredDecals())
                m_bloodSplats.spawn(hit.point.x, hit.point.z, m_terrain.coarse());
            m_chase.ai().onHunterKilled(world(), victim);
        }
        return;
    }
    if (!hp)
        return;
    const float before = hp->health.hp();
    hp->health.applyDamage(hit.damage);
    if (hp->health.hp() >= before)
        return;
    if (HitReactionComponent* hr = world().get<HitReactionComponent>(victim))
        hr->hit.apply(hit.direction);
    playSoundCueAt(world(), audio(), assets(), victim, "pain", hit.point);
}

void SandboxApp::syncPlayerMode()
{
    const Entity body = possessedBody();
    if (!body.valid() || !world().alive(body))
        return;
    if (!m_godMode && !m_reaperMode)
    {
        if (world().has<Combat::PlayerModeComponent>(body))
            world().remove<Combat::PlayerModeComponent>(body);
        return;
    }
    Combat::PlayerModeComponent* mode = world().get<Combat::PlayerModeComponent>(body);
    if (!mode)
        mode = &world().emplace<Combat::PlayerModeComponent>(body);
    mode->godMode    = m_godMode;
    mode->reaperMode = m_reaperMode;
}

void SandboxApp::updateCombat(float dt)
{
    if (m_hurtSoundTimer > 0.0f)
        m_hurtSoundTimer -= dt;
    if (m_muzzleTimer > 0.0f)
    {
        m_muzzleTimer -= dt;
        if (m_muzzleTimer <= 0.0f)
        {
            if (LocalLightComponent* light = m_muzzle.valid() ? world().get<LocalLightComponent>(m_muzzle) : nullptr)
                light->enabled = false;
        }
        else if (TransformComponent* mxf = m_muzzle.valid() ? world().get<TransformComponent>(m_muzzle) : nullptr)
            mxf->position = m_viewCamera.GetPosition() + m_viewCamera.GetLook() * 0.8f;
    }

    const Entity body = possessedBody();
    if (Health* hpTick = localHealth())
        hpTick->tick(dt);
    if (Combat::StatusEffectComponent* st = body.valid() ? world().get<Combat::StatusEffectComponent>(body) : nullptr)
        st->tick(dt);
    const WeaponWorldQuery query = makeWeaponQuery();
    if (WeaponLoadout* wTick = localWeapons())
        wTick->tick(dt, query);

    Health* hpCombat = localHealth();
    if (!hpCombat || !hpCombat->alive())
    {
        m_fireQuick   = false;
        m_fireCharged = false;
        m_playerDeadTimer += dt;
        if (m_playerDeadTimer >= kDeathSeconds)
            respawnPlayer();
        return;
    }

    constexpr float kStandoff = 2.25f;
    constexpr float kContactDps = 12.0f;
    constexpr float kJumpAttackBuffer = 0.12f;
    const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    JumpAttackComponent* jac = body.valid() ? world().get<JumpAttackComponent>(body) : nullptr;
    Combat::JumpAttack*  jump = jac ? &jac->jump : nullptr;
    Combat::StatusEffectComponent* status = body.valid() ? world().get<Combat::StatusEffectComponent>(body) : nullptr;
    HitReaction* hitRx = localHit();
    PlayerMotor* motor = localMotor();
    const bool inAirCommit = jump && jump->inAirCommit();
    const bool ccLocked    = (hitRx && hitRx->stunned()) || (status && status->hasHardCc());
    const bool jumpBusy    = jump && jump->busy();

    if (m_chaseOk && xf && !inAirCommit)
    {
        const float before = hpCombat->hp();
        Combat::CombatSystem combat;
        if (Combat::DefenseComponent* defense = world().get<Combat::DefenseComponent>(body))
            Combat::syncShieldDefense(*defense, m_offhand.shield, m_lookYaw);
        world().each<AiAgentComponent>([&](Entity e, AiAgentComponent&) {
            const HealthComponent* hp = world().get<HealthComponent>(e);
            const TransformComponent* hxf = world().get<TransformComponent>(e);
            if (!hp || !hp->health.alive() || !hxf)
                return;
            const float dx = hxf->position.x - xf->position.x;
            const float dz = hxf->position.z - xf->position.z;
            if (dx * dx + dz * dz > kStandoff * kStandoff)
                return;
            Vector3f hitDir{ xf->position.x - hxf->position.x, 0.0f, xf->position.z - hxf->position.z };
            if (hitDir.MagnitudeSqrd() > 1.0e-8f)
                hitDir.Normalize();
            else
                hitDir = Vector3f{ 0.0f, 0.0f, 1.0f };
            Combat::DamageEvent ev{};
            ev.source   = e;
            ev.target   = body;
            ev.amount   = kContactDps * dt;
            ev.type     = Combat::DamageType::Slash;
            ev.hitDir   = hitDir;
            ev.hitPoint = xf->position;
            ev.flags    = Combat::DamageFlags::CanBlock | Combat::DamageFlags::CanParry;
            if (combat.resolve(world(), ev).killed)
            {
                DE_LOG_INFO("Player: down");
                playSoundCue(world(), audio(), assets(), possessedBody(), "death");
            }
        });
        if (hpCombat->hp() < before && m_hurtSoundTimer <= 0.0f)
        {
            playSoundCue(world(), audio(), assets(), possessedBody(), "pain");
            m_hurtSoundTimer = 0.40f;
            Vector3f away{ 0.0f, 0.0f, 0.0f };
            float    best = kStandoff * kStandoff;
            world().each<AiAgentComponent>([&](Entity e, AiAgentComponent&) {
                const HealthComponent* hp = world().get<HealthComponent>(e);
                const TransformComponent* hxf = world().get<TransformComponent>(e);
                if (!hp || !hp->health.alive() || !hxf)
                    return;
                const float dx = hxf->position.x - xf->position.x;
                const float dz = hxf->position.z - xf->position.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 > best)
                    return;
                best = d2;
                away = Vector3f{ -dx, 0.0f, -dz };
            });
            if (HitReaction* hit = localHit())
                hit->apply(away);
        }
    }

    if (m_jumpAttackBuffer > 0.0f)
        m_jumpAttackBuffer = Math::Max(0.0f, m_jumpAttackBuffer - dt);

    const bool attackPressed = input().actionPressed("attack") || (!m_showDevTools && input().mousePressed(MouseButton::Left));
    const bool swimming = motor && motor->state() == PlayerMoveState::Swimming;
    const bool airborne = motor && (motor->state() == PlayerMoveState::Jumping || motor->state() == PlayerMoveState::Falling);
    const bool fireQuick = m_fireQuick;
    const bool fireCharged = m_fireCharged;
    m_fireQuick   = false;
    m_fireCharged = false;

    auto noteSenses = [&]() {
        if (SkillComponent* sk = body.valid() ? world().get<SkillComponent>(body) : nullptr)
        {
            const bool foreign = audio().liveForeignSpatialVoices(body.id()) > 0;
            notePlayerSenseXp(*sk, sk->seeArmed, m_offhand.lightOn, foreign, dt);
        }
    };

    if (ccLocked || jumpBusy)
    {
        if (ccLocked)
            m_jumpAttackBuffer = 0.0f;
        noteSenses();
        return;
    }

    if (swimming)
    {
        m_jumpAttackBuffer = 0.0f;
        if (fireCharged)
            firePossessedLoadout(true);
        else if (fireQuick)
            firePossessedLoadout(false);
        noteSenses();
        return;
    }

    auto tryBeginJumpAttack = [&]() -> bool {
        if (!jump || !xf || !motor)
            return false;
        const float groundY = m_terrain.heightAtWorld(xf->position.x, xf->position.z);
        const float heightAbove = xf->position.y - groundY - motor->settings().groundOffset;
        const bool  airOk = heightAbove >= jump->def().minHeight || motor->airTime() >= jump->def().minAirTime;
        if (!airOk)
            return false;
        Vector3f lookFlat{ m_viewCamera.GetLook().x, 0.0f, m_viewCamera.GetLook().z };
        if (lookFlat.MagnitudeSqrd() > 1.0e-6f)
            lookFlat.Normalize();
        else
            lookFlat = Vector3f{ 0.0f, 0.0f, 1.0f };
        Combat::JumpAttackBegin req{};
        req.attacker          = body;
        req.position          = xf->position;
        req.lookFlat          = lookFlat;
        req.velocity          = motor->velocity();
        req.heightAboveGround = heightAbove;
        req.airTime           = motor->airTime();
        if (!jump->begin(req))
            return false;
        motor->clearJumpBuffer();
        tryJumpAttackAnim(world(), body);
        DE_LOG_INFO("Player: jump attack");
        return true;
    };

    if (airborne)
    {
        if (attackPressed || m_jumpAttackBuffer > 0.0f)
        {
            if (tryBeginJumpAttack())
                m_jumpAttackBuffer = 0.0f;
            else if (attackPressed)
                m_jumpAttackBuffer = kJumpAttackBuffer;
        }
        noteSenses();
        return;
    }

    if (fireCharged)
    {
        m_jumpAttackBuffer = 0.0f;
        firePossessedLoadout(true);
    }
    else if (fireQuick || m_jumpAttackBuffer > 0.0f)
    {
        m_jumpAttackBuffer = 0.0f;
        firePossessedLoadout(false);
    }
    noteSenses();
}

void SandboxApp::resolveJumpAttackAndFx(const Combat::DamageEvent* events, int count)
{
    if (count <= 0 || !events)
        return;

    struct Snap
    {
        Entity e{};
        float  hp      = 0.0f;
        bool   wasAlive = false;
        bool   hunter  = false;
    };
    Snap snaps[8]{};
    const int n = count < 8 ? count : 8;
    for (int i = 0; i < n; ++i)
    {
        const Entity t = events[i].target;
        snaps[i].e     = t;
        if (!t.valid() || !world().alive(t))
            continue;
        snaps[i].hunter = world().has<AiAgentComponent>(t);
        if (const HealthComponent* hp = world().get<HealthComponent>(t))
        {
            snaps[i].hp       = hp->health.hp();
            snaps[i].wasAlive = hp->health.alive();
        }
    }

    Combat::CombatSystem combat;
    Combat::resolveJumpAttackEvents(world(), combat, events, count);

    const Entity player = possessedBody();
    const TransformComponent* playerXf = player.valid() ? world().get<TransformComponent>(player) : nullptr;
    const Vector3f playerPos = playerXf ? playerXf->position : Vector3f{};
    for (int i = 0; i < n; ++i)
    {
        if (!snaps[i].e.valid())
            continue;
        HealthComponent* hp = world().get<HealthComponent>(snaps[i].e);
        if (!hp)
            continue;
        const bool damaged = hp->health.hp() < snaps[i].hp;
        const bool killed  = snaps[i].wasAlive && !hp->health.alive();
        if (!damaged && !killed)
            continue;
        if (!snaps[i].hunter)
        {
            if (snaps[i].e.id() == player.id())
                playSoundCue(world(), audio(), assets(), snaps[i].e, "pain");
            continue;
        }
        playSoundCueAt(world(), audio(), assets(), snaps[i].e, "pain", events[i].hitPoint);
        playSoundCueAt(world(), audio(), assets(), snaps[i].e, "grunt", events[i].hitPoint);
        m_chase.ai().onHunterAttacked(world(), snaps[i].e, playerPos);
        spawnHunterBlood(events[i].hitPoint);
        spawnLivingBloodDecal(snaps[i].e, events[i].hitPoint, -events[i].hitDir);
        if (killed)
        {
            // Jump events have no surface normal. A flat hint keeps the stain on the height field.
            spawnDeathBloodDecal(events[i].hitPoint, Vector3f(0.0f, 0.0f, 1.0f));
            if (!deferredDecals())
                m_bloodSplats.spawn(events[i].hitPoint.x, events[i].hitPoint.z, m_terrain.coarse());
            m_chase.ai().onHunterKilled(world(), snaps[i].e);
        }
    }
}

bool SandboxApp::firePossessedLoadout(bool charged)
{
    const Entity body = possessedBody();
    const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    WeaponFireRequest req{};
    req.direction = m_viewCamera.GetLook();
    if (req.direction.MagnitudeSqrd() > 1.0e-6f)
        req.direction.Normalize();
    else
        req.direction = Vector3f{ 0.0f, 0.0f, 1.0f };
    req.origin      = m_viewCamera.GetPosition() + req.direction * 2.2f;
    req.ownerPos    = xf ? xf->position : m_viewCamera.GetPosition();
    req.damageScale = charged ? m_chargeSettings.attackDamageScale : 1.0f;

    WeaponLoadout*  wFire = localWeapons();
    SkillComponent* skill = nullptr;
    if (wFire && wFire->activeKind() == WeaponKind::Projectile && body.valid())
        skill = world().get<SkillComponent>(body);
    if (skill)
    {
        const int shootLevel = skill->level(SkillId::Shoot);
        req.recoilScale      = skillScalar(SkillId::Shoot, SkillScalar::RecoilScale, shootLevel);
        req.cooldownScale    = skillScalar(SkillId::Shoot, SkillScalar::CooldownScale, shootLevel);
    }

    const WeaponWorldQuery query = makeWeaponQuery();
    if (!wFire || !wFire->fire(req, query))
        return false;
    if (skill)
        noteShotXp(*skill, true);
    if (AnimGraphComponent* ag = body.valid() ? world().get<AnimGraphComponent>(body) : nullptr)
    {
        if (wFire->activeKind() == WeaponKind::Melee)
            ag->graph.setTrigger("swing");
        else
            ag->graph.setTrigger("shoot");
    }
    pulseMuzzle();
    if (wFire->activeKind() == WeaponKind::Melee)
        playSoundCue(world(), audio(), assets(), m_camera, "click");
    else
    {
        const RecoilKick kick = wFire->projectile().takeRecoil();
        m_lookYaw += kick.yaw;
        m_lookPitch += kick.pitch;
        m_lookYaw   = Math::WrapPi(m_lookYaw);
        m_lookPitch = Math::Clamp(m_lookPitch, -0.96f, 0.96f);
        updateShoulderCamera();
    }
    return true;
}

void SandboxApp::pulseMuzzle()
{
    if (!m_muzzle.valid())
        return;
    if (TransformComponent* xf = world().get<TransformComponent>(m_muzzle))
        xf->position = m_viewCamera.GetPosition() + m_viewCamera.GetLook() * 0.8f;
    if (LocalLightComponent* light = world().get<LocalLightComponent>(m_muzzle))
    {
        light->enabled   = true;
        light->intensity = 37699.0f; // 12000*π after Fd/π
        light->range     = 6.0f;
    }
    m_muzzleTimer = 0.05f;
}

void SandboxApp::updateFlashlight()
{
    if (!m_flashlight.valid())
        return;
    TransformComponent* xf = world().get<TransformComponent>(m_flashlight);
    if (!xf)
        return;
    const TransformComponent* bodyXf = possessedBody().valid() ? world().get<TransformComponent>(possessedBody()) : nullptr;
    const Vector3f            bodyPos = bodyXf ? bodyXf->position : m_viewCamera.GetPosition();
    placePlayerFlashlight(*xf, bodyPos, m_viewCamera.GetLook(), m_viewCamera.GetRight(), m_viewCamera.GetUp());
    LocalLightComponent* light = world().get<LocalLightComponent>(m_flashlight);
    if (!light)
        return;
    light->enabled = m_offhand.lightOn;
    if (m_flashlightBaseId != m_flashlight.id())
    {
        m_flashlightBaseRange = light->range;
        m_flashlightBaseOuter = light->outerConeDeg;
        m_flashlightBaseId    = m_flashlight.id();
    }
    float seeRange = kSkillIdentity;
    float seeCone  = kSkillIdentity;
    const Entity body = possessedBody();
    if (const SkillComponent* sk = body.valid() ? world().get<SkillComponent>(body) : nullptr)
    {
        const int seeLevel = sk->level(SkillId::See);
        seeRange = skillScalar(SkillId::See, SkillScalar::SeeRangeScale, seeLevel);
        seeCone  = skillScalar(SkillId::See, SkillScalar::SeeConeScale, seeLevel);
    }
    scaleFlashlight(m_flashlightBaseRange, m_flashlightBaseOuter, seeRange, seeCone, light->range, light->outerConeDeg);
}

bool SandboxApp::createSandboxModels()
{
    MeshData cubeData;
    if (!CreateCube(cubeData, 1.0f))
        return false;
    auto cubeMat = std::make_shared<Material>();
    if (!cubeMat->createFromAlbedoPath(assets(), "textures/dark_engine_cube.png", /*fallback*/ 64, 166, 242, 255))
        return false;
    cubeMat = assets().internMaterial(cubeMat, "runtime:/sandbox/cube-mat");
    AssetRef<Model> cubeModel = internAndUploadProceduralModel(renderer(), assets(), std::move(cubeData), cubeMat, "runtime:/sandbox/cube");
    if (!cubeModel)
        return false;
    m_cubeModelId = cubeModel->id;

    MeshData packData;
    if (!CreateCross(packData, 1.0f, 0.30f, 0.22f))
        return false;
    AssetRef<Material> packMat = internSolidMaterial(assets(), 214, 28, 36, 255, "runtime:/sandbox/pack-mat");
    AssetRef<Model> packModel  = internAndUploadProceduralModel(renderer(), assets(), std::move(packData), packMat, "runtime:/sandbox/pack");
    if (!packModel)
        return false;
    m_packModelId = packModel->id;

    MeshData lanternData;
    if (!CreateCube(lanternData, 1.0f))
        return false;
    auto lanternMat = std::make_shared<Material>();
    if (!lanternMat->createSolid(assets(), 220, 150, 60, 255))
        return false;
    lanternMat->setEmissive(1.0f);
    lanternMat = assets().internMaterial(lanternMat, "runtime:/sandbox/lantern-mat");
    if (!lanternMat || lanternMat->id == NULL_ASSET)
        return false;
    AssetRef<Model> lanternModel = internAndUploadProceduralModel(renderer(), assets(), std::move(lanternData), lanternMat, "runtime:/sandbox/lantern");
    if (!lanternModel)
        return false;
    m_lanternModelId = lanternModel->id;

    MeshData tracerData;
    if (!CreateSphere(tracerData, 0.5f, 8, 12))
    {
        DE_LOG_ERROR("SandboxApp: projectile tracer mesh failed");
        return true;
    }
    AssetRef<Material> tracerMat = internSolidMaterial(assets(), 255, 196, 48, 255, "runtime:/sandbox/tracer-mat");
    AssetRef<Model> tracerModel  = internAndUploadProceduralModel(renderer(), assets(), std::move(tracerData), tracerMat, "runtime:/sandbox/tracer");
    if (tracerModel)
        m_tracerModelId = tracerModel->id;
    else
        DE_LOG_ERROR("SandboxApp: projectile tracer model failed");
    return true;
}

void SandboxApp::spawnGltfDemo()
{
    auto spawn = [&](const char* virtualPath, const char* tag, float x, float z, float scale) {
        AssetRef<Model> model = loadAndUploadModel(renderer(), assets(), virtualPath);
        if (!model || !model->valid())
        {
            DE_LOG_WARN("SandboxApp: glTF '{}' not loaded", virtualPath);
            return;
        }
        const float groundY = m_terrain.heightAtWorld(x, z);
        const float y       = groundY + scale * 0.5f;
        Entity e = world().createEntity();
        world().emplace<TagComponent>(e, tag);
        world().emplace<TransformComponent>(e, Vector3f{ x, y, z }, Quaternion::IDENTITY, Vector3f{ scale, scale, scale });
        ModelComponent mc;
        mc.modelAssetID = model->id;
        mc.castShadow   = model->hasOpaque();
        world().emplace<ModelComponent>(e, mc);
        DE_LOG_INFO("SandboxApp: spawned {} at ({:.1f},{:.1f},{:.1f}) ground {:.1f}", tag, x, y, z, groundY);
    };

    // Sit on the terrain next to the player cube at the origin (default camera looks here).
    const EntityMaster cubeMaster = loadEntityMasterType("unit_cube");
    const EntityMaster glassMaster = loadEntityMasterType("unit_glass");
    const std::string  cubeGltf = cubeMaster.gltf.empty() ? std::string("models/unit_cube.gltf") : cubeMaster.gltf;
    const std::string  glassGltf = glassMaster.gltf.empty() ? std::string("models/unit_glass.gltf") : glassMaster.gltf;
    spawn(cubeGltf.c_str(), "GltfCube", 3.0f, 3.0f, 2.0f);
    spawn(glassGltf.c_str(), "GltfGlass", 5.5f, 3.0f, 2.0f);
    spawnAnimatedDemo();
}

void SandboxApp::onWiggleNotify(void*, const AnimNotify& n)
{
    if (!n.name || std::strcmp(n.name, "footstep") != 0)
        return;
    DE_LOG_INFO("SandboxApp: wiggle footstep t={:.2f}", n.time);
}

void SandboxApp::spawnAnimatedDemo()
{
    const EntityMaster wiggleMaster = loadEntityMasterType("wiggle");
    const std::string  wiggleGltf = wiggleMaster.gltf.empty() ? std::string("models/wiggle.gltf") : wiggleMaster.gltf;
    const char*        kGltf = wiggleGltf.c_str();
    AssetRef<Model> model = loadAndUploadModel(renderer(), assets(), kGltf);
    if (!model || !model->valid())
    {
        DE_LOG_WARN("SandboxApp: animated glTF '{}' not loaded", kGltf);
        return;
    }
    AssetRef<AnimGraphDef> graph;
    if (!wiggleMaster.anim.empty())
        graph = assets().loadAnimGraph(wiggleMaster.anim);
    if (!graph)
        graph = assets().tryLoadAnimGraphForModel(kGltf);
    if (!graph)
        DE_LOG_WARN("SandboxApp: '{}' has no anim graph sidecar; clips still play if present", kGltf);

    constexpr float x = 8.0f;
    constexpr float z = 3.0f;
    constexpr float scale = 2.0f;
    const float groundY = m_terrain.heightAtWorld(x, z);
    Entity e = world().createEntity();
    world().emplace<TagComponent>(e, "Wiggle");
    world().emplace<TransformComponent>(e, Vector3f{ x, groundY, z }, Quaternion::IDENTITY, Vector3f{ scale, scale, scale });
    ModelComponent mc;
    mc.modelAssetID = model->id;
    mc.castShadow   = model->hasOpaque();
    world().emplace<ModelComponent>(e, mc);

    AnimGraphComponent ag;
    ag.model   = model;
    ag.animSet = model->animationSet();
    ag.graphDef = graph;
    if (graph && model->skeleton())
        ag.graph.bind(graph.get(), model->skeleton());
    else if (ag.animSet && model->skeleton())
    {
        ag.graph.player().bind(model->skeleton(), ag.animSet.get());
        if (!ag.graph.player().play("Idle", 0.0f))
            ag.graph.player().playIndex(0, 0.0f);
    }
    ag.graph.setApplyRootMotion(false);
    ag.graph.player().addListener(&SandboxApp::onWiggleNotify, this);
    world().emplace<AnimGraphComponent>(e, std::move(ag));
    m_wiggle = e;
    DE_LOG_INFO("SandboxApp: spawned Wiggle at ({:.1f},{:.1f},{:.1f}) ground {:.1f} graph={}", x, groundY, z, groundY, graph ? "yes" : "no");
}

void SandboxApp::updateWiggleAnim()
{
    if (!m_wiggle.valid())
        return;
    AnimGraphComponent* ag = world().get<AnimGraphComponent>(m_wiggle);
    if (!ag || !ag->graphDef)
        return;

    float speed = 0.0f;
    const bool uiKeys = m_showDevTools && m_imgui.isReady() && m_imgui.wantCaptureKeyboard();
    if (!uiKeys && input().actionDown("anim_walk"))
        speed = 1.0f;
    else
    {
        const Vector3f v = localMotor() ? localMotor()->velocity() : Vector3f{};
        speed = Vector3f(v.x, 0.0f, v.z).Magnitude();
    }
    ag->graph.setFloat("speed", speed);
}

bool SandboxApp::createSkeletonLineBuffers()
{
    ID3D12Device* device = renderer().device();
    if (!device)
        return false;
    constexpr uint64_t kMaxVerts = 1024;
    const uint64_t vbBytes = sizeof(Vector3f) * kMaxVerts;
    const uint64_t ibBytes = sizeof(uint32_t) * kMaxVerts;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Height           = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels        = 1;
    desc.SampleDesc       = { 1, 0 };
    desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (int i = 0; i < 2; ++i)
    {
        desc.Width = vbBytes;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skelLineVb[i]))))
            return false;
        desc.Width = ibBytes;
        if (FAILED(device->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_skelLineIb[i]))))
            return false;
        m_skelLineVbv[i].BufferLocation = m_skelLineVb[i]->GetGPUVirtualAddress();
        m_skelLineVbv[i].StrideInBytes  = sizeof(Vector3f);
        m_skelLineVbv[i].SizeInBytes    = static_cast<UINT>(vbBytes);
        m_skelLineIbv[i].BufferLocation = m_skelLineIb[i]->GetGPUVirtualAddress();
        m_skelLineIbv[i].Format         = DXGI_FORMAT_R32_UINT;
        m_skelLineIbv[i].SizeInBytes    = static_cast<UINT>(ibBytes);
    }
    return true;
}

void SandboxApp::drawSkeletonOverlay(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj)
{
    if (!m_showSkeleton || !cmd || !m_skelLinePipeline.isValid() || !m_skelLineVb[0])
        return;

    const GpuScope skeleton(cmd, "Skeleton", ProfileColor::Skeleton);
    SkeletonDebugLines lines;
    world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
        const TransformComponent* xf = world().get<TransformComponent>(e);
        const auto model = assets().getAs<Model>(mc.modelAssetID);
        if (!xf || !model || !model->skinned() || !model->skeleton())
            return;
        const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
        if (!pose || pose->boneCount == 0)
            return;
        SkeletonDebugLines one;
        collectSkeletonDebugLines(*model->skeleton(), *pose, makeWorldMatrix(*xf), 0.25f, one);
        lines.bones.insert(lines.bones.end(), one.bones.begin(), one.bones.end());
        lines.axisX.insert(lines.axisX.end(), one.axisX.begin(), one.axisX.end());
        lines.axisY.insert(lines.axisY.end(), one.axisY.begin(), one.axisY.end());
        lines.axisZ.insert(lines.axisZ.end(), one.axisZ.begin(), one.axisZ.end());
    });

    struct Batch
    {
        const std::vector<Vector3f>* verts;
        float r, g, b;
    };
    const Batch batches[] = {
        { &lines.bones, 1.00f, 0.45f, 0.95f },
        { &lines.axisX, 1.00f, 0.25f, 0.20f },
        { &lines.axisY, 0.25f, 1.00f, 0.30f },
        { &lines.axisZ, 0.30f, 0.55f, 1.00f },
    };

    std::vector<Vector3f> verts;
    std::vector<uint32_t> idx;
    uint32_t rangeStart[4]{};
    uint32_t rangeCount[4]{};
    verts.reserve(256);
    idx.reserve(256);
    for (int b = 0; b < 4; ++b)
    {
        rangeStart[b] = static_cast<uint32_t>(idx.size());
        const auto& src = *batches[b].verts;
        for (size_t i = 0; i + 1 < src.size(); i += 2)
        {
            const uint32_t i0 = static_cast<uint32_t>(verts.size());
            verts.push_back(src[i]);
            verts.push_back(src[i + 1]);
            idx.push_back(i0);
            idx.push_back(i0 + 1);
        }
        rangeCount[b] = static_cast<uint32_t>(idx.size()) - rangeStart[b];
    }
    constexpr size_t kMaxVerts = 1024;
    if (verts.empty() || idx.empty() || verts.size() > kMaxVerts)
        return;

    const uint32_t fi = renderer().frameIndex() % 2;
    void* vp = nullptr;
    void* ip = nullptr;
    if (FAILED(m_skelLineVb[fi]->Map(0, nullptr, &vp)) || FAILED(m_skelLineIb[fi]->Map(0, nullptr, &ip)))
        return;
    std::memcpy(vp, verts.data(), verts.size() * sizeof(Vector3f));
    std::memcpy(ip, idx.data(), idx.size() * sizeof(uint32_t));
    m_skelLineVb[fi]->Unmap(0, nullptr);
    m_skelLineIb[fi]->Unmap(0, nullptr);
    m_skelLineVbv[fi].SizeInBytes = static_cast<UINT>(verts.size() * sizeof(Vector3f));
    m_skelLineIbv[fi].SizeInBytes = static_cast<UINT>(idx.size() * sizeof(uint32_t));

    m_skelLinePipeline.bind(cmd);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    cmd->IASetVertexBuffers(0, 1, &m_skelLineVbv[fi]);
    cmd->IASetIndexBuffer(&m_skelLineIbv[fi]);
    LineFrameConstants lc{};
    copyMatrix(lc.worldViewProj, viewProj);
    lc.color[3] = 1.0f;
    for (int b = 0; b < 4; ++b)
    {
        if (rangeCount[b] == 0)
            continue;
        lc.color[0] = batches[b].r;
        lc.color[1] = batches[b].g;
        lc.color[2] = batches[b].b;
        m_skelLinePipeline.setConstants(cmd, lc);
        cmd->DrawIndexedInstanced(rangeCount[b], 1, rangeStart[b], 0, 0);
    }
}

void SandboxApp::spawnHybridLocalLights()
{
    if (renderer().scenePath() != ScenePath::HybridDeferred)
        return;

    m_flashlight = world().createEntity();
    world().emplace<TagComponent>(m_flashlight, "Flashlight");
    world().emplace<TransformComponent>(m_flashlight, Vector3f{}, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    auto& flashlight          = world().emplace<LocalLightComponent>(m_flashlight);
    flashlight.type           = LocalLightType::Spot;
    flashlight.color          = Vector3f{ 1.0f, 0.97f, 0.9f };
    flashlight.intensity      = 1571.0f; // 500*π after Fd/π
    flashlight.range          = 22.0f;
    flashlight.innerConeDeg   = 10.0f;
    flashlight.outerConeDeg   = 22.0f;
    flashlight.enabled        = true;
    flashlight.castShadow     = true;
    updateFlashlight();

    m_muzzle = world().createEntity();
    world().emplace<TagComponent>(m_muzzle, "Muzzle");
    world().emplace<TransformComponent>(m_muzzle, Vector3f{}, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
    auto& muzzle     = world().emplace<LocalLightComponent>(m_muzzle);
    muzzle.type      = LocalLightType::Point;
    muzzle.color     = Vector3f{ 1.0f, 0.82f, 0.45f };
    muzzle.intensity = 37699.0f; // 12000*π after Fd/π
    muzzle.range     = 6.0f;
    muzzle.enabled   = false;
    m_muzzleTimer    = 0.0f;

    // ±1.2 from pack/tree XZ so 0.22 cubes sit outside trunks (r=0.5) and health crosses.
    const Vector3f spots[] = {
        { 6.2f, 0.0f, 5.0f },
        { -8.2f, 0.0f, 7.0f },
        { 8.0f, 0.0f, -9.2f },
        { -4.0f, 0.0f, -5.8f },
        { 13.2f, 0.0f, 8.0f },
        { -11.2f, 0.0f, 10.0f },
        { 14.0f, 0.0f, -7.2f },
        { 5.2f, 0.0f, -18.0f },
    };
    const float waterY = m_water.params().waterLevel;
    int         spawned = 0;
    bool        firstLantern = true;
    for (const Vector3f& s : spots)
    {
        const float gy = m_terrain.heightAtWorld(s.x, s.z);
        if (gy < waterY - 0.2f)
            continue;
        const Vector3f pos{ s.x, gy + 1.55f, s.z };

        Entity fixture = world().createEntity();
        world().emplace<TagComponent>(fixture, "Lantern");
        world().emplace<TransformComponent>(fixture, pos, Quaternion::IDENTITY, Vector3f{ 0.22f, 0.22f, 0.22f });
        if (m_lanternModelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = m_lanternModelId;
            mc.castShadow   = true;
            setModelComponent(world(), pins(), assets(), fixture, mc);
        }

        Entity lightE = world().createEntity();
        world().emplace<TagComponent>(lightE, "LanternLight");
        world().emplace<TransformComponent>(lightE, pos, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
        auto& light        = world().emplace<LocalLightComponent>(lightE);
        light.type         = LocalLightType::Point;
        light.color        = Vector3f{ 1.0f, 0.72f, 0.35f };
        light.intensity    = 1257.0f; // 400*π after Fd/π
        light.range        = 6.0f;
        light.emissiveMesh = fixture;
        if (firstLantern)
        {
            light.castShadow = true;
            firstLantern     = false;
        }
        ++spawned;
    }
    DE_LOG_INFO("SandboxApp: flashlight on, muzzle ready, {} demo lanterns", spawned);
}

void SandboxApp::spawnHunterBlood(const Vector3f& pos)
{
    m_blood.setTransform(Vector3f{ pos.x, pos.y + 0.45f, pos.z });
    m_blood.emitBurst(12);
}

namespace
{

    bool decalPointFinite(const Vector3f& p)
    {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    Vector3f decalSafeNormal(const Vector3f& n)
    {
        if (n.MagnitudeSqrd() <= 1.0e-8f)
            return Vector3f(0.0f, 1.0f, 0.0f);
        Vector3f out = n;
        out.Normalize();
        return out;
    }

    Vector3f decalFlatFacing(const Vector3f& facing)
    {
        Vector3f f(facing.x, 0.0f, facing.z);
        if (f.MagnitudeSqrd() <= 1.0e-8f)
            return Vector3f(0.0f, 0.0f, 1.0f);
        f.Normalize();
        return f;
    }

    Vector3f decalYawFacing(float x, float z)
    {
        const uint32_t hx  = static_cast<uint32_t>(std::fabs(x) * 1000.0f);
        const uint32_t hz  = static_cast<uint32_t>(std::fabs(z) * 1000.0f);
        const uint32_t h   = hx * 1664525u + hz + 1013904223u;
        const float    yaw = static_cast<float>(h & 0xFFFFu) * (6.2831853f / 65535.0f);
        return Vector3f(std::cos(yaw), 0.0f, std::sin(yaw));
    }

}

bool SandboxApp::deferredDecals()
{
    return renderer().scenePath() == ScenePath::HybridDeferred
        && renderer().debugState().decalsEnabled
        && m_scene.decalsReady();
}

bool SandboxApp::isStepCue(Entity e, const char* cue)
{
    const TransformComponent* xf = e.valid() ? world().get<TransformComponent>(e) : nullptr;
    const char* groundCue = nullptr;
    if (xf)
        groundCue = groundContactAt(xf->position.x, xf->position.z).cue;
    return decalCueIsGroundStep(cue, groundCue);
}

void SandboxApp::spawnFootmark(const Vector3f& bodyPos, const Vector3f& facing)
{
    if (!deferredDecals())
        return;
    DecalSpawnDesc desc;
    desc.kind     = DecalKind::Footmark;
    desc.space    = DecalSpace::World;
    desc.position = Vector3f(bodyPos.x, m_terrain.heightAtWorld(bodyPos.x, bodyPos.z), bodyPos.z);
    desc.axisY    = Vector3f(0.0f, 1.0f, 0.0f);
    desc.axisX    = decalFlatFacing(facing);
    m_scene.spawnDecal(desc);
}

void SandboxApp::spawnHunterFootmark(Entity hunter)
{
    if (!deferredDecals())
        return;
    const TransformComponent* xf = hunter.valid() ? world().get<TransformComponent>(hunter) : nullptr;
    if (!xf)
        return;
    Vector3f facing(0.0f, 0.0f, 1.0f);
    if (const AiAgentComponent* ai = world().get<AiAgentComponent>(hunter))
        facing = ai->forward;
    spawnFootmark(xf->position, facing);
}

void SandboxApp::spawnWeaponImpact(const WeaponHit& hit)
{
    if (!deferredDecals() || !decalPointFinite(hit.point))
        return;
    const Vector3f normal = decalSafeNormal(hit.normal);
    DecalSpawnDesc desc;
    desc.kind   = DecalKind::Impact;
    desc.weapon = hit.weapon;
    desc.axisY  = normal;
    desc.axisX  = decalImpactAxisX(normal, hit.direction);
    Entity victim = hit.targetEntity;
    if (!victim.valid() && hit.targetIndex >= 0 && hit.targetIndex < static_cast<int>(m_weaponTargets.size()))
        victim = m_weaponTargets[static_cast<size_t>(hit.targetIndex)].entity;
    const bool attach = hit.hitTarget && victim.valid() && world().alive(victim) && world().get<TransformComponent>(victim);
    if (attach)
    {
        desc.entity   = victim;
        desc.position = decalShellBiasedPosition(hit.point, normal);
        if (world().has<AnimGraphComponent>(victim))
        {
            desc.space = DecalSpace::Bone;
            desc.bone  = -1;
        }
        else
            desc.space = DecalSpace::Entity;
    }
    else
    {
        desc.space    = DecalSpace::World;
        desc.position = hit.point;
    }
    m_scene.spawnDecal(desc);
}

void SandboxApp::spawnLivingBloodDecal(Entity victim, const Vector3f& point, const Vector3f& hitNormal)
{
    if (!deferredDecals() || !decalPointFinite(point))
        return;
    if (!victim.valid() || !world().alive(victim) || !world().get<TransformComponent>(victim))
        return;
    const Vector3f normal = decalSafeNormal(hitNormal);
    DecalSpawnDesc desc;
    desc.kind     = DecalKind::Blood;
    desc.entity   = victim;
    desc.position = decalShellBiasedPosition(point, normal);
    desc.axisY    = normal;
    desc.axisX    = decalImpactAxisX(normal, Vector3f(1.0f, 0.0f, 0.0f));
    if (world().has<AnimGraphComponent>(victim))
    {
        desc.space = DecalSpace::Bone;
        desc.bone  = -1;
    }
    else
        desc.space = DecalSpace::Entity;
    m_scene.spawnDecal(desc);
}

void SandboxApp::spawnDeathBloodDecal(const Vector3f& point, const Vector3f& hitNormal)
{
    if (!deferredDecals() || !decalPointFinite(point))
        return;
    DecalSpawnDesc desc;
    desc.kind     = DecalKind::Blood;
    desc.space    = DecalSpace::World;
    desc.position = Vector3f(point.x, m_terrain.heightAtWorld(point.x, point.z), point.z);
    desc.axisY    = decalDeathAxisY(hitNormal, m_terrain.normalAtWorld(point.x, point.z));
    desc.axisX    = decalYawFacing(point.x, point.z);
    m_scene.spawnDecal(desc);
}

void SandboxApp::placeHealthPacks()
{
    std::vector<Entity> old;
    world().each<HealthPackComponent>([&](Entity e, HealthPackComponent&) { old.push_back(e); });
    for (Entity e : old)
    {
        onEntityRemoved(world(), e, &pins());
        world().destroyEntity(e);
    }

    const Vector3f spots[] = {
        { 5.0f, 0.0f, 5.0f },
        { -7.0f, 0.0f, 7.0f },
        { 8.0f, 0.0f, -8.0f },
        { -4.0f, 0.0f, -7.0f },
    };
    const float   waterY = m_water.params().waterLevel;
    int count = 0;
    for (const Vector3f& s : spots)
    {
        const float gy = m_terrain.heightAtWorld(s.x, s.z);
        if (gy < waterY - 0.2f)
            continue;
        const Vector3f pos{ s.x, gy + 0.95f, s.z };
        Entity         e = world().createEntity();
        world().emplace<TagComponent>(e, "HealthPack");
        TransformComponent xf{};
        xf.position = pos;
        xf.scale    = Vector3f{ 0.9f, 0.9f, 0.9f };
        world().emplace<TransformComponent>(e, xf);
        if (m_packModelId != NULL_ASSET)
        {
            ModelComponent mc{};
            mc.modelAssetID = m_packModelId;
            mc.castShadow   = true;
            setModelComponent(world(), pins(), assets(), e, mc);
        }
        HealthPackComponent pack{};
        pack.restPos = pos;
        pack.active  = true;
        world().emplace<HealthPackComponent>(e, pack);
        stampProceduralId(world(), e, std::format("sandbox/healthpack/{}", count), "healthpack");
        ++count;
    }
    DE_LOG_INFO("SandboxApp: {} health packs", count);
}

void SandboxApp::updateHealthPacks(float dt)
{
    const int taken = tickHealthPacks(world(), possessedBody(), dt);
    for (int i = 0; i < taken; ++i)
        playSoundCue(world(), audio(), assets(), possessedBody(), "heal");
}

void SandboxApp::drawProjectiles(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, MeshFrameConstants& cb)
{
    WeaponLoadout* w = localWeapons();
    const auto model = assets().getAs<Model>(m_tracerModelId);
    if (!cmd || !w || !model || !model->hasOpaque() || !renderer().gpuResources().ensureModel(model))
        return;
    bool any = false;
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (s.alive)
        {
            any = true;
            break;
        }
    }
    if (!any)
        return;

    const GpuScope projectiles(cmd, "Projectiles", ProfileColor::Projectiles);
    const float     radius = w->projectile().desc().radius;
    const float     scale  = radius * 2.0f;
    const DebugFill fill   = renderer().debugState().fill;
    GpuResourceCache& gpu  = renderer().gpuResources();
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (!s.alive)
            continue;
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
        drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, world, viewProj, cb, fill);
    }
}

void SandboxApp::drawProjectilesGBuffer(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, const Matrix4f& prevViewProj)
{
    WeaponLoadout* w = localWeapons();
    const auto model = assets().getAs<Model>(m_tracerModelId);
    if (!cmd || !w || !model || !model->hasOpaque() || !renderer().gpuResources().ensureModel(model))
        return;
    bool any = false;
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (s.alive)
        {
            any = true;
            break;
        }
    }
    if (!any)
        return;

    const GpuScope projectiles(cmd, "Projectiles", ProfileColor::Projectiles);
    const float     radius = w->projectile().desc().radius;
    const float     scale  = radius * 2.0f;
    const DebugFill fill   = renderer().debugState().fill;
    GpuResourceCache& gpu  = renderer().gpuResources();
    for (const LiveProjectile& s : w->projectile().live())
    {
        if (!s.alive)
            continue;
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(scale, scale, scale) * Matrix4f::TranslationMatrix(s.position.x, s.position.y, s.position.z);
        drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, world, viewProj, prevViewProj, fill);
    }
}

void SandboxApp::updateShoulderCamera()
{
    const Entity body = possessedBody();
    const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
    if (!xf)
        return;

    const bool crouched = localMotor() && localMotor()->state() == PlayerMoveState::Crouch;
    const Vector3f target{ xf->position.x, xf->position.y + (crouched ? 0.78f : 1.15f), xf->position.z };
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

    m_viewCamera.SetLens(1.04719755f, m_viewCamera.GetAspect(), m_viewCamera.GetNearZ(), m_viewCamera.GetFarZ());
    const Vector3f aim = target + look * 16.0f;
    m_viewCamera.LookAt(cam, aim, Vector3f{ 0.0f, 1.0f, 0.0f });
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
    attachSandboxCubeModel(world(), pins(), assets(), e, m_cubeModelId);
    if (!network().registerEntity(world(), e, NetPrefab::PlayerPawn, owner, pawnPaletteColor(owner)))
    {
        onEntityRemoved(world(), e, &pins());
        world().destroyEntity(e);
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register pawn for client {}", static_cast<unsigned>(owner));
        return;
    }
    if (owner == network().localClientId())
        attachLocalPlayer(e);
    else
        attachReplicaCombat(e);
}

void SandboxApp::ensureLocalCube()
{
    if (m_cube.valid() && world().alive(m_cube))
        return;

    const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
    m_cube              = world().createEntity();
    world().emplace<TagComponent>(m_cube, "Cube");
    world().emplace<TransformComponent>(m_cube, Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    attachSandboxCubeModel(world(), pins(), assets(), m_cube, m_cubeModelId);
    if (!network().registerEntity(world(), m_cube, NetPrefab::Cube))
    {
        onEntityRemoved(world(), m_cube, &pins());
        world().destroyEntity(m_cube);
        m_cube = {};
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register local cube");
    }
}

bool SandboxApp::onNetSpawn(World& world, Entity e, NetPrefab prefab, const TransformComponent&, uint32_t, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app || !e.valid())
        return false;
    if (!world.has<ModelComponent>(e))
        attachSandboxCubeModel(world, app->pins(), app->assets(), e, app->m_cubeModelId);
    if (prefab == NetPrefab::PlayerPawn)
    {
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        const bool local = nc && nc->owner == app->network().localClientId();
        if (local)
            app->attachLocalPlayer(e);
        else
            app->attachReplicaCombat(e);
    }
    return true;
}

void SandboxApp::onNetDespawn(World& world, Entity e, NetId, void* user)
{
    auto* app = static_cast<SandboxApp*>(user);
    if (!app)
        return;
    onEntityRemoved(world, e, &app->pins());
    if (app->m_cube == e)
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
    if (const Entity body = possessedBody(); body.valid())
    {
        if (const TransformComponent* xf = world().get<TransformComponent>(body))
            m_terrain.pinWorldXZ(xf->position.x, xf->position.z, 3);
    }
    else
        m_terrain.clearPin();

    m_terrain.updateStreaming(m_viewCamera.GetPosition(), &renderer(), &m_terrainMaterial);
    if (m_terrain.heightTexture().valid())
    {
        renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());
        m_waterPipeline.setHeightSrv(renderer().device(), m_terrain.heightTexture().cpuHandle());
    }

    const bool waterDirty = m_water.needsRebuild();
    if (!waterDirty)
        return;
    m_water.rebuildDirtyCpuMeshes();
    if (!m_water.uploadDirty(renderer()))
        DE_LOG_ERROR("SandboxApp: water upload failed");
}

void SandboxApp::createPhysicsWorld()
{
    Physics::PhysicsWorldDesc desc;
    desc.enabled = true;
    desc.gravity = 24.0f;
    if (!m_physics.create(desc))
    {
        DE_LOG_ERROR(LogCategory::Collision, "SandboxApp: physics world create failed");
        return;
    }

    const HeightMap& coarse = m_terrain.coarse();
    if (!coarse.valid())
    {
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: no terrain height map; physics has no ground");
        return;
    }

    Physics::PhysicsHeightFieldDesc hf;
    hf.heights     = coarse.samples();
    hf.countX      = coarse.width();
    hf.countZ      = coarse.height();
    hf.cellSize    = coarse.cellSize();
    hf.heightScale = coarse.heightScale();
    hf.origin      = coarse.origin();
    hf.friction    = 0.8f;
    m_physicsGround = m_physics.createHeightField(hf);
    if (m_physicsGround == Physics::kNullPhysicsBody)
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: terrain height field failed");
}

void SandboxApp::addChaseObstaclesToPhysics()
{
    if (!m_physics.valid())
        return;

    for (const AABox3f& box : m_chase.cubes())
    {
        Physics::PhysicsBoxDesc desc;
        desc.position    = box.Center();
        desc.halfExtents = box.Extents();
        desc.dynamic     = false;
        desc.friction    = 0.6f;
        if (m_physics.createBox(desc) == Physics::kNullPhysicsBody)
            DE_LOG_WARN(LogCategory::Collision, "SandboxApp: chase obstacle body failed");
    }
}

void SandboxApp::bindEntityPhysics(Entity e, const char* entityType)
{
    if (!m_physics.valid() || !e.valid() || !world().alive(e))
        return;

    const EntityMaster master = loadEntityMasterType(entityType);
    if (master.physics.empty())
    {
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: '{}' has no physics card", entityType);
        return;
    }

    const std::filesystem::path file = resolveContentFile(master.physics);
    PhysicsComponent settings;
    if (file.empty() || !Physics::loadPhysicsSettingsFile(file, settings))
    {
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: physics card '{}' for '{}' missing or invalid", master.physics, entityType);
        return;
    }
    world().emplace<PhysicsComponent>(e, std::move(settings));

    AssetRef<Model> model;
    if (const ModelComponent* mc = world().get<ModelComponent>(e))
        model = assets().getAs<Model>(mc->modelAssetID);
    if (!Physics::bindPhysicsEntity(m_physics, world(), e, model.get()))
        DE_LOG_WARN(LogCategory::Collision, "SandboxApp: physics bind failed for '{}' #{}", entityType, e.id());
}

void SandboxApp::bindCharacterPhysics()
{
    bindEntityPhysics(m_chase.walker(), "player");
    for (int i = 0; i < m_chase.hunterCount(); ++i)
    {
        const Entity e = m_chase.hunterEntity(i);
        const TagComponent* tag = e.valid() ? world().get<TagComponent>(e) : nullptr;
        const bool wolf = tag && tag->name == "Wolf";
        bindEntityPhysics(e, wolf ? "wolf" : "human");
    }
}

float SandboxApp::playerGroundHeight(float x, float z) const
{
    float groundY = m_terrain.heightAtWorld(x, z);
    if (!m_physics.valid())
        return groundY;

    Physics::PhysicsWorld::GroundProbe probe;
    probe.from       = Vector3f(x, m_groundProbeY, z);
    probe.radius     = 0.3f;
    probe.maxDrop    = (m_groundProbeY - groundY) + 1.0f;
    probe.ignore     = m_groundIgnore;
    probe.ignore2    = m_physicsGround;
    probe.staticOnly = true;

    float surfaceY = 0.0f;
    if (probe.maxDrop > 0.0f && m_physics.probeGround(probe, surfaceY) && surfaceY > groundY)
        groundY = surfaceY;
    return groundY;
}

Terrain::GroundContact SandboxApp::groundContactAt(float x, float z) const
{
    const Terrain::HeightMap* height = m_terrain.editableWorking();
    if (!height || !height->valid())
        height = &m_terrain.coarse();
    const Terrain::SplatMap* splat = m_terrain.editableWorkingSplat();
    return m_ground.at(height, splat && splat->valid() ? splat : nullptr, x, z);
}

void SandboxApp::onInit()
{
    DE_LOG_INFO("SandboxApp: init");

    mountContentRoots(assets());
    m_ground.loadFromContent();
    skillCatalog().loadFromContent();
    m_chase.setGround(&m_ground);
    registerDefaultActions();
    audio().setMasterVolume(0.85f);

    if (!renderer().enableSceneBuffers(config().scenePath))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: SceneBuffers enable failed; SwapChainForward");
    if (renderer().scenePath() == ScenePath::HybridDeferred)
        renderer().debugState().decalsEnabled = true;

    {
        SceneRendererDesc sceneDesc{};
        sceneDesc.createWorldEnvironment = true;
        sceneDesc.createAutoExposure     = renderer().scenePath() == ScenePath::HybridDeferred;
        sceneDesc.logTag = "SandboxApp";
        if (!m_scene.init(renderer(), sceneDesc))
        {
            requestQuit();
            return;
        }
        m_foliagePipeline.create(renderer().device());
        m_foliagePrototypes.create(renderer(), assets());
        if (!m_skelLinePipeline.create(renderer().device(), renderer().sceneColorFormat(), false))
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skeleton LinePipeline create failed");
        else if (!createSkeletonLineBuffers())
            DE_LOG_ERROR(LogCategory::Render, "SandboxApp: skeleton line buffers failed");
    }

    if (!m_healthHud.create(renderer()))
        DE_LOG_ERROR("SandboxApp: health HUD failed");
    if (!m_crosshair.create(renderer()))
        DE_LOG_ERROR("SandboxApp: crosshair HUD failed");
    if (!m_particles.create(renderer(), assets()))
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
    if (!pumpBootFrame())
        return;
    if (!pumpBootFrame())
        return;
    if (!pumpBootFrame())
        return;
    if (!pumpBootFrame())
        return;
    populateMainMenu();
    if (!m_menu.create(renderer(), UiAccent::Sandbox))
        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: main menu GPU init failed");
    else if (shouldShowMainMenu(config()))
        m_menu.show();

    if (!m_imgui.init(window(), renderer(), "sandbox_imgui.ini", false, UiAccent::Sandbox))
        DE_LOG_WARN("SandboxApp: ImGui init failed — Dev Tools (M) disabled");
    if (!pumpBootFrame())
        return;

    if (!loadAndBakeIbl(renderer(), assets(), m_ibl, m_iblImageId))
        DE_LOG_INFO(LogCategory::Render, "SandboxApp: IBL off — using ambient");

    std::vector<SceneObjectData> scenePawns;
    {
        auto sidecar = [](const std::filesystem::path& scenePath, const std::string& file) {
            if (file.empty())
                return std::filesystem::path{};
            const std::filesystem::path p(file);
            if (p.is_absolute())
                return p;
            const std::filesystem::path parent = scenePath.has_parent_path() ? scenePath.parent_path() : std::filesystem::path{};
            return parent / p;
        };

        bool loadedScene = false;
        bool worldEngine = false;
        Terrain::SplatMap matSplat;
        Terrain::WorldEngineMaps worldMaps;
        SceneFileData sceneData{};
        {
            std::string   err;
            const std::filesystem::path scenePath = defaultScenePath("level.json");
            if (!loadSceneFromJson(scenePath, sceneData, &err))
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene load failed ({}) — {}", scenePath.string(), err);
            else
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: scene {}", scenePath.string());
            applySceneAtmosphere(m_env, sceneData);
            {
                const ExposureSceneDesc exposure = sceneData.exposure.present ? sceneData.exposure : ExposureSceneDesc{};
                m_autoExposure.mode        = exposure.autoExposure ? ExposureMode::Auto : ExposureMode::Manual;
                m_autoExposure.evBias      = exposure.evBias;
                m_autoExposure.maxEv       = exposure.maxEv;
                m_autoExposure.adaptBright = exposure.adaptBright;
                m_autoExposure.adaptDark   = exposure.adaptDark;
                resetAutoExposure(m_autoExposureState);
                DE_LOG_INFO(LogCategory::Render, "SandboxApp: exposure auto={}", exposure.autoExposure);
            }
            if (sceneData.mode != SceneMode::Scene2D && sceneData.hasTerrain)
            {
                if (!sceneData.terrain.source.empty())
                {
                    const std::filesystem::path dir = Terrain::resolveWorldEngineDirectory(sceneData.terrain.source);
                    Terrain::WorldEngineLoadDesc importDesc{};
                    importDesc.worldSizeMeters   = sceneData.terrain.worldSize > 1.0f ? sceneData.terrain.worldSize : 1024.0f;
                    importDesc.heightRangeMeters = sceneData.terrain.importHeight > 0.0f ? sceneData.terrain.importHeight : 480.0f;
                    const int chunkCells = sceneData.terrain.chunkCells > 0 ? sceneData.terrain.chunkCells : 64;
                    if (!dir.empty()
                        && Terrain::loadWorldEngineDirectory(dir, importDesc, worldMaps)
                        && m_terrain.createFromHeightMap(std::move(worldMaps.height), chunkCells))
                    {
                        matSplat = worldMaps.splat;
                        if (!m_terrain.setWorkingSplat(std::move(worldMaps.splat)))
                            DE_LOG_WARN(LogCategory::Render, "SandboxApp: World Engine splat was not kept");
                        loadedScene = true;
                        worldEngine = true;
                        DE_LOG_INFO(LogCategory::Render, "SandboxApp: loaded World Engine terrain '{}'", sceneData.terrain.source);
                    }
                    else
                        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: World Engine folder '{}' failed", sceneData.terrain.source);
                }
                else if (sceneData.terrain.hasGrid)
                {
                    TerrainGridDesc gridDesc{};
                    gridDesc.tilesX       = sceneData.terrain.grid.tilesX;
                    gridDesc.tilesZ       = sceneData.terrain.grid.tilesZ;
                    gridDesc.tileCells    = sceneData.terrain.grid.tileCells;
                    gridDesc.chunkCells   = sceneData.terrain.chunkCells > 0 ? sceneData.terrain.chunkCells : 64;
                    gridDesc.cellSize     = sceneData.terrain.grid.cellSize;
                    gridDesc.heightScale  = sceneData.terrain.grid.heightScale;
                    gridDesc.origin       = sceneData.terrain.grid.origin;
                    gridDesc.coarseFile   = sidecar(scenePath, sceneData.terrain.grid.coarseFile);
                    gridDesc.tileDir      = sidecar(scenePath, sceneData.terrain.grid.tileDir);
                    gridDesc.residentRing = sceneData.terrain.grid.residentRing;
                    if (m_terrain.create(gridDesc))
                    {
                        loadedScene        = true;
                        m_haveTerrainSea   = true;
                        m_terrainSeaLevel  = sceneData.terrain.grid.seaLevel;
                        DE_LOG_INFO(LogCategory::Render, "SandboxApp: streaming scene terrain {}x{} tiles", gridDesc.tilesX, gridDesc.tilesZ);
                    }
                    else
                        DE_LOG_ERROR(LogCategory::Render, "SandboxApp: scene terrain.grid failed — FBM fallback");
                }
                else if (!sceneData.terrain.heightFile.empty())
                {
                    HeightMap height;
                    if (height.loadBinary(sidecar(scenePath, sceneData.terrain.heightFile))
                        && m_terrain.createFromHeightMap(std::move(height), sceneData.terrain.chunkCells > 0 ? sceneData.terrain.chunkCells : 16))
                    {
                        loadedScene = true;
                        DE_LOG_INFO(LogCategory::Render, "SandboxApp: loaded legacy heightFile as 1-tile Grid");
                    }
                }
            }
        }

        m_foliageDensity.dirtTreesPerM2    = sceneData.terrain.foliage.dirtTreesPerM2;
        m_foliageDensity.dirtFlowersPerM2  = sceneData.terrain.foliage.dirtFlowersPerM2;
        m_foliageDensity.grassTreesPerM2   = sceneData.terrain.foliage.grassTreesPerM2;
        m_foliageDensity.grassFlowersPerM2 = sceneData.terrain.foliage.grassFlowersPerM2;
        m_foliageDensity.rockPerM2         = sceneData.terrain.foliage.rockPerM2;
        m_foliageDensity.grassPerM2        = sceneData.terrain.foliage.grassPerM2;
        m_foliageDensity.seed              = sceneData.terrain.foliage.seed;
        m_foliageDensity.treeModel         = sceneData.terrain.foliage.treeModel;
        m_foliageDensity.flowerModel       = sceneData.terrain.foliage.flowerModel;
        m_foliageDensity.rockModel         = sceneData.terrain.foliage.rockModel;
        m_foliageDensity.grassModel        = sceneData.terrain.foliage.grassModel;

        if (!loadedScene)
        {
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
            if (!matSplat.generateFromHeight(base))
            {
                DE_LOG_FATAL("SandboxApp: splat generate failed");
                requestQuit();
                return;
            }
            if (!m_terrain.createFromHeightMap(std::move(base), 16))
            {
                DE_LOG_FATAL("SandboxApp: terrain create failed");
                requestQuit();
                return;
            }
            m_terrain.setWorkingSplat(SplatMap(matSplat));
        }

        if (worldEngine)
        {
            const HeightMap* height = m_terrain.editableWorking();
            const SplatMap*  splat  = m_terrain.editableWorkingSplat();
            FoliageSpawnIn   spawnIn;
            spawnIn.height    = height;
            spawnIn.splat     = splat;
            spawnIn.density   = m_foliageDensity;
            spawnIn.tilesX    = m_terrain.tilesX();
            spawnIn.tilesZ    = m_terrain.tilesZ();
            const int cells   = m_terrain.tileCells();
            spawnIn.tileCells = cells > 0 ? static_cast<uint32_t>(cells) : 0u;
            spawnIn.cellSize  = m_terrain.cellSize();
            spawnIn.origin    = m_terrain.origin();
            spawnIn.seaLevel  = height ? height->origin().y : 0.0f;
            FoliageSpawnOut spawned;
            if (height && splat && spawnFoliage(spawnIn, spawned))
                m_foliage.swap(spawned.records);
            else
                DE_LOG_WARN(LogCategory::Render, "SandboxApp: splat foliage spawn failed");
        }

        if (!matSplat.valid() && !matSplat.create(2, 2))
        {
            DE_LOG_FATAL("SandboxApp: dummy splat failed");
            requestQuit();
            return;
        }
        if (worldEngine)
        {
            if (!Terrain::uploadWorldEngineMaterial(renderer(), assets(), matSplat, worldMaps, m_terrainMaterial, nullptr))
            {
                DE_LOG_FATAL("SandboxApp: World Engine material failed");
                requestQuit();
                return;
            }
            DE_LOG_INFO(LogCategory::Render, "Terrain: World Engine albedo and roughness");
        }
        else if (tryCreateTerrainFromContent(renderer(), assets(), matSplat, m_terrainMaterial))
            DE_LOG_INFO(LogCategory::Render, "Terrain: using content layer set");
        else
        {
            DE_LOG_INFO(LogCategory::Render, "Terrain: checkers fallback");
            if (!m_terrainMaterial.createDefault(renderer(), matSplat))
            {
                DE_LOG_FATAL("SandboxApp: terrain material create failed");
                requestQuit();
                return;
            }
        }
        m_terrainMaterial.setLayerSamplingRaw(renderer().device(), renderer().debugState().legacyUnormAlbedo);
        if (!pumpBootFrame())
            return;
        m_terrain.updateStreaming(Vector3f{ 0.0f, 50.0f, -80.0f }, &renderer(), &m_terrainMaterial);
        if (!m_terrain.uploadCoarseHeightTexture(renderer()))
        {
            DE_LOG_FATAL("SandboxApp: coarse height texture upload failed");
            requestQuit();
            return;
        }
        if (m_terrain.heightTexture().valid())
        {
            renderer().setHeightSrv(m_terrain.heightTexture().cpuHandle());
            m_waterPipeline.setHeightSrv(renderer().device(), m_terrain.heightTexture().cpuHandle());
        }
        if (!pumpBootFrame())
            return;

        auto spawnCloudEntity = [&](const Vector3f& pos, const Vector3f& scale, const CloudVolumeDesc& desc) {
            Entity e = world().createEntity();
            TransformComponent xf{};
            xf.position = pos;
            xf.scale    = scale;
            world().emplace<TransformComponent>(e, xf);
            CloudVolumeComponent cloud{};
            cloud.desc = desc;
            world().emplace<CloudVolumeComponent>(e, cloud);
        };
        uint32_t spawnedClouds = 0;
        for (const SceneObjectData& o : sceneData.objects)
        {
            if (o.type == SceneObjectType::Hunter || o.type == SceneObjectType::Wolf)
                scenePawns.push_back(o);
            if (o.type != SceneObjectType::CloudVolume)
                continue;
            CloudVolumeDesc desc{};
            if (o.hasCloud)
                cloudDescFromSceneData(o, desc);
            spawnCloudEntity(o.position, o.scale, desc);
            ++spawnedClouds;
        }
        if (spawnedClouds == 0)
        {
            CloudVolumeDesc desc{};
            spawnCloudEntity(Vector3f(0.0f, 36.0f, 0.0f), Vector3f(140.0f, 32.0f, 140.0f), desc);
            ++spawnedClouds;
        }
        DE_LOG_INFO(LogCategory::Render, "SandboxApp: {} cloud volume(s)", spawnedClouds);

        const AABox3f terrainBox = m_terrain.bounds();
        const WaterSceneDesc& waterScene = sceneData.water;
        float waterLevel = Lerp(terrainBox.Min.y, terrainBox.Max.y, waterScene.levelFraction);
        if (waterScene.hasLevel)
            waterLevel = waterScene.level;
        else if (m_haveTerrainSea)
            waterLevel = m_terrainSeaLevel;
        WaterDesc waterDesc;
        waterDesc.chunkCells = waterScene.chunkCells > 0 ? waterScene.chunkCells : 16;
        waterDesc.waterLevel = waterLevel;
        const int lodCount = waterScene.lodDistanceCount < 1 ? 1 : (waterScene.lodDistanceCount > Terrain::kMaxLodLevels ? Terrain::kMaxLodLevels : waterScene.lodDistanceCount);
        waterDesc.lodDistanceCount = lodCount;
        for (int i = 0; i < lodCount; ++i)
            waterDesc.lodDistances[i] = waterScene.lodDistances[i];
        waterDesc.params = sceneWaterParams(waterScene, waterLevel);
        if (!m_water.create(m_terrain.coarse(), waterDesc))
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

        // Streams are visual only. cellWet and swimming a creek stay on the still lake.
        for (const SceneObjectData& o : sceneData.objects)
        {
            if (o.type != SceneObjectType::Stream || !o.hasStream)
                continue;
            if (m_streams.size() >= 16)
                break;
            StreamDesc desc;
            desc.width        = o.streamWidth;
            desc.flowSpeed    = o.streamFlowSpeed;
            desc.bedClearance = 0.45f;
            desc.pointsXZ     = o.streamPoints;
            MeshData cpu;
            SandboxStream slot;
            std::string err;
            if (!buildStreamRibbon(m_terrain.coarse(), desc, waterLevel, cpu, &slot.bounds, &err))
                continue;
            if (!Mesh::tryCreate(renderer(), cpu, slot.mesh))
            {
                DE_LOG_ERROR(LogCategory::Render, "SandboxApp: stream upload failed");
                continue;
            }
            slot.flowSpeed = o.streamFlowSpeed;
            m_streams.push_back(std::move(slot));
        }
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

    if (!createSandboxModels())
    {
        DE_LOG_FATAL("SandboxApp: procedural models failed");
        requestQuit();
        return;
    }

    if (!pumpBootFrame())
        return;

    renderer().gpuResources().setShadowSrv(m_shadows.srvCpu());
    renderer().setShadowSrv(m_shadows.srvCpu());
    m_skyPipeline.setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_waterPipeline.setShadowSrv(renderer().device(), m_shadows.srvCpu());
    m_scene.localShadows().setDepthPipeline(&m_shadows.pipeline());
    {
        const LocalShadowSystem::CpuSrvs localSrvs = m_scene.localShadows().cpuSrvs();
        renderer().setLocalShadowSrvs(localSrvs.arraySrv, localSrvs.recordsSrv);
    }
    spawnGltfDemo();

    const float aspect = (renderer().height() > 0) ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height()) : 1.0f;
    m_viewCamera.SetLens(/*fovY*/ 1.04719755f /*60deg*/, aspect, 0.05f, 1.0e5f);
    m_viewCamera.LookAt(Vector3f(0.0f, 48.0f, -86.0f), Vector3f(0.0f, 8.0f, 0.0f), Vector3f(0.0f, 1.0f, 0.0f));

    m_camera = world().createEntity();
    world().emplace<TagComponent>(m_camera, "Main Camera");
    world().emplace<TransformComponent>(m_camera, m_viewCamera.GetPosition(), Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    world().emplace<CameraComponent>(m_camera, /* fovDeg */ 60.0f, /* near */ 0.5f, /* far */ 1.0e5f, /* primary */ true);
    world().emplace<AudioListenerComponent>(m_camera);
    attachCameraSounds(world(), pins(), assets(), audio(), m_camera);

    const float groundY = m_terrain.heightAtWorld(0.0f, 0.0f) + 0.5f;
    m_cube = world().createEntity();
    world().emplace<TagComponent>(m_cube, "Cube");
    world().emplace<TransformComponent>(m_cube, Vector3f{ 0.0f, groundY, 0.0f }, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    attachSandboxCubeModel(world(), pins(), assets(), m_cube, m_cubeModelId);

    network().setWantsPawn(true);
    network().setSceneMode(0);
    network().setPlayerName("Sandbox");
    network().setSpawnCallback(&SandboxApp::onNetSpawn, this);
    network().setDespawnCallback(&SandboxApp::onNetDespawn, this);
    network().setPeerCallback(&SandboxApp::onNetPeer, this);
    if (!network().registerEntity(world(), m_cube, NetPrefab::Cube))
    {
        onEntityRemoved(world(), m_cube, &pins());
        world().destroyEntity(m_cube);
        m_cube = {};
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox: failed to register init cube");
    }

    DE_LOG_INFO(
        "SandboxApp: cube mesh {} verts / {} indices, aspect {:.3f}, cube model id={}, terrain {}x{} tiles",
        m_cubeMesh.vertexCount(),
        m_cubeMesh.indexCount(),
        aspect,
        m_cubeModelId,
        m_terrain.tilesX(),
        m_terrain.tilesZ());
    DE_LOG_INFO(LogCategory::Networking, "Sandbox net: Sandbox.exe -host   and   Sandbox.exe -join 127.0.0.1");
    DE_LOG_INFO(LogCategory::Networking, "Sandbox net: M opens Dev Tools (host / join / browse / debugger)");

    createPhysicsWorld();

    m_chaseOk = m_chase.init(renderer(), m_terrain, m_water, world(), pins(), assets());
    if (!m_chaseOk)
        DE_LOG_ERROR(LogCategory::AI, "SandboxApp: path chase init failed");
    else
    {
        addChaseObstaclesToPhysics();
        m_chase.ai().setJumpAttackHits(
            [](void* user, const Combat::DamageEvent* events, int count) {
                static_cast<SandboxApp*>(user)->resolveJumpAttackAndFx(events, count);
            },
            this);
        m_chase.ai().setHunterCue(
            [](void* user, Entity hunter, const char* cue) {
                auto* app = static_cast<SandboxApp*>(user);
                if (!cue || cue[0] == '\0')
                    return;
                if (std::strcmp(cue, "grunt") == 0)
                    tryJumpAttackAnim(app->world(), hunter);
                // playSoundCue returns when the bank has the cue, so the mark has to be first.
                if (app->isStepCue(hunter, cue))
                    app->spawnHunterFootmark(hunter);
                if (playSoundCue(app->world(), app->audio(), app->assets(), hunter, cue))
                    return;
                if (std::strcmp(cue, "pounce") == 0)
                    playSoundCue(app->world(), app->audio(), app->assets(), hunter, "impact");
                else if (std::strcmp(cue, "pound") == 0)
                    playSoundCueFallback(app->world(), app->audio(), app->assets(), hunter, "land", "impact");
            },
            this);
    }

    if (m_chase.walker().valid())
        attachLocalPlayer(m_chase.walker());
    m_shield = spawnPlayerShield(world(), pins(), assets(), renderer());
    if (m_chaseOk)
    {
        int hunterStamp = 0;
        int wolfStamp   = 0;
        for (const SceneObjectData& o : scenePawns)
        {
            const bool wolf = o.type == SceneObjectType::Wolf;
            TransformComponent xf{};
            xf.position = o.position;
            xf.rotation = o.rotation;
            const bool zeroScale = o.scale.x == 0.0f && o.scale.y == 0.0f && o.scale.z == 0.0f;
            xf.scale = zeroScale ? Vector3f{ 1.0f, 1.0f, 1.0f } : o.scale;
            xf.position.y = m_terrain.heightAtWorld(xf.position.x, xf.position.z) + 0.5f;
            if (!m_chase.ai().walkability().walkableWorld(xf.position.x, xf.position.z))
            {
                DE_LOG_WARN(LogCategory::AI, "SandboxApp: scene {} at ({:.1f}, {:.1f}) is not walkable",
                    wolf ? "wolf" : "hunter", xf.position.x, xf.position.z);
            }

            const Entity hunter = m_chase.spawnListedHunter(world(), pins(), assets(), xf);
            if (!hunter.valid())
                continue;
            const char* gltf = "models/skeleton.gltf";
            std::string wolfGltf;
            if (wolf)
            {
                const EntityMaster wolfMaster = loadEntityMasterType("wolf");
                wolfGltf = wolfMaster.gltf.empty() ? std::string("models/wolf.gltf") : wolfMaster.gltf;
                gltf     = wolfGltf.c_str();
            }
            attachAnimatedCharacter(hunter, gltf);
            attachHunterSounds(world(), pins(), assets(), audio(), hunter, m_ground);
            if (HittableComponent* hit = world().get<HittableComponent>(hunter))
                hit->halfExtents = wolf ? Vector3f{ 0.45f, 0.45f, 0.70f } : Vector3f{ 0.4f, 0.7f, 0.4f };
            if (wolf)
            {
                if (TagComponent* tag = world().get<TagComponent>(hunter))
                    tag->name = "Wolf";
                applySkillProfile(world(), hunter, "wolf");
            }
            const int stamp = wolf ? wolfStamp++ : hunterStamp++;
            stampProceduralId(world(), hunter, std::format("sandbox/level/{}/{}", wolf ? "wolf" : "hunter", stamp), wolf ? "wolf" : "hunter");
        }
        DE_LOG_INFO(LogCategory::AI, "SandboxApp: {} scene pawn(s)", m_chase.hunterCount());
    }
    if (m_chaseOk)
    {
        bindCharacterPhysics();
        for (int i = 0; i < m_chase.hunterCount(); ++i)
        {
            const Entity        e   = m_chase.hunterEntity(i);
            const TagComponent* tag = e.valid() ? world().get<TagComponent>(e) : nullptr;
            if (!tag || tag->name != "Wolf")
                equipHunterArmor(world(), pins(), assets(), renderer(), e);
        }
    }

    placeHealthPacks();
    spawnHybridLocalLights();
    ensureSessionEntity();
    installSaveHost();
    m_save.captureBaseline(world());
}

void SandboxApp::onSplashFinished()
{
    m_spawnAge = 0.0f;
    playMusicCue(world(), audio(), assets(), m_camera, "music");
}

void SandboxApp::onUpdate(float dt)
{
    m_frameDt = dt;
    const bool menuFrame = m_menu.visible();
    handleRuntimeCommands(dt);
    if (menuFrame || m_menu.visible())
    {
        m_save.service(world());
        return;
    }
    if (!m_gameplayPaused || m_stepGameplay)
    {
        syncPlayerMode();
        m_cloudTime += dt;
        if (WorldClockComponent* clock = m_session.valid() ? world().get<WorldClockComponent>(m_session) : nullptr)
            clock->playTimeSec += static_cast<double>(dt);
        m_env.tick(dt);
        m_water.tick(dt);
        if (m_chaseOk)
        {
            const PlayerMotor* motor = localMotor();
            const float speed = motor ? Vector3f{ motor->velocity().x, 0.0f, motor->velocity().z }.Magnitude() : 0.0f;
            const bool crouched = motor && motor->state() == PlayerMoveState::Crouch;
            const bool moving = speed >= m_stealth.stillSpeed;
            const bool sprinting = moving && !crouched && input().actionDown("sprint");
            m_chase.ai().setPreySense(preySenseFor(m_stealth, crouched, moving, sprinting));
            m_chase.tick(dt, world(), input(), m_terrain, possessedBody(), m_playerWet);
        }
        updateCombat(dt);
        syncArmorVisuals(world());
        if (m_physics.valid())
        {
            m_physics.pushPoses(world(), false);
            m_physics.step(dt, world());
            m_physics.writeDynamicPoses(world());
        }
        Combat::CombatSystem combat;
        Combat::harvestAndResolveDots(world(), combat);
        tickStatusFx(world(), &audio(), &assets());
        updateHealthPacks(dt);
        m_blood.update(dt);
        tickParticleEmitters(world(), dt);
        if (Health* hpAlive = localHealth(); hpAlive && hpAlive->alive())
            m_spawnAge += dt;
        updateWiggleAnim();
        updateCharacterAnims(dt);
        tickAnimGraphs(world(), assets(), dt);
        m_stepGameplay = false;
    }
    m_save.service(world());
    m_water.updateLod(m_viewCamera.GetPosition());
    syncTerrainLod();
    updateFlashlight();

    AudioListener lis{};
    lis.position = m_viewCamera.GetPosition();
    lis.forward  = m_viewCamera.GetLook();
    lis.up       = m_viewCamera.GetUp();
    lis.distanceScale = 1.0f;
    if (skillCatalog().enabled())
    {
        const Entity body = possessedBody();
        if (const SkillComponent* sk = body.valid() ? world().get<SkillComponent>(body) : nullptr)
            lis.distanceScale = skillScalar(SkillId::Hear, SkillScalar::HearScale, sk->level(SkillId::Hear));
    }
    audio().setListener(lis);
    tickSoundEmitters(world(), audio(), assets());
    m_scene.tickDecals(world(), dt);
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
    GpuResourceCache& gpu = renderer().gpuResources();
    if (m_skinRing.isValid())
        m_skinRing.beginFrame(renderer().frameIndex());

    const Frustum3f cameraCull(m_viewCamera.GetCullViewProj());
    const bool selectLocalShadows = renderer().scenePath() == ScenePath::HybridDeferred
        && renderer().debugState().localLights
        && renderer().debugState().lightingActive();
    m_scene.localShadows().update(world(), m_viewCamera, cameraCull, renderer().frameIndex(), selectLocalShadows);

    AABox3f sceneBounds = m_terrain.shadowBounds(m_viewCamera);
    world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
        if (mc.primitive == PrimitiveMesh::None)
            return;
        if (const TransformComponent* xf = world().get<TransformComponent>(e))
            sceneBounds.ExpandToInclude(xf->position);
    });
    world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
        const TransformComponent* xf = world().get<TransformComponent>(e);
        if (!xf)
            return;
        const auto model = assets().getAs<Model>(mc.modelAssetID);
        if (!model || !model->bounds().IsValid())
            return;
        sceneBounds.ExpandToInclude(model->bounds().Transformed(makeWorldMatrix(*xf)));
    });
    if (m_chaseOk)
        m_chase.expandBounds(sceneBounds);
    m_shadows.update(
        m_viewCamera,
        m_env.lightDir(),
        sceneBounds,
        m_env.sunElevation(),
        m_env.weather.cloudCoverage,
        renderer().frameIndex());

    // Opaque casters only — same set as G-buffer / forward color. Water, particles, blood, lines stay out.
    // A point light culls foliage once against its range sphere and redraws that list on each face.
    auto drawShadowCasters = [&](const Matrix4f& lightViewProj, const Frustum3f& casterFrustum, const Sphere3f* foliageSphere, bool beginFoliage) {
        m_terrain.drawDepth(cmd, &casterFrustum);
        world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (world().has<ModelComponent>(e))
                return;
            if (!mc.castShadow)
                return;
            if (const HealthComponent* hp = world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, m_cubeMesh, m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            drawShadowCaster(cmd, m_shadows.pipeline(), lightViewProj, makeWorldMatrix(*xf), *gpuMesh);
        });
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (camouflageHides(e))
                return;
            if (!mc.castShadow)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->valid() || !gpu.ensureModel(model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                if (pose)
                    drawSkinnedModelDepth(cmd, gpu, m_shadows.pipeline(), lightViewProj, m_skinnedShadowPipeline, m_skinRing, *model, *pose, worldMat);
                else
                    drawModelDepth(cmd, gpu, m_shadows.pipeline(), lightViewProj, *model, worldMat);
            }
            else
                drawModelDepth(cmd, gpu, m_shadows.pipeline(), lightViewProj, *model, worldMat);
        });
        if (m_terrain.valid())
        {
            if (beginFoliage)
                m_foliagePipeline.beginDepthView(renderer(), assets(), m_foliagePrototypes, m_terrain, m_foliage.empty() ? nullptr : &m_foliage, m_foliageDensity, m_viewCamera, casterFrustum, foliageSphere);
            m_foliagePipeline.drawPreparedDepth(cmd, lightViewProj);
        }
    };

    if (m_shadows.isValid() && m_shadows.enabled())
    {
        m_shadows.beginCapture(cmd);
        for (int i = 0; i < m_shadows.cascadeCount(); ++i)
        {
            m_shadows.beginCascade(cmd, i);
            const Frustum3f casterFrustum(m_shadows.cascade(i).viewProj);
            drawShadowCasters(m_shadows.cascade(i).viewProj, casterFrustum, nullptr, true);
        }
        m_shadows.endCapture(cmd);
    }
    else if (m_shadows.isValid())
    {
        m_shadows.endCapture(cmd);
    }

    LocalShadowSystem& localShadows = m_scene.localShadows();
    if (localShadows.faceCountThisFrame() > 0)
    {
        localShadows.beginCapture(cmd);
        for (int f = 0; f < localShadows.faceCountThisFrame(); ++f)
        {
            localShadows.beginFace(cmd, f);
            const LocalShadowFaceDraw& face = localShadows.face(f);
            Sphere3f pointSphere;
            const Sphere3f* foliageSphere = nullptr;
            if (face.firstFace)
            {
                const LocalLightComponent* light = world().get<LocalLightComponent>(face.light);
                const TransformComponent* xf = world().get<TransformComponent>(face.light);
                if (light && xf && light->type == LocalLightType::Point)
                {
                    pointSphere = Sphere3f(xf->position, Min(light->range, 80.0f));
                    foliageSphere = &pointSphere;
                }
            }
            drawShadowCasters(face.viewProj, face.frustum, foliageSphere, face.firstFace);
        }
        localShadows.endCapture(cmd);
    }
    else if (localShadows.isValid())
    {
        // An idle frame still has to leave this half of the array as a shader resource.
        localShadows.endCapture(cmd);
    }

    const bool deferred = renderer().scenePath() == ScenePath::HybridDeferred;
    const Matrix4f viewProjEarly = m_scene.beginCameraFrame(m_viewCamera, renderer());
    (void)viewProjEarly;
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

    const Matrix4f  viewProj = m_viewCamera.GetViewProj();
    const Matrix4f  prevViewProj = m_scene.havePrevViewProj() ? m_scene.prevViewProj() : viewProj;
    const Frustum3f frustum(m_viewCamera.GetCullViewProj());
    const DebugFill fill     = renderer().debugState().fill;
    const Vector3f  camPos   = m_viewCamera.GetPosition();
    uint32_t        meshDraws = 0;

    const float skyExposure = deferred ? 1.0f : m_env.exposure();
    const float fogScale    = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
    m_skyPipeline.upload(renderer().frameIndex(), m_viewCamera, m_env, skyExposure, m_water.params().waterLevel, fogScale);
    if (!deferred)
        m_skyPipeline.draw(cmd, renderer().frameIndex(), &m_shadows);

    AssetRef<Material> material;
    if (m_cube.valid())
    {
        if (auto* meshComp = world().get<MeshComponent>(m_cube))
            material = assets().getAs<Material>(meshComp->matAssetID);
        else if (const ModelComponent* mo = world().get<ModelComponent>(m_cube))
        {
            if (const auto model = assets().getAs<Model>(mo->modelAssetID))
            {
                if (const Model::Part* part = model->partAt(0))
                    material = part->material;
            }
        }
    }

    if (deferred)
    {
        {
            const GpuScope gbuffer(cmd, "GBuffer", ProfileColor::GBuffer);
            m_terrain.drawGBuffer(cmd, m_terrainPipeline, m_terrainMaterial, m_viewCamera, &frustum, &renderer().debugState(), &prevViewProj);
            {
                const GpuScope meshes(cmd, "Opaque Meshes", ProfileColor::OpaqueMeshes);
                m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
        MeshGBufferConstants gcb{};
        if (material)
            applyMaterialSurface(*material, gcb);
        else
        {
            gcb.color[0] = 1.0f;
            gcb.color[1] = 1.0f;
            gcb.color[2] = 1.0f;
            gcb.color[3] = 0.0f;
        }
        world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, m_cubeMesh, m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            if (auto mat = assets().getAs<Material>(mc.matAssetID))
            {
                gpu.ensureMaterial(mat);
                gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
                applyMaterialSurface(*mat, gcb);
            }
            const Matrix4f worldMat  = makeWorldMatrix(*xf);
            const Matrix4f prevWorld = m_prevWorldByEntity.count(e.id()) ? m_prevWorldByEntity[e.id()] : worldMat;
            fillMeshGBufferXforms(gcb, worldMat, viewProj, prevViewProj, prevWorld);
            if (const NetworkedComponent* nc = world().get<NetworkedComponent>(e))
                unpackRgba8(nc->colorRgba8, gcb.color);
            gcb.color[3] = mc.emissive;
            m_meshPipeline.setGBufferConstants(cmd, gcb);
            gpuMesh->draw(cmd, fill == DebugFill::Points);
            m_prevWorldByEntity[e.id()] = worldMat;
            ++meshDraws;
        });
            }
            drawProjectilesGBuffer(cmd, viewProj, prevViewProj);
            {
                const GpuScope models(cmd, "Opaque Models", ProfileColor::OpaqueModels);
                world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (camouflageHides(e))
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasOpaque() || !gpu.ensureModel(model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                AnimGraphComponent* ag = world().get<AnimGraphComponent>(e);
                const AnimPose* pose = skinnedPose(*model, ag);
                if (!pose)
                    return;
                Matrix4f prevW = worldMat;
                if (ag)
                {
                    if (ag->prevWorldValid)
                        prevW = ag->prevWorld;
                    ag->prevWorld = worldMat;
                    ag->prevWorldValid = true;
                }
                drawSkinnedModelOpaqueGBuffer(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_skinRing, *model, *pose, worldMat, prevW, viewProj, prevViewProj, fill);
            }
            else
                drawModelOpaqueGBuffer(cmd, gpu, m_meshPipeline, *model, worldMat, viewProj, prevViewProj, fill);
        });
            }
            if (m_terrain.valid())
                m_foliagePipeline.drawGBuffer(cmd, renderer(), assets(), m_foliagePrototypes, m_terrain, m_foliage.empty() ? nullptr : &m_foliage, m_foliageDensity, m_viewCamera, viewProj, prevViewProj, frustum);
        }

        m_scene.drawDecals(cmd, renderer(), m_viewCamera, viewProj);
        renderer().bindHdr(false);
        renderer().clearHdr();
        m_scene.applyGtao(cmd, renderer(), m_viewCamera, prevViewProj, m_ssao);
        m_scene.applySsr(cmd, renderer(), m_viewCamera, prevViewProj, m_ssr, &m_env);
        LightingConstants lc{};
        copyMatrix(lc.invViewProj, viewProj.Inverse());
        lc.cameraPos[0]     = camPos.x;
        lc.cameraPos[1]     = camPos.y;
        lc.cameraPos[2]     = camPos.z;
        lc.lightDirWS[0]    = m_env.lightDir().x;
        lc.lightDirWS[1]    = m_env.lightDir().y;
        lc.lightDirWS[2]    = m_env.lightDir().z;
        lc.lighting         = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        lc.lightColor[0]    = m_env.lightColor().x;
        lc.lightColor[1]    = m_env.lightColor().y;
        lc.lightColor[2]    = m_env.lightColor().z;
        {
            const Vector3f pbrSun = pbrSunLightColor(m_env.lightColor());
            lc.pbrLightColor[0]   = pbrSun.x;
            lc.pbrLightColor[1]   = pbrSun.y;
            lc.pbrLightColor[2]   = pbrSun.z;
        }
        lc.emissiveGain     = 4.0f;
        lc.ambientColor[0]  = m_env.ambientColor().x;
        lc.ambientColor[1]  = m_env.ambientColor().y;
        lc.ambientColor[2]  = m_env.ambientColor().z;
        FogGpu fog = makeFogGpu(&m_env, m_water.params().waterLevel, lc.lighting > 0.5f);
        fillFogHeightMap(fog, &m_terrain.coarse());
        applyFogToLighting(lc, fog);
        fillIblLightingConstants(lc, m_ibl, renderer().debugState().iblEnabled, renderer().debugState().iblDebug, iblGpuReady(renderer().gpuResources(), m_iblImageId));
        fillSsrLightingConstants(lc, m_ssr, renderer().debugState().ssrEnabled, m_scene.ssr().isValid() && renderer().hasGBuffer());
        m_lighting.draw(cmd, renderer(), m_shadows, lc);
        m_localLightVolumes.draw(cmd, renderer(), world(), m_localLightGpu, m_pointVolumeMesh, m_spotVolumeMesh, m_viewCamera, viewProj, lc, &m_scene.localShadows());

        renderer().bindHdr(true);
        m_skyPipeline.draw(cmd, renderer().frameIndex(), &m_shadows);
    }
    else
    {
        const GpuScope forward(cmd, "Forward Opaque", ProfileColor::ForwardOpaque);
        m_terrain.draw(
            cmd, m_terrainPipeline, m_terrainMaterial, m_viewCamera, &frustum, &m_env, &m_shadows,
            &renderer().debugState());

        m_meshPipeline.bind(cmd, fill);
        if (material && material->isValid())
            renderer().gpuResources().bindMaterial(cmd, *material, MeshPipeline::kRootAlbedoSrv);
        m_shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);

        MeshFrameConstants cb{};
        if (material)
            applyMaterialSurface(*material, cb);
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
        {
            const Vector3f pbrSun = pbrSunLightColor(m_env.lightColor());
            cb.lightColor[0]      = pbrSun.x;
            cb.lightColor[1]      = pbrSun.y;
            cb.lightColor[2]      = pbrSun.z;
        }
        cb.cameraPos[0]  = camPos.x;
        cb.cameraPos[1]  = camPos.y;
        cb.cameraPos[2]  = camPos.z;
        cb.lighting      = renderer().debugState().lightingActive() ? 1.0f : 0.0f;

        {
            const GpuScope meshes(cmd, "Opaque Meshes", ProfileColor::OpaqueMeshes);
            world().each<MeshComponent>([&](Entity e, MeshComponent& mc) {
            if (world().has<ModelComponent>(e))
                return;
            if (const HealthComponent* hp = world().get<HealthComponent>(e); hp && !hp->health.alive())
                return;
            const Mesh* gpuMesh = sandboxPrimitiveMesh(mc.primitive, m_cubeMesh, m_crossMesh);
            if (!gpuMesh)
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            if (!xf)
                return;
            if (auto mat = assets().getAs<Material>(mc.matAssetID))
            {
                gpu.ensureMaterial(mat);
                gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
                applyMaterialSurface(*mat, cb);
            }
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            copyMatrix(cb.worldViewProj, worldMat * viewProj);
            copyMatrix(cb.world, worldMat);
            if (const NetworkedComponent* nc = world().get<NetworkedComponent>(e))
                unpackRgba8(nc->colorRgba8, cb.color);
            m_meshPipeline.setConstants(cmd, cb);
            gpuMesh->draw(cmd, fill == DebugFill::Points);
            ++meshDraws;
        });
        }

        drawProjectiles(cmd, viewProj, cb);
        {
            const GpuScope models(cmd, "Opaque Models", ProfileColor::OpaqueModels);
            world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (camouflageHides(e))
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasOpaque() || !gpu.ensureModel(model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                AnimGraphComponent* ag = world().get<AnimGraphComponent>(e);
                const AnimPose* pose = skinnedPose(*model, ag);
                if (!pose)
                    return;
                if (ag)
                {
                    ag->prevWorld = worldMat;
                    ag->prevWorldValid = true;
                }
                drawSkinnedModelForward(cmd, gpu, m_skinnedPipeline, m_meshPipeline, m_shadows, m_skinRing, *model, false, *pose, worldMat, viewProj, cb, fill);
            }
            else
                drawModelForward(cmd, gpu, m_meshPipeline, m_shadows, *model, false, worldMat, viewProj, cb, fill);
        });
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS waterLightsVa   = m_localLightGpu.isValid() ? m_localLightGpu.dummyGpuVa() : 0;
    uint32_t                  waterLightCount = 0;
    uint32_t                  waterIndex[kWaterLocalLightMax]{};
    if (renderer().debugState().localLights && renderer().debugState().lightingActive() && m_localLightGpu.isValid())
    {
        LocalLightCullInput in{};
        in.frustum    = &frustum;
        in.cameraPos  = camPos;
        in.cameraLook = m_viewCamera.GetLook();
        in.nearZ      = m_viewCamera.GetNearZ();
        in.viewportW  = renderer().width();
        in.viewportH  = renderer().height();
        in.viewProj      = &viewProj;
        in.localShadows  = &m_scene.localShadows();
        LocalLightDrawLists lists{};
        if (gatherLocalLights(world(), in, lists) && lists.count > 0)
        {
            m_localLightGpu.upload(renderer().frameIndex(), lists);
            waterLightCount = lists.waterCount;
            std::memcpy(waterIndex, lists.waterIndex, sizeof(waterIndex));
            waterLightsVa = m_localLightGpu.lightsGpuVa();
        }
    }

    ID3D12DescriptorHeap*         heightHeap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE   heightGpu{};
    if (m_terrain.heightTexture().valid())
    {
        heightHeap = m_terrain.heightTexture().srvHeap();
        heightGpu  = m_terrain.heightTexture().gpuHandle();
    }
    else if (renderer().lightingHeap())
    {
        heightHeap = renderer().lightingHeap();
        heightGpu  = renderer().heightTableGpu();
    }
    if (renderer().hasSceneBuffers())
        m_scene.captureSsrSceneColor(cmd, renderer());
    D3D12_CPU_DESCRIPTOR_HANDLE waterSceneColor{};
    D3D12_CPU_DESCRIPTOR_HANDLE waterDepth{};
    const SsrSettings*          waterSsr = nullptr;
    if (m_scene.ssr().hasSceneColor())
    {
        waterSceneColor = m_scene.ssr().sceneColorSrvCpu();
        waterDepth      = renderer().depthSrvCpu();
        waterSsr        = &m_ssr;
    }
    const bool waterSheet = m_water.wetChunkCount() > 0;
    if (waterSheet || !m_streams.empty())
    {
        const GpuScope waterScope(cmd, "Water", ProfileColor::Water);
        uint32_t drawIndex = 0;
        if (waterSheet)
        {
            m_water.draw(
                cmd,
                m_waterPipeline,
                m_viewCamera,
                &frustum,
                &m_env,
                &renderer().debugState(),
                waterLightsVa,
                waterLightCount,
                waterIndex,
                renderer().frameIndex(),
                heightHeap,
                heightGpu,
                &m_shadows,
                waterSceneColor,
                waterDepth,
                waterSsr,
                drawIndex);
            drawIndex = 1;
        }
        for (const SandboxStream& stream : m_streams)
        {
            if (drawIndex >= WaterPipeline::kMaxWaterDrawsPerFrame)
            {
                DE_LOG_WARN(LogCategory::Render, "SandboxApp: water draw cap {} reached", WaterPipeline::kMaxWaterDrawsPerFrame);
                break;
            }
            WaterParams streamParams = m_water.params();
            streamParams.flowSpeed = stream.flowSpeed;
            if (drawStreamRibbon(
                    cmd,
                    m_waterPipeline,
                    stream.mesh,
                    stream.bounds,
                    streamParams,
                    m_viewCamera,
                    &frustum,
                    &renderer().debugState(),
                    m_water.time(),
                    renderer().frameIndex(),
                    drawIndex,
                    heightHeap,
                    heightGpu,
                    &m_shadows,
                    waterSceneColor,
                    waterDepth,
                    waterSsr,
                    &m_terrain.coarse(),
                    &m_env))
                ++drawIndex;
        }
    }

    if (renderer().hasGBuffer())
    {
        renderer().bindHdrDepthRead();
        CloudVolumeFrame cf{};
        cf.sunDir       = m_env.lightDir();
        cf.sunColor     = m_env.lightColor();
        cf.ambientColor = m_env.ambientColor();
        cf.time         = m_cloudTime;
        cf.lod          = m_cloudLod;
        m_scene.drawCloudVolumes(cmd, renderer(), world(), m_viewCamera, viewProj, cf);
    }

    if (m_camouflage && renderer().hasSceneBuffers() && m_scene.camouflage().isValid())
    {
        const Entity body = possessedBody();
        const TransformComponent* xf = body.valid() ? world().get<TransformComponent>(body) : nullptr;
        const ModelComponent* mc = body.valid() ? world().get<ModelComponent>(body) : nullptr;
        const auto model = (mc && xf) ? assets().getAs<Model>(mc->modelAssetID) : AssetRef<Model>{};
        if (xf && model && gpu.ensureModel(model))
        {
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            AnimGraphComponent* ag = world().get<AnimGraphComponent>(body);
            const AnimPose* pose = model->skinned() ? skinnedPose(*model, ag) : nullptr;
            if (ag)
            {
                ag->prevWorld      = worldMat;
                ag->prevWorldValid = true;
            }
            m_scene.camouflage().drawModel(cmd, renderer(), gpu, m_skinRing, *model, pose, worldMat, viewProj, camPos, m_cloudTime, fill);
        }
    }

    {
        const GpuScope translucent(cmd, "Translucent", ProfileColor::Translucent);
        MeshFrameConstants lit{};
        lit.lightDirWS[0] = m_env.lightDir().x;
        lit.lightDirWS[1] = m_env.lightDir().y;
        lit.lightDirWS[2] = m_env.lightDir().z;
        lit.ambientScale  = 0.22f;
        {
            const Vector3f pbrSun = pbrSunLightColor(m_env.lightColor());
            lit.lightColor[0]     = pbrSun.x;
            lit.lightColor[1]     = pbrSun.y;
            lit.lightColor[2]     = pbrSun.z;
        }
        lit.cameraPos[0]  = camPos.x;
        lit.cameraPos[1]  = camPos.y;
        lit.cameraPos[2]  = camPos.z;
        lit.lighting      = renderer().debugState().lightingActive() ? 1.0f : 0.0f;
        world().each<ModelComponent>([&](Entity e, ModelComponent& mc) {
            if (camouflageHides(e))
                return;
            const TransformComponent* xf = world().get<TransformComponent>(e);
            const auto model = assets().getAs<Model>(mc.modelAssetID);
            if (!xf || xf->scale.MagnitudeSqrd() < 1.0e-12f || !model || !model->hasTranslucent() || !gpu.ensureModel(model))
                return;
            const Matrix4f worldMat = makeWorldMatrix(*xf);
            if (model->skinned())
            {
                const AnimPose* pose = skinnedPose(*model, world().get<AnimGraphComponent>(e));
                if (!pose)
                    return;
                drawSkinnedModelForward(cmd, gpu, m_skinnedTransparentPipeline, m_meshTransparentPipeline, m_shadows, m_skinRing, *model, true, *pose, worldMat, viewProj, lit, fill);
            }
            else
                drawModelForward(cmd, gpu, m_meshTransparentPipeline, m_shadows, *model, true, worldMat, viewProj, lit, fill);
        });
    }

    if (m_chaseOk)
    {
        const GpuScope paths(cmd, "Path Debug", ProfileColor::DebugOverlay);
        m_chase.drawPaths(cmd, renderer(), viewProj);
    }
    drawSkeletonOverlay(cmd, viewProj);
    m_scene.drawDecalVolumes(cmd, renderer(), m_chase.linePipeline(), viewProj);

    {
        const GpuScope particles(cmd, "Particles", ProfileColor::Particles);
        m_particles.beginFrame(renderer().frameIndex());
    if (m_blood.aliveCount() > 0)
        m_particles.draw(cmd, m_viewCamera, m_blood, false);
    world().each<ParticleEmitterComponent>([&](Entity, ParticleEmitterComponent& pe) {
        if (!pe.runtime)
            return;
        const Material* sprite = nullptr;
        if (AssetRef<Material> mat = assets().getAs<Material>(pe.matAssetID))
        {
            gpu.ensureMaterial(mat);
            sprite = mat.get();
        }
        m_particles.draw(cmd, m_viewCamera, *pe.runtime, pe.runtime->desc().additiveBlend, sprite);
    });
    if (WeaponLoadout* wFx = localWeapons(); wFx && wFx->projectile().impactEmitter().aliveCount() > 0)
        m_particles.draw(cmd, m_viewCamera, wFx->projectile().impactEmitter(), true);
    }

    if (!deferredDecals())
        m_bloodSplats.draw(cmd, m_viewCamera);

    {
        const bool aces = useAcesTonemap(renderer());
        TonemapSettings post = playerPostFx();
        post.mode            = aces ? 1.0f : 0.0f;
        float exposure       = m_env.exposure();
        m_autoExposureResult = {};
        m_autoExposureResult.finalExposure = exposure;
        const bool deferredMeter = renderer().scenePath() == ScenePath::HybridDeferred && m_scene.autoExposure().isValid();
        if (deferredMeter)
        {
            // Resize drops the 1×1. Zero the correction before this frame can apply a stale sample.
            if (m_scene.syncAutoExposureSize(renderer()))
                resetAutoExposure(m_autoExposureState);
            const float measured = m_autoExposure.mode == ExposureMode::Auto ? m_scene.autoExposure().readMeasuredLuma(renderer().frameIndex()) : 0.0f;
            m_autoExposureResult = adaptExposure(m_autoExposureState, measured, exposure, m_frameDt, m_autoExposure);
            exposure             = m_autoExposureResult.finalExposure;
        }
        post.exposure = exposure;
        m_scene.applyPost(renderer(), cmd, viewProj, post, deferredMeter && m_autoExposure.mode == ExposureMode::Auto);
    }

    m_scene.endCameraFrame(m_viewCamera, viewProj);

    Health* hudHp = localHealth();
    const Entity hudBody = possessedBody();
    const HudTagComponent* hudTag = hudBody.valid() ? world().get<HudTagComponent>(hudBody) : nullptr;
    const bool drawHealth = !m_menu.visible() && hudHp && hudTag && hudTag->kind == HudKind::HealthBar;
    const bool drawCrosshair = !m_menu.visible() && hudHp && hudHp->alive() && !m_gameplayPaused;
    if (drawHealth || drawCrosshair)
    {
        const GpuScope hud(cmd, "HUD", ProfileColor::Hud);
        if (drawHealth)
            m_healthHud.draw(cmd, renderer().width(), renderer().height(), hudHp->ratio());
        if (drawCrosshair)
            m_crosshair.draw(cmd, renderer().width(), renderer().height(), localWeapons() ? localWeapons()->activeKind() : WeaponKind::Melee);
    }

    renderer().stats().drawCalls = m_terrain.lastDrawCalls() + m_water.lastDrawCalls() + meshDraws + 1;
    renderer().stats().triangles =
        m_terrain.lastTriangles() + m_water.lastTriangles() + meshDraws * (m_cubeMesh.indexCount() / 3);

    drawDebugOverlays(cmd);
    m_menu.draw(renderer());
    if (m_imgui.isReady() && !m_menu.visible())
    {
        drawNpcInfoOverlay();
        if (m_gameplayPaused)
            drawPauseOverlay();
        if (m_showDevTools)
            drawDevTools();
        m_imgui.render(renderer());
    }
    else if (m_imgui.isReady() && m_menu.visible())
    {
        // ImGui frame was begun; submit an empty frame so the backend stays paired.
        m_imgui.render(renderer());
    }
    renderer().endFrame();
}

void SandboxApp::drawDebugOverlays(ID3D12GraphicsCommandList* cmd)
{
    if (!cmd || !m_debugOverlay.isValid())
        return;
    const bool ssaoTile = renderer().hasGBuffer() && renderer().debugState().ssaoDebug == 1;
    const bool ssrTile  = renderer().hasGBuffer() && renderer().debugState().ssrDebug != 0;
    if (!m_showShadowMaps && !renderer().debugState().shadowMapTiles && !m_showDepth && !m_showGBuffer && !m_showVelocity && !ssaoTile && !ssrTile)
        return;

    const GpuScope overlay(cmd, "Debug Overlay", ProfileColor::DebugOverlay);
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
        renderer().transitionAlbedo(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        renderer().transitionAttrib(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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
    if (ssaoTile)
    {
        m_scene.gtao().transitionAoFull(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const D3D12_CPU_DESCRIPTOR_HANDLE aoFull = m_scene.gtao().aoFullSrvCpu();
        if (aoFull.ptr != 0)
        {
            LONG x = pad;
            if (m_showGBuffer)
                x += 2 * (tile + 8);
            if (m_showGBuffer || m_showVelocity)
                x += tile + 8;
            m_debugOverlay.draw2D(cmd, renderer().device(), aoFull, x, pad, tile, tile, 1.0f, false);
        }
    }
    if (ssrTile)
    {
        LONG x = pad;
        if (m_showGBuffer)
            x += 2 * (tile + 8);
        if (m_showGBuffer || m_showVelocity)
            x += tile + 8;
        if (ssaoTile)
            x += tile + 8;
        if (renderer().debugState().ssrDebug == 1)
        {
            m_scene.ssr().transitionFull(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            const D3D12_CPU_DESCRIPTOR_HANDLE full = m_scene.ssr().fullSrvCpu();
            if (full.ptr != 0)
                m_debugOverlay.drawColor(cmd, renderer().device(), full, x, pad, tile, tile);
        }
        else
        {
            m_scene.ssr().transitionDebug(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            const D3D12_CPU_DESCRIPTOR_HANDLE conf = m_scene.ssr().debugConfSrvCpu();
            if (conf.ptr != 0)
                m_debugOverlay.draw2D(cmd, renderer().device(), conf, x, pad, tile, tile, 1.0f, false);
        }
    }

    if (m_showDepth && renderer().depthResource())
    {
        renderer().transitionDepth(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        const LONG x = pad;
        const LONG y = sh - pad - tile;
        m_debugOverlay.draw2D(
            cmd, renderer().device(), renderer().depthSrvCpu(), x, y, tile, tile, 1.0f, true);
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

    if (renderer().debugState().shadowMapTiles && m_scene.localShadows().isValid())
    {
        LocalShadowSystem& local = m_scene.localShadows();
        const int n = local.slicesPerFrame();
        const LONG gap = 8;
        const LONG y = sh - pad - tile - gap - tile;
        LONG x0 = pad;
        LONG tw = tile;
        if (n > 0)
        {
            const LONG need = n * tw + (n - 1) * gap;
            if (x0 + need > sw - pad)
            {
                const LONG avail = sw - pad - x0 - (n - 1) * gap;
                if (avail > 64)
                    tw = avail / n;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const LONG x = x0 + i * (tw + gap);
            m_debugOverlay.drawArray(
                cmd,
                renderer().device(),
                local.srvCpu(),
                x,
                y,
                tw,
                tile,
                local.debugSlice(i),
                1.25f,
                true);
        }
    }

    cmd->RSSetViewports(1, &renderer().viewport());
    cmd->RSSetScissorRects(1, &renderer().scissor());
}

void SandboxApp::onShutdown()
{
    network().shutdown();
    renderer().waitForGpu();
    m_foliagePipeline.destroy();
    m_menu.shutdown(renderer());
    m_scene.shutdown();
    m_imgui.shutdown(renderer());
    m_water = WaterWorld{};
    m_terrainMaterial = TerrainMaterial{};
    Physics::destroyFoliageCollision(m_physics, m_foliageCollision);
    m_physics.destroy();
    m_physicsGround = Physics::kNullPhysicsBody;
    m_terrain.clear();
    audio().stopAll();
    if (WeaponLoadout* w = localWeapons())
    {
        w->projectile().setAudio(nullptr, nullptr);
        w->clear();
    }
    m_particles.destroy(renderer());
    m_bloodSplats.destroy(renderer());
    DE_LOG_INFO("SandboxApp: shutdown");
}
