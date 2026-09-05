#include "Sandbox2DApp.h"

#include "Collision/Collision.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Input/InputCodes.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Network/NetTypes.h"
#include "Render/MeshGen.h"
#include "Scene/SceneFile.h"
#include "Sprite/SpriteSheet.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace Dark;
using namespace Math;
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
    for (const fs::path& c : contentRootCandidates())
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

float facingFromRotation(const Quaternion& q)
{
    const Vector3f right = q.Rotate(Vector3f::X_AXIS);
    return right.x >= 0.0f ? 1.0f : -1.0f;
}

Quaternion rotationFromFacing(float facing)
{
    return Quaternion::FromAxisAngle(Vector3f::Y_AXIS, facing < 0.0f ? Pi : 0.0f);
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
    a.bindButton("jump", GamepadButton::B);

    a.bindKeyAsAxis("move", Key::A, -1.0f);
    a.bindKeyAsAxis("move", Key::D, 1.0f);
    a.bindKeyAsAxis("move", Key::Left, -1.0f);
    a.bindKeyAsAxis("move", Key::Right, 1.0f);
    a.bindAxis("move", GamepadAxis::LeftX, 1.0f);
    a.bindButtonAsAxis("move", GamepadButton::DPadLeft, -1.0f);
    a.bindButtonAsAxis("move", GamepadButton::DPadRight, 1.0f);

    a.bindKey("debug", Key::F1);
    a.bindButton("debug", GamepadButton::Start);

    a.bindKey("net_disconnect", Key::F4);
    a.bindKey("net_host", Key::F5);
    a.bindKey("net_join", Key::F6);
    a.bindKey("debug_listen", Key::F10);

    DE_LOG_INFO(
        "Sandbox2D: move(A/D / arrows / LS / D-pad) jump(Space/W / A/B) reset(R/Y) debug(F1/Start) quit(Esc/Back)");
    DE_LOG_INFO(LogCategory::Networking, "Sandbox2D net: F5 host :26160  F6 join 127.0.0.1:26160  F4 disconnect  F10 visual debugger");
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
    for (Platform& p : m_platforms)
        p.body = b2_nullBodyId;
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

void Sandbox2DApp::addPlatformBody(Platform& p)
{
    if (!b2World_IsValid(m_physWorld) || b2Body_IsValid(p.body))
        return;

    const Vector2f c = p.box.Center();
    const Vector2f h = p.box.Extents();
    if (h.x <= 0.0f || h.y <= 0.0f)
        return;

    b2ShapeDef shapeDef           = b2DefaultShapeDef();
    shapeDef.material.friction    = 0.6f;
    shapeDef.material.restitution = 0.0f;

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type      = b2_staticBody;
    bodyDef.position  = { c.x, c.y };
    const b2BodyId body = b2CreateBody(m_physWorld, &bodyDef);
    if (!b2Body_IsValid(body))
        return;

    const b2Polygon box = b2MakeBox(h.x, h.y);
    b2CreatePolygonShape(body, &shapeDef, &box);
    p.body = body;
}

void Sandbox2DApp::createPlatformBodies()
{
    if (!b2World_IsValid(m_physWorld))
        return;
    for (Platform& p : m_platforms)
        addPlatformBody(p);
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
        audio().play2D(m_sfxJump, 0.7f);
    }

    m_player.jumpBuffer = Max(0.0f, m_player.jumpBuffer - dt);
    m_player.coyote     = Max(0.0f, m_player.coyote - dt);

    // Client PawnState is rejected above kNetPawnMaxSpeed; hypot(run, fall -22) exceeds it.
    // 0.9 leaves slack for 20 Hz wall-clock jitter on the host (early packet → dist/dt > 20).
    if (network().role() == NetRole::Client)
    {
        constexpr float kSendSpeed = kNetPawnMaxSpeed * 0.9f;
        const float     spd        = std::sqrt(vel.x * vel.x + vel.y * vel.y);
        if (spd > kSendSpeed && spd > 0.0f)
        {
            const float s = kSendSpeed / spd;
            vel.x *= s;
            vel.y *= s;
        }
    }

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
    // Client Transform is speed-clamped in updatePlayer so R/fall teleports are not rejected.
    if (network().role() != NetRole::Client)
        syncLocalPawnTransform(0.0f);
}

void Sandbox2DApp::registerLevelEntities()
{
    for (Platform& p : m_platforms)
    {
        if (p.entity.valid() && world().alive(p.entity) && world().has<NetworkedComponent>(p.entity))
            continue;

        Entity e = world().createEntity();
        const Vector2f c = p.box.Center();
        const Vector2f s = p.box.Size();
        TransformComponent xf{};
        xf.position = Vector3f(c.x, c.y, 0.0f);
        xf.scale    = Vector3f(s.x, s.y, 1.0f);
        world().emplace<TagComponent>(e, "Platform");
        world().emplace<TransformComponent>(e, xf);
        if (!network().registerEntity(world(), e, NetPrefab::Platform))
        {
            world().destroyEntity(e);
            DE_LOG_WARN(LogCategory::Networking, "Sandbox2D: failed to register platform");
            continue;
        }
        if (NetworkedComponent* nc = world().get<NetworkedComponent>(e))
            nc->replicateTransform = false;
        p.entity = e;
    }

    for (Coin& c : m_coins)
    {
        if (c.entity.valid() && world().alive(c.entity) && world().has<NetworkedComponent>(c.entity))
            continue;

        Entity e = world().createEntity();
        TransformComponent xf{};
        xf.position = Vector3f(c.pos.x, c.pos.y, 0.0f);
        xf.scale    = Vector3f(0.45f, 0.45f, 1.0f);
        world().emplace<TagComponent>(e, "Coin");
        world().emplace<TransformComponent>(e, xf);
        if (!network().registerEntity(world(), e, NetPrefab::Coin))
        {
            world().destroyEntity(e);
            DE_LOG_WARN(LogCategory::Networking, "Sandbox2D: failed to register coin");
            continue;
        }
        if (NetworkedComponent* nc = world().get<NetworkedComponent>(e))
            nc->replicateTransform = false;
        c.entity = e;
    }
}

void Sandbox2DApp::createLocalPlayerEntity()
{
    if (m_playerEntity.valid() && world().alive(m_playerEntity))
        return;

    Entity e = world().createEntity();
    TransformComponent xf{};
    xf.position = Vector3f(m_player.pos.x, m_player.pos.y, 0.0f);
    xf.rotation = rotationFromFacing(m_player.facing);
    world().emplace<TagComponent>(e, "Player2D");
    world().emplace<TransformComponent>(e, xf);
    if (!network().registerEntity(world(), e, NetPrefab::Player2D, ClientId::Host, pawnPaletteColor(ClientId::Host)))
    {
        world().destroyEntity(e);
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox2D: failed to register local Player2D");
        return;
    }
    m_playerEntity = e;
}

void Sandbox2DApp::unregisterIdleReplicas()
{
    std::vector<Entity> stale;
    world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
        if (nc.netId == NULL_NET_ID)
            stale.push_back(e);
    });
    for (Entity e : stale)
        network().unregisterEntity(world(), e);
}

void Sandbox2DApp::restoreLocalLevel()
{
    m_remotePawns.clear();
    m_playerEntity = {};
    m_platforms.clear();
    m_coins.clear();
    destroyPhysics();
    if (!tryLoadLevel())
        buildLevel();
    if (!createPhysicsWorld())
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox2D: physics restore failed");
    resetPlayer();
    registerLevelEntities();
    createLocalPlayerEntity();
    m_score = 0;
}

Entity Sandbox2DApp::findPawn(ClientId owner)
{
    Entity found{};
    world().each<NetworkedComponent>([&](Entity e, NetworkedComponent& nc) {
        if (!found.valid() && nc.prefab == NetPrefab::Player2D && nc.owner == owner)
            found = e;
    });
    return found;
}

void Sandbox2DApp::spawnOwnedPawn(ClientId owner, float offsetX)
{
    if (findPawn(owner).valid())
        return;

    const Vector2f pos(m_spawn.x + offsetX, m_spawn.y);
    Entity e = world().createEntity();
    TransformComponent xf{};
    xf.position = Vector3f(pos.x, pos.y, 0.0f);
    world().emplace<TagComponent>(e, "Player2D");
    world().emplace<TransformComponent>(e, xf);
    const uint32_t color = pawnPaletteColor(owner);
    if (!network().registerEntity(world(), e, NetPrefab::Player2D, owner, color))
    {
        world().destroyEntity(e);
        DE_LOG_ERROR(LogCategory::Networking, "Sandbox2D: failed to register pawn for client {}", static_cast<unsigned>(owner));
        return;
    }

    if (owner == ClientId::Host)
    {
        m_playerEntity = e;
        m_player.pos   = pos;
        return;
    }

    RemotePawn rp{};
    rp.entity      = e;
    rp.colorRgba8  = color;
    m_remotePawns.push_back(rp);
}

void Sandbox2DApp::syncLocalPawnTransform(float dt)
{
    Entity pawn = m_playerEntity.valid() ? m_playerEntity : network().localPawn();
    if (!pawn.valid())
        return;
    TransformComponent* xf = world().get<TransformComponent>(pawn);
    if (!xf)
        return;

    const Vector3f target(m_player.pos.x, m_player.pos.y, 0.0f);
    if (network().role() == NetRole::Client && dt > 0.0f)
    {
        const Vector3f delta   = target - xf->position;
        const float    dist    = delta.Magnitude();
        const float    maxStep = kNetPawnMaxSpeed * 0.9f * dt;
        if (dist > maxStep && maxStep > 0.0f)
            xf->position += delta * (maxStep / dist);
        else
            xf->position = target;
    }
    else
        xf->position = target;
    xf->rotation = rotationFromFacing(m_player.facing);
}

bool Sandbox2DApp::ensureClientPhysics()
{
    if (network().role() != NetRole::Client)
        return b2World_IsValid(m_physWorld);
    if (!network().localPawn().valid())
        return false;
    if (b2Body_IsValid(m_playerBody))
        return true;

    if (const TransformComponent* xf = world().get<TransformComponent>(network().localPawn()))
    {
        m_spawn       = Vector2f(xf->position.x, xf->position.y);
        m_player.pos  = m_spawn;
        m_player.facing = facingFromRotation(xf->rotation);
    }
    return createPhysicsWorld();
}

void Sandbox2DApp::collectCoinsHostAuthority()
{
    const NetRole role = network().role();
    if (role == NetRole::Client)
        return;

    auto overlaps = [&](const Vector2f& pos) {
        const Aabb2f hit = playerBounds(pos, m_player.half);
        std::vector<Entity> eaten;
        for (Coin& c : m_coins)
        {
            if (c.collected)
                continue;
            const Aabb2f coinBox = Aabb2f::FromCenterExtents(c.pos, Vector2f(0.28f, 0.28f));
            if (!Intersects(hit, coinBox))
                continue;
            ++m_score;
            audio().play3D(m_sfxCoin, Vector3f(c.pos.x, c.pos.y, 0.0f), 0.8f);
            DE_LOG_INFO("Sandbox2D: coin +1  score {}", m_score);
            if (role == NetRole::Host && c.entity.valid())
                eaten.push_back(c.entity);
            else
                c.collected = true;
        }
        for (Entity e : eaten)
            network().unregisterEntity(world(), e);
    };

    overlaps(m_player.pos);
    if (role != NetRole::Host)
        return;
    for (const RemotePawn& rp : m_remotePawns)
    {
        const TransformComponent* xf = world().get<TransformComponent>(rp.entity);
        if (xf)
            overlaps(Vector2f(xf->position.x, xf->position.y));
    }
}

void Sandbox2DApp::handleNetHotkeys()
{
    if (input().actionPressed("net_host"))
    {
        if (network().host(kNetDefaultPort))
            DE_LOG_INFO(LogCategory::Networking, "Sandbox2D: hosting on port {}", kNetDefaultPort);
    }
    if (input().actionPressed("net_join"))
    {
        Address addr{};
        addr.port = kNetDefaultPort;
        parseIPv4("127.0.0.1", addr);
        if (network().join(addr))
            DE_LOG_INFO(LogCategory::Networking, "Sandbox2D: joining 127.0.0.1:{}", kNetDefaultPort);
    }
    if (input().actionPressed("net_disconnect"))
    {
        network().disconnect();
        DE_LOG_INFO(LogCategory::Networking, "Sandbox2D: disconnect");
    }
    if (input().actionPressed("debug_listen"))
    {
        if (debug().isListening())
        {
            debug().shutdown();
            DE_LOG_INFO(LogCategory::Debug, "Sandbox2D: Visual Debugger listen stopped");
        }
        else if (debug().listen(kDebugDefaultPort))
            DE_LOG_INFO(LogCategory::Debug, "Sandbox2D: Visual Debugger listening TCP {}", debug().boundAddress().port);
        else
            DE_LOG_ERROR(LogCategory::Debug, "Sandbox2D: Visual Debugger listen failed");
    }
}

void Sandbox2DApp::applyNetRole()
{
    const NetRole role = network().role();
    if (role != m_netRole)
    {
        DE_LOG_INFO(
            LogCategory::Networking,
            "Sandbox2D: role {} peers {} rtt {:.1f}ms pkts in/out {}/{}",
            netRoleName(role),
            network().peerCount(),
            network().rttMs(network().localClientId()),
            network().packetsIn(),
            network().packetsOut());

        if (role == NetRole::Client)
            destroyPhysics();
        else if (role == NetRole::Idle && m_netRole == NetRole::Client)
            restoreLocalLevel();

        m_netRole = role;
    }

    if (role == NetRole::Client)
        unregisterIdleReplicas();

    if (role == NetRole::Host && !network().localPawn().valid())
        spawnOwnedPawn(ClientId::Host, 0.0f);

    if (role == NetRole::Client)
        ensureClientPhysics();
}

bool Sandbox2DApp::onNetSpawn(World& world, Entity e, NetPrefab prefab, const TransformComponent& xf, uint32_t colorRgba8, void* user)
{
    auto* app = static_cast<Sandbox2DApp*>(user);
    if (!app || !e.valid())
        return false;

    if (prefab == NetPrefab::Platform)
    {
        Platform p;
        p.box = Aabb2f::FromCenterExtents(
            Vector2f(xf.position.x, xf.position.y),
            Vector2f(0.5f * std::fabs(xf.scale.x), 0.5f * std::fabs(xf.scale.y)));
        p.entity = e;
        app->m_platforms.push_back(p);
        app->addPlatformBody(app->m_platforms.back());
        return true;
    }
    if (prefab == NetPrefab::Coin)
    {
        Coin c;
        c.pos    = Vector2f(xf.position.x, xf.position.y);
        c.entity = e;
        app->m_coins.push_back(c);
        return true;
    }
    if (prefab == NetPrefab::Player2D)
    {
        const NetworkedComponent* nc = world.get<NetworkedComponent>(e);
        const ClientId owner = nc ? nc->owner : ClientId::Invalid;
        if (owner == app->network().localClientId())
        {
            app->m_playerEntity = e;
            app->m_player.pos     = Vector2f(xf.position.x, xf.position.y);
            app->m_player.facing  = facingFromRotation(xf.rotation);
            app->ensureClientPhysics();
            return true;
        }
        for (const RemotePawn& rp : app->m_remotePawns)
        {
            if (rp.entity == e)
                return true;
        }
        RemotePawn rp{};
        rp.entity     = e;
        rp.colorRgba8 = colorRgba8;
        app->m_remotePawns.push_back(rp);
        return true;
    }
    return true;
}

void Sandbox2DApp::onNetDespawn(World&, Entity e, NetId, void* user)
{
    auto* app = static_cast<Sandbox2DApp*>(user);
    if (!app)
        return;

    if (app->m_playerEntity == e)
        app->m_playerEntity = {};

    std::erase_if(app->m_platforms, [&](Platform& p) {
        if (p.entity != e)
            return false;
        if (b2Body_IsValid(p.body) && b2World_IsValid(app->m_physWorld))
            b2DestroyBody(p.body);
        p.body = b2_nullBodyId;
        return true;
    });
    std::erase_if(app->m_coins, [&](const Coin& c) { return c.entity == e; });
    std::erase_if(app->m_remotePawns, [&](const RemotePawn& rp) { return rp.entity == e; });
}

void Sandbox2DApp::onNetPeer(const NetPeerInfo& info, NetPeerEvent event, void* user)
{
    auto* app = static_cast<Sandbox2DApp*>(user);
    if (!app)
        return;

    if (event == NetPeerEvent::Joined && info.wantsPawn)
        app->spawnOwnedPawn(info.id, 1.5f * static_cast<float>(static_cast<uint8_t>(info.id)));
    else if (event == NetPeerEvent::Left)
    {
        const Entity pawn = app->findPawn(info.id);
        if (pawn.valid())
            app->network().unregisterEntity(app->world(), pawn);
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
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;
    if (!m_linePipe.create(renderer().device()))
    {
        DE_LOG_FATAL("Sandbox2D: LinePipeline create failed");
        requestQuit();
        return;
    }
    if (!pumpBootFrame())
        return;

    {
        MeshData mesh_data;
        CreateQuadXY(mesh_data, 1.0f, 1.0f);
        m_quad = Mesh::Create(renderer(), mesh_data);
    }
    {
        LineMeshData outline;
        CreateBoxOutlineXY(outline);
        m_boxOutline = LineMesh::Create(renderer(), outline);
    }
    if (!m_quad.valid())
    {
        DE_LOG_FATAL("Sandbox2D: quad mesh failed");
        requestQuit();
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
            requestQuit();
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
        requestQuit();
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
        requestQuit();
        return;
    }
    resetPlayer();
    m_camera.SetPosition(m_player.pos.x, m_player.pos.y + 1.0f);
    m_score = 0;

    network().setWantsPawn(true);
    network().setSceneMode(1);
    network().setPlayerName("Sandbox2D");
    network().setSpawnCallback(&Sandbox2DApp::onNetSpawn, this);
    network().setDespawnCallback(&Sandbox2DApp::onNetDespawn, this);
    network().setPeerCallback(&Sandbox2DApp::onNetPeer, this);
    registerLevelEntities();
    createLocalPlayerEntity();

    m_sfxJump  = audio().loadOrBlip(assets(), "audio/jump.wav", 420.0f, 0.14f, 0.5f);
    m_sfxCoin  = audio().loadOrBlip(assets(), "audio/coin.wav", 880.0f, 0.16f, 0.45f);
    m_sfxReset = audio().loadOrBlip(assets(), "audio/whoosh.wav", 180.0f, 0.22f, 0.35f);
    audio().setMasterVolume(0.85f);

    DE_LOG_INFO(
        "Sandbox2D: {} platforms, {} coins, camera ortho height {:.1f}",
        m_platforms.size(),
        m_coins.size(),
        m_camera.GetOrthoHeight());
    DE_LOG_INFO(LogCategory::Networking, "Sandbox2D net: Sandbox2D.exe -host   and   Sandbox2D.exe -join 127.0.0.1");
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
    syncLocalPawnTransform(dt);
    collectCoinsHostAuthority();

    if (m_player.pos.y < -6.0f)
    {
        DE_LOG_INFO("Sandbox2D: fell — respawn");
        audio().play2D(m_sfxReset, 0.55f);
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

    Vector2f follow = m_player.pos;
    float    facing = m_player.facing;
    const bool spectator = network().role() == NetRole::Client && !network().localPawn().valid();
    if (spectator)
    {
        follow = m_spawn;
        facing = 1.0f;
        if (!m_remotePawns.empty())
        {
            if (const TransformComponent* xf = world().get<TransformComponent>(m_remotePawns.front().entity))
            {
                follow = Vector2f(xf->position.x, xf->position.y);
                facing = facingFromRotation(xf->rotation);
            }
        }
    }

    Vector2f target(follow.x + facing * 2.4f, follow.y + 1.1f);
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
    handleNetHotkeys();
    applyNetRole();

    if (input().actionPressed("quit"))
    {
        requestQuit();
        return;
    }
    if (input().actionPressed("reset"))
    {
        if (network().role() == NetRole::Idle)
        {
            for (Coin& c : m_coins)
                c.collected = false;
            m_score = 0;
        }
        resetPlayer();
        audio().play2D(m_sfxReset, 0.5f);
        DE_LOG_INFO("Sandbox2D: reset");
    }
    if (input().actionPressed("debug"))
    {
        m_showCollision = !m_showCollision;
        DE_LOG_INFO("Sandbox2D: collision debug {}", m_showCollision);
    }

    Audio::AudioListener lis{};
    lis.position = Vector3f(m_camera.GetPosition().x, m_camera.GetPosition().y, 0.0f);
    lis.forward  = Vector3f(0.0f, 0.0f, 1.0f);
    lis.up       = Vector3f(0.0f, 1.0f, 0.0f);
    audio().setListener(lis);

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

void Sandbox2DApp::drawPawnSprite(
    ID3D12GraphicsCommandList* cmd,
    const Vector2f& pos,
    float facing,
    float tintR,
    float tintG,
    float tintB)
{
    const Texture2D* playerTex = m_playerAnim.currentTexture();
    if (!playerTex)
        return;
    const SpriteUvRect uv = m_playerAnim.currentUv();
    const SpriteSheet* sheet = m_playerAnim.currentSheet();
    const float fw = (sheet && sheet->frameWidth() > 0) ? static_cast<float>(sheet->frameWidth()) : 32.0f;
    const float fh = (sheet && sheet->frameHeight() > 0) ? static_cast<float>(sheet->frameHeight()) : 32.0f;
    constexpr float kSpriteHeight = 1.50f;
    const Vector2f playerSize(kSpriteHeight * (fw / fh), kSpriteHeight);
    const Vector2f spritePos(pos.x, pos.y - m_player.half.y + kSpriteHeight * 0.5f);
    drawSprite(cmd, *playerTex, spritePos, playerSize, 1.0f, tintR, tintG, tintB, 1, uv.du, uv.dv, facing, uv.u, uv.v);
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

    const bool drawLocal = network().role() != NetRole::Client || network().localPawn().valid();
    if (drawLocal)
        drawPawnSprite(cmd, m_player.pos, m_player.facing, 1.0f, 1.0f, 1.0f);

    for (const RemotePawn& rp : m_remotePawns)
    {
        if (!rp.entity.valid() || rp.entity == m_playerEntity)
            continue;
        const TransformComponent* xf = world().get<TransformComponent>(rp.entity);
        if (!xf)
            continue;
        float tint[4]{};
        unpackRgba8(rp.colorRgba8, tint);
        drawPawnSprite(
            cmd,
            Vector2f(xf->position.x, xf->position.y),
            facingFromRotation(xf->rotation),
            tint[0],
            tint[1],
            tint[2]);
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

    renderer().stats().drawCalls = static_cast<uint32_t>(m_platforms.size() + m_coins.size() + m_remotePawns.size() + 6);
    renderer().endFrame();
}

void Sandbox2DApp::onShutdown()
{
    network().shutdown();
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
    audio().stopAll();
    m_sfxJump.reset();
    m_sfxCoin.reset();
    m_sfxReset.reset();
    DE_LOG_INFO("Sandbox2D: shutdown (score {})", m_score);
}
