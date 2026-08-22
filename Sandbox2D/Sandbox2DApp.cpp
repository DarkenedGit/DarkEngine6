#include "Sandbox2DApp.h"

#include "Collision/Collision.h"
#include "Core/Log.h"
#include "Core/Paths.h"
#include "Geometry/MeshGen.h"
#include "Input/InputCodes.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Scene/SceneFile.h"
#include "Sprite/SpriteSheet.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace Dark;
using namespace Math;
using namespace Geometry;
using namespace Collision;

namespace
{

void copyMatrix(float dst[16], const Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

bool createChecker(
    Renderer& renderer,
    Texture2D& out,
    uint8_t r0, uint8_t g0, uint8_t b0,
    uint8_t r1, uint8_t g1, uint8_t b1,
    uint32_t size = 32,
    uint32_t cell = 8)
{
    std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4u);
    for (uint32_t y = 0; y < size; ++y)
    {
        for (uint32_t x = 0; x < size; ++x)
        {
            const bool alt = ((x / cell) + (y / cell)) & 1u;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4u;
            px[i + 0] = alt ? r1 : r0;
            px[i + 1] = alt ? g1 : g0;
            px[i + 2] = alt ? b1 : b0;
            px[i + 3] = 255;
        }
    }
    return out.createFromRGBA(renderer, px.data(), size, size, size * 4u);
}

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
    for (const fs::path& c : candidates)
    {
        std::error_code ec;
        if (!c.empty() && fs::exists(c, ec) && !ec && fs::is_directory(c, ec) && !ec)
            assets.mountDirectory(c);
    }
}

Aabb2f playerBounds(const Vector2f& pos, const Vector2f& half)
{
    return Aabb2f::FromCenterExtents(pos, half);
}

} // namespace

void Sandbox2DApp::registerActions()
{
    ActionMap& a = input().actions();
    a.clear();

    a.bindKey("quit", Key::Escape);
    a.bindButton("quit", GamepadButton::Back);

    a.bindKey("reset", Key::R);
    a.bindButton("reset", GamepadButton::Y);

    a.bindKey("jump", Key::Space);
    a.bindKey("jump", Key::W);
    a.bindKey("jump", Key::Up);
    a.bindButton("jump", GamepadButton::A);

    a.bindKeyAsAxis("move", Key::A, -1.0f);
    a.bindKeyAsAxis("move", Key::D, 1.0f);
    a.bindKeyAsAxis("move", Key::Left, -1.0f);
    a.bindKeyAsAxis("move", Key::Right, 1.0f);
    a.bindAxis("move", GamepadAxis::LeftX, 1.0f);
    a.bindButtonAsAxis("move", GamepadButton::DPadLeft, -1.0f);
    a.bindButtonAsAxis("move", GamepadButton::DPadRight, 1.0f);

    a.bindKey("debug", Key::F1);

    DE_LOG_INFO(
        "Sandbox2D: move(A/D / arrows / stick) jump(Space/W / A) reset(R/Y) debug(F1) quit(Esc)");
}

void Sandbox2DApp::buildLevel()
{
    m_platforms.clear();
    m_coins.clear();

    auto addPlat = [&](float x0, float y0, float x1, float y1) {
        Platform p;
        p.box = Aabb2f(Vector2f(x0, y0), Vector2f(x1, y1));
        m_platforms.push_back(p);
    };
    auto addCoin = [&](float x, float y) {
        Coin c;
        c.pos = Vector2f(x, y);
        m_coins.push_back(c);
    };

    // Ground with a pit in the middle.
    addPlat(0.0f, 0.0f, 28.0f, 1.4f);
    addPlat(36.0f, 0.0f, 96.0f, 1.4f);

    // Starter ledges
    addPlat(7.0f, 3.6f, 12.0f, 4.1f);
    addPlat(14.5f, 5.8f, 19.0f, 6.3f);
    addPlat(21.0f, 4.4f, 26.5f, 4.9f);

    // Across the pit
    addPlat(30.0f, 3.2f, 34.5f, 3.7f);

    // Right-side climb
    addPlat(42.0f, 3.4f, 47.0f, 3.9f);
    addPlat(50.0f, 5.6f, 57.0f, 6.1f);
    addPlat(60.0f, 7.6f, 66.0f, 8.1f);
    addPlat(70.0f, 5.2f, 76.0f, 5.7f);
    addPlat(80.0f, 3.6f, 88.0f, 4.1f);
    addPlat(88.0f, 7.2f, 94.0f, 7.7f);

    addCoin(9.5f, 4.7f);
    addCoin(16.8f, 6.9f);
    addCoin(23.8f, 5.5f);
    addCoin(32.2f, 4.3f);
    addCoin(53.5f, 6.7f);
    addCoin(63.0f, 8.7f);
    addCoin(73.0f, 6.3f);
    addCoin(91.0f, 8.3f);
}

bool Sandbox2DApp::tryLoadLevel()
{
    const std::filesystem::path path = defaultScenePath("level2d.json");
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec) || ec)
        return false;

    SceneFileData data{};
    std::string err;
    if (!loadSceneFromJson(path, data, &err) || data.mode != SceneMode::Scene2D)
    {
        if (!err.empty())
            DE_LOG_WARN("Sandbox2D: scene load skipped — {}", err);
        return false;
    }

    m_platforms.clear();
    m_coins.clear();
    m_worldMin = data.worldMin;
    m_worldMax = data.worldMax;

    bool haveSpawn = false;
    for (const SceneObjectData& o : data.objects)
    {
        if (o.type == SceneObjectType::Platform)
        {
            Platform p;
            p.box = Aabb2f::FromCenterExtents(
                Vector2f(o.position.x, o.position.y),
                Vector2f(0.5f * std::fabs(o.scale.x), 0.5f * std::fabs(o.scale.y)));
            m_platforms.push_back(p);
        }
        else if (o.type == SceneObjectType::Coin)
        {
            Coin c;
            c.pos = Vector2f(o.position.x, o.position.y);
            m_coins.push_back(c);
        }
        else if (o.type == SceneObjectType::Spawn && !haveSpawn)
        {
            m_spawn    = Vector2f(o.position.x, o.position.y);
            haveSpawn  = true;
        }
    }

    if (m_platforms.empty())
        return false;

    DE_LOG_INFO(
        "Sandbox2D: loaded {} platforms, {} coins from {}",
        m_platforms.size(),
        m_coins.size(),
        path.string());
    return true;
}

void Sandbox2DApp::destroyPhysics()
{
    if (B2_IS_NON_NULL(m_physWorld))
        b2DestroyWorld(m_physWorld);
    m_physWorld   = b2_nullWorldId;
    m_playerBody  = b2_nullBodyId;
    m_playerShape = b2_nullShapeId;
    m_physAccum   = 0.0f;
}

bool Sandbox2DApp::createPhysicsWorld()
{
    destroyPhysics();

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity    = { 0.0f, -38.0f };
    m_physWorld         = b2CreateWorld(&worldDef);
    if (!b2World_IsValid(m_physWorld))
    {
        DE_LOG_ERROR("Sandbox2D: Box2D world create failed");
        m_physWorld = b2_nullWorldId;
        return false;
    }

    createPlatformBodies();
    if (!createPlayerBody())
    {
        destroyPhysics();
        return false;
    }

    DE_LOG_INFO("Sandbox2D: Box2D world ready ({} platforms)", m_platforms.size());
    return true;
}

void Sandbox2DApp::createPlatformBodies()
{
    if (!b2World_IsValid(m_physWorld))
        return;

    b2ShapeDef shapeDef           = b2DefaultShapeDef();
    shapeDef.material.friction    = 0.6f;
    shapeDef.material.restitution = 0.0f;

    for (const Platform& p : m_platforms)
    {
        const Vector2f c = p.box.Center();
        const Vector2f h = p.box.Extents();
        if (h.x <= 0.0f || h.y <= 0.0f)
            continue;

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type      = b2_staticBody;
        bodyDef.position  = { c.x, c.y };
        const b2BodyId body = b2CreateBody(m_physWorld, &bodyDef);
        if (!b2Body_IsValid(body))
            continue;

        const b2Polygon box = b2MakeBox(h.x, h.y);
        b2CreatePolygonShape(body, &shapeDef, &box);
    }
}

bool Sandbox2DApp::createPlayerBody()
{
    if (!b2World_IsValid(m_physWorld))
        return false;

    b2BodyDef bodyDef          = b2DefaultBodyDef();
    bodyDef.type               = b2_dynamicBody;
    bodyDef.position           = { m_spawn.x, m_spawn.y };
    bodyDef.motionLocks.angularZ = true;
    bodyDef.enableSleep        = false;
    bodyDef.isAwake            = true;
    m_playerBody               = b2CreateBody(m_physWorld, &bodyDef);
    if (!b2Body_IsValid(m_playerBody))
    {
        DE_LOG_ERROR("Sandbox2D: player body create failed");
        m_playerBody = b2_nullBodyId;
        return false;
    }

    // Slightly rounded box so the player slides off platform corners instead of catching.
    constexpr float kRadius = 0.06f;
    const float hw = Max(m_player.half.x - kRadius, 0.08f);
    const float hh = Max(m_player.half.y - kRadius, 0.08f);

    b2ShapeDef shapeDef           = b2DefaultShapeDef();
    shapeDef.density              = 1.0f;
    shapeDef.material.friction    = 0.0f;
    shapeDef.material.restitution = 0.0f;
    shapeDef.enableContactEvents  = true;

    const b2Polygon hull = b2MakeRoundedBox(hw, hh, kRadius);
    m_playerShape        = b2CreatePolygonShape(m_playerBody, &shapeDef, &hull);
    if (!b2Shape_IsValid(m_playerShape))
    {
        DE_LOG_ERROR("Sandbox2D: player shape create failed");
        m_playerShape = b2_nullShapeId;
        return false;
    }
    return true;
}

void Sandbox2DApp::syncPlayerFromBody()
{
    if (!b2Body_IsValid(m_playerBody))
        return;
    const b2Vec2 p = b2Body_GetPosition(m_playerBody);
    const b2Vec2 v = b2Body_GetLinearVelocity(m_playerBody);
    m_player.pos   = Vector2f(p.x, p.y);
    m_player.vel   = Vector2f(v.x, v.y);
}

bool Sandbox2DApp::playerGrounded() const
{
    if (!b2Body_IsValid(m_playerBody))
        return false;

    const int capacity = b2Body_GetContactCapacity(m_playerBody);
    if (capacity <= 0)
        return false;

    b2ContactData contacts[8];
    const int n = b2Body_GetContactData(m_playerBody, contacts, 8);
    for (int i = 0; i < n; ++i)
    {
        if (contacts[i].manifold.pointCount <= 0)
            continue;

        b2Vec2 nrm = contacts[i].manifold.normal;
        if (B2_ID_EQUALS(contacts[i].shapeIdA, m_playerShape))
        {
            nrm.x = -nrm.x;
            nrm.y = -nrm.y;
        }
        // Normal from platform to player. Up-facing contact is ground.
        if (nrm.y > 0.55f)
            return true;
    }
    return false;
}

void Sandbox2DApp::applyPlayerControl(float dt)
{
    if (!b2Body_IsValid(m_playerBody))
        return;

    const float move = input().actionAxis("move");
    if (std::fabs(move) > 0.15f)
        m_player.facing = (move > 0.0f) ? 1.0f : -1.0f;

    constexpr float kMaxRun   = 8.5f;
    constexpr float kAccel    = 48.0f;
    constexpr float kFriction = 36.0f;
    constexpr float kJumpVel  = 14.0f;
    constexpr float kCoyote   = 0.10f;
    constexpr float kBuffer   = 0.10f;
    constexpr float kMaxFall  = -22.0f;

    b2Vec2 vel = b2Body_GetLinearVelocity(m_playerBody);

    if (std::fabs(move) > 0.05f)
        vel.x += move * kAccel * dt;
    else if (m_player.grounded)
    {
        if (vel.x > 0.0f)
            vel.x = Max(0.0f, vel.x - kFriction * dt);
        else
            vel.x = Min(0.0f, vel.x + kFriction * dt);
    }
    vel.x = Clamp(vel.x, -kMaxRun, kMaxRun);

    if (vel.y < kMaxFall)
        vel.y = kMaxFall;

    if (input().actionPressed("jump"))
        m_player.jumpBuffer = kBuffer;

    if (m_player.grounded)
        m_player.coyote = kCoyote;

    if (m_player.jumpBuffer > 0.0f && m_player.coyote > 0.0f)
    {
        vel.y               = kJumpVel;
        m_player.grounded   = false;
        m_player.coyote     = 0.0f;
        m_player.jumpBuffer = 0.0f;
    }

    m_player.jumpBuffer = Max(0.0f, m_player.jumpBuffer - dt);
    m_player.coyote     = Max(0.0f, m_player.coyote - dt);

    b2Body_SetLinearVelocity(m_playerBody, vel);
}

void Sandbox2DApp::resetPlayer()
{
    m_player.pos         = m_spawn;
    m_player.vel         = Vector2f(0.0f, 0.0f);
    m_player.grounded    = false;
    m_player.facing      = 1.0f;
    m_player.coyote      = 0.0f;
    m_player.jumpBuffer  = 0.0f;
    m_player.wasGrounded = false;
    m_physAccum          = 0.0f;
    m_playerAnim.play("idle", true);

    if (b2Body_IsValid(m_playerBody))
    {
        b2Body_SetTransform(m_playerBody, b2Vec2{ m_spawn.x, m_spawn.y }, b2Rot_identity);
        b2Body_SetLinearVelocity(m_playerBody, b2Vec2_zero);
        b2Body_SetAngularVelocity(m_playerBody, 0.0f);
        b2Body_SetAwake(m_playerBody, true);
    }
}

void Sandbox2DApp::onInit()
{
    DE_LOG_INFO("Sandbox2D: init");
    mountContentRoots(assets());
    registerActions();

    renderer().setClearColor(0.38f, 0.62f, 0.86f, 1.0f);

    if (!m_spritePipe.create(renderer().device()))
    {
        DE_LOG_FATAL("Sandbox2D: SpritePipeline create failed");
        return;
    }
    if (!m_linePipe.create(renderer().device()))
    {
        DE_LOG_FATAL("Sandbox2D: LinePipeline create failed");
        return;
    }

    m_quad       = Mesh::Create(renderer(), CreateQuadXY(1.0f, 1.0f));
    m_boxOutline = LineMesh::Create(renderer(), CreateBoxOutlineXY());
    if (!m_quad.valid())
    {
        DE_LOG_FATAL("Sandbox2D: quad mesh failed");
        return;
    }

    std::vector<SpriteClip> clips;
    if (loadSpriteSetFromJson(renderer(), assets(), "sprites/player.json", m_playerSheets, clips))
    {
        DE_LOG_INFO("Sandbox2D: imported {} player clip textures", m_playerSheets.size());
        m_playerAnim.setSheet(nullptr);
        m_playerAnim.clearClips();
        for (size_t i = 0; i < clips.size(); ++i)
        {
            const SpriteSheet* sheet = (i < m_playerSheets.size() && m_playerSheets[i].valid())
                ? &m_playerSheets[i]
                : nullptr;
            m_playerAnim.addClip(clips[i], sheet);
        }
    }
    else
    {
        if (!m_playerSheet.createProceduralHero(renderer()))
        {
            DE_LOG_FATAL("Sandbox2D: player sprite sheet failed");
            return;
        }
        clips = defaultHeroClips();
        m_playerAnim.setSheet(&m_playerSheet);
        m_playerAnim.clearClips();
        for (const SpriteClip& clip : clips)
            m_playerAnim.addClip(clip);
        DE_LOG_INFO("Sandbox2D: using procedural hero flipbook");
    }
    m_playerAnim.play("idle", true);

    if (!createChecker(renderer(), m_texPlatform, 118, 86, 52, 92, 66, 40)
        || !m_texCoin.createSolidColor(renderer(), 236, 196, 64)
        || !createChecker(renderer(), m_texHillFar, 62, 92, 128, 54, 80, 116, 32, 16)
        || !createChecker(renderer(), m_texHillMid, 72, 122, 78, 58, 102, 64, 32, 16)
        || !m_texWhite.createSolidColor(renderer(), 255, 255, 255))
    {
        DE_LOG_FATAL("Sandbox2D: textures failed");
        return;
    }

    const float aspect = (renderer().height() > 0)
        ? static_cast<float>(renderer().width()) / static_cast<float>(renderer().height())
        : 16.0f / 9.0f;
    m_camera.SetViewportSize(static_cast<float>(renderer().width()), static_cast<float>(renderer().height()));
    m_camera.SetOrthoHeight(12.0f);
    m_camera.SetAspect(aspect);
    m_camera.SetClipPlanes(0.0f, 80.0f);
    m_camera.SetZoom(1.0f);

    if (!tryLoadLevel())
        buildLevel();
    if (!createPhysicsWorld())
    {
        DE_LOG_FATAL("Sandbox2D: physics init failed");
        return;
    }
    resetPlayer();
    m_camera.SetPosition(m_player.pos.x, m_player.pos.y + 1.0f);
    m_score = 0;

    DE_LOG_INFO(
        "Sandbox2D: {} platforms, {} coins, camera ortho height {:.1f}",
        m_platforms.size(),
        m_coins.size(),
        m_camera.GetOrthoHeight());
}

void Sandbox2DApp::updatePlayer(float dt)
{
    if (!b2World_IsValid(m_physWorld) || !b2Body_IsValid(m_playerBody))
        return;

    constexpr float kStep    = 1.0f / 60.0f;
    constexpr int   kSubStep = 4;
    constexpr int   kMaxSteps = 5;

    applyPlayerControl(dt);

    m_physAccum += dt;
    int steps = 0;
    while (m_physAccum >= kStep && steps < kMaxSteps)
    {
        b2World_Step(m_physWorld, kStep, kSubStep);
        m_physAccum -= kStep;
        ++steps;
    }
    if (steps == kMaxSteps)
        m_physAccum = 0.0f;

    syncPlayerFromBody();
    m_player.grounded = playerGrounded();

    const Aabb2f coinHit = playerBounds(m_player.pos, m_player.half);
    for (Coin& c : m_coins)
    {
        if (c.collected)
            continue;
        const Aabb2f coinBox = Aabb2f::FromCenterExtents(c.pos, Vector2f(0.28f, 0.28f));
        if (Intersects(coinHit, coinBox))
        {
            c.collected = true;
            ++m_score;
            DE_LOG_INFO("Sandbox2D: coin +1  score {}", m_score);
        }
    }

    if (m_player.pos.y < -6.0f)
    {
        DE_LOG_INFO("Sandbox2D: fell — respawn");
        resetPlayer();
    }
}

void Sandbox2DApp::updatePlayerAnim(float dt)
{
    const char* clip = "idle";
    if (!m_player.grounded)
        clip = "jump";
    else if (std::fabs(m_player.vel.x) > 0.45f)
        clip = "run";

    const bool restartJump = (!m_player.grounded && m_player.wasGrounded);
    m_playerAnim.play(clip, restartJump);
    m_playerAnim.update(dt);
    m_player.wasGrounded = m_player.grounded;
}

void Sandbox2DApp::updateCamera(float dt)
{
    m_camera.SetViewportSize(static_cast<float>(renderer().width()), static_cast<float>(renderer().height()));

    const float wheel = input().mouseWheel();
    if (wheel != 0.0f)
        m_camera.ZoomBy(wheel > 0.0f ? 1.12f : 1.0f / 1.12f);

    Vector2f target(m_player.pos.x + m_player.facing * 2.4f, m_player.pos.y + 1.1f);
    const float blend = 1.0f - std::exp(-7.0f * dt);
    Vector2f cam = m_camera.GetPosition();
    cam.x += (target.x - cam.x) * blend;
    cam.y += (target.y - cam.y) * blend;

    const float halfW = m_camera.GetVisibleWidth() * 0.5f;
    const float halfH = m_camera.GetVisibleHeight() * 0.5f;
    cam.x = Clamp(cam.x, m_worldMin.x + halfW, m_worldMax.x - halfW);
    cam.y = Clamp(cam.y, m_worldMin.y + halfH * 0.35f, m_worldMax.y - halfH);
    m_camera.SetPosition(cam);
}

void Sandbox2DApp::onUpdate(float dt)
{
    if (input().actionPressed("quit"))
    {
        requestQuit();
        return;
    }
    if (input().actionPressed("reset"))
    {
        for (Coin& c : m_coins)
            c.collected = false;
        m_score = 0;
        resetPlayer();
        DE_LOG_INFO("Sandbox2D: reset");
    }
    if (input().actionPressed("debug"))
    {
        m_showCollision = !m_showCollision;
        DE_LOG_INFO("Sandbox2D: collision debug {}", m_showCollision);
    }

    updatePlayer(dt);
    updatePlayerAnim(dt);
    updateCamera(dt);
}

void Sandbox2DApp::drawSprite(
    ID3D12GraphicsCommandList* cmd,
    const Texture2D& texture,
    const Vector2f& pos,
    const Vector2f& size,
    float z,
    float tintR,
    float tintG,
    float tintB,
    float tintA,
    float uvScaleX,
    float uvScaleY,
    float flipX,
    float uvOffX,
    float uvOffY)
{
    if (!cmd || !texture.valid() || !m_quad.valid())
        return;

    const float sx = size.x * flipX;
    const Matrix4f world = Matrix4f::ScaleMatrixXYZ(sx, size.y, 1.0f)
        * Matrix4f::TranslationMatrix(pos.x, pos.y, z);
    const Matrix4f wvp = world * m_camera.GetViewProj();

    SpriteConstants cb{};
    copyMatrix(cb.worldViewProj, wvp);
    cb.color[0]    = tintR;
    cb.color[1]    = tintG;
    cb.color[2]    = tintB;
    cb.color[3]    = tintA;
    cb.uvScale[0]  = uvScaleX;
    cb.uvScale[1]  = uvScaleY;
    cb.uvOffset[0] = uvOffX;
    cb.uvOffset[1] = uvOffY;

    texture.bind(cmd, SpritePipeline::kRootAlbedoSrv);
    m_spritePipe.setConstants(cmd, cb);
    m_quad.draw(cmd);
}

void Sandbox2DApp::onRender()
{
    renderer().beginFrame();
    auto* cmd = renderer().commandList();

    m_spritePipe.bind(cmd);

    const Vector2f cam = m_camera.GetPosition();

    // Far / mid parallax hills (scroll slower than the camera).
    drawSprite(cmd, m_texHillFar, Vector2f(cam.x * 0.12f + 20.0f, 4.0f), Vector2f(70.0f, 10.0f), 40.0f,
               1, 1, 1, 1, 8.0f, 1.4f);
    drawSprite(cmd, m_texHillFar, Vector2f(cam.x * 0.12f + 70.0f, 3.2f), Vector2f(50.0f, 8.0f), 39.0f,
               1, 1, 1, 1, 6.0f, 1.2f);
    drawSprite(cmd, m_texHillMid, Vector2f(cam.x * 0.35f + 12.0f, 2.4f), Vector2f(36.0f, 6.0f), 25.0f,
               1, 1, 1, 1, 5.0f, 1.0f);
    drawSprite(cmd, m_texHillMid, Vector2f(cam.x * 0.35f + 48.0f, 2.0f), Vector2f(40.0f, 5.2f), 24.0f,
               1, 1, 1, 1, 5.5f, 0.9f);

    for (const Platform& p : m_platforms)
    {
        const Vector2f c = p.box.Center();
        const Vector2f s = p.box.Size();
        drawSprite(cmd, m_texPlatform, c, s, p.z, 1, 1, 1, 1, s.x, s.y);
    }

    for (const Coin& c : m_coins)
    {
        if (c.collected)
            continue;
        drawSprite(cmd, m_texCoin, c.pos, Vector2f(0.45f, 0.45f), 1.2f, 1, 1, 1, 1, 1, 1);
    }

    if (const Texture2D* playerTex = m_playerAnim.currentTexture())
    {
        const SpriteUvRect uv = m_playerAnim.currentUv();
        const SpriteSheet* sheet = m_playerAnim.currentSheet();
        const float fw = (sheet && sheet->frameWidth() > 0) ? static_cast<float>(sheet->frameWidth()) : 32.0f;
        const float fh = (sheet && sheet->frameHeight() > 0) ? static_cast<float>(sheet->frameHeight()) : 32.0f;
        constexpr float kSpriteHeight = 1.50f;
        const Vector2f playerSize(kSpriteHeight * (fw / fh), kSpriteHeight);
        const Vector2f spritePos(m_player.pos.x, m_player.pos.y - m_player.half.y + kSpriteHeight * 0.5f);
        drawSprite(
            cmd,
            *playerTex,
            spritePos,
            playerSize,
            1.0f,
            1,
            1,
            1,
            1,
            uv.du,
            uv.dv,
            m_player.facing,
            uv.u,
            uv.v);
    }

    if (m_showCollision && m_boxOutline.valid())
    {
        m_linePipe.bind(cmd);
        auto drawBox = [&](const Aabb2f& box, float r, float g, float b) {
            const Vector2f c = box.Center();
            const Vector2f s = box.Size();
            const Matrix4f world = Matrix4f::ScaleMatrixXYZ(s.x, s.y, 1.0f)
                * Matrix4f::TranslationMatrix(c.x, c.y, 0.4f);
            const Matrix4f wvp = world * m_camera.GetViewProj();
            LineFrameConstants lc{};
            copyMatrix(lc.worldViewProj, wvp);
            lc.color[0] = r;
            lc.color[1] = g;
            lc.color[2] = b;
            lc.color[3] = 1.0f;
            m_linePipe.setConstants(cmd, lc);
            m_boxOutline.draw(cmd);
        };

        drawBox(playerBounds(m_player.pos, m_player.half), 0.2f, 1.0f, 0.4f);
        for (const Platform& p : m_platforms)
            drawBox(p.box, 1.0f, 0.85f, 0.2f);
        for (const Coin& c : m_coins)
        {
            if (c.collected)
                continue;
            drawBox(Aabb2f::FromCenterExtents(c.pos, Vector2f(0.28f, 0.28f)), 1.0f, 0.9f, 0.2f);
        }
    }

    renderer().stats().drawCalls = static_cast<uint32_t>(m_platforms.size() + m_coins.size() + 6);
    renderer().endFrame();
}

void Sandbox2DApp::onShutdown()
{
    renderer().waitForGpu();
    destroyPhysics();
    m_playerAnim.setSheet(nullptr);
    m_playerAnim.clearClips();
    m_playerSheets.clear();
    m_playerSheet = SpriteSheet{};
    m_texPlatform = Texture2D{};
    m_texCoin     = Texture2D{};
    m_texHillFar  = Texture2D{};
    m_texHillMid  = Texture2D{};
    m_texWhite    = Texture2D{};
    m_quad        = Mesh{};
    m_boxOutline  = LineMesh{};
    DE_LOG_INFO("Sandbox2D: shutdown (score {})", m_score);
}
