#pragma once

#include "Core/Application.h"
#include "Render/Mesh.h"
#include "Render/LineMesh.h"
#include "Network/Replication.h"
#include "Render/Camera2D.h"
#include "Render/SpritePipeline.h"
#include "Render/LinePipeline.h"
#include "Render/Texture2D.h"
#include "Sprite/SpriteAnimator.h"
#include "Sprite/SpriteSheet.h"
#include "Math/AABox2f.h"
#include "Math/Vector2f.h"
#include "Audio/SoundClip.h"

#include <box2d/box2d.h>

#include <cstdint>
#include <memory>
#include <vector>

using namespace Dark;

class Sandbox2DApp : public Application
{
public:
    using Application::Application;

    void onInit() override;
    void onUpdate(float dt) override;
    void onRender() override;
    void onShutdown() override;

private:
    struct Platform
    {
        Math::AABox2f box;
        float              z    = 2.0f;
        Entity       entity{};
        b2BodyId           body = b2_nullBodyId;
    };

    struct Coin
    {
        Math::Vector2f pos;
        bool                 collected = false;
        Entity         entity{};
    };

    struct Player
    {
        Math::Vector2f pos;
        Math::Vector2f vel;
        Math::Vector2f half{ 0.38f, 0.68f };
        bool                 grounded    = false;
        float                facing      = 1.0f;
        float                coyote      = 0.0f;
        float                jumpBuffer  = 0.0f;
        bool                 wasGrounded = false;
    };

    struct RemotePawn
    {
        Entity entity{};
        uint32_t     colorRgba8 = 0xFFFFFFFFu;
    };

    void registerActions();
    void buildLevel();
    bool tryLoadLevel();
    void resetPlayer();
    void updatePlayer(float dt);
    void updateCamera(float dt);

    void destroyPhysics();
    bool createPhysicsWorld();
    bool createPlayerBody();
    void createPlatformBodies();
    void addPlatformBody(Platform& p);
    void applyPlayerControl(float dt);
    void syncPlayerFromBody();
    bool playerGrounded() const;

    void handleNetHotkeys();
    void applyNetRole();
    void registerLevelEntities();
    void createLocalPlayerEntity();
    void unregisterIdleReplicas();
    void restoreLocalLevel();
    void spawnOwnedPawn(ClientId owner, float offsetX);
    Entity findPawn(ClientId owner);
    void syncLocalPawnTransform(float dt);
    void collectCoinsHostAuthority();
    bool ensureClientPhysics();

    void drawSprite(
        ID3D12GraphicsCommandList* cmd,
        const Texture2D& texture,
        const Math::Vector2f& pos,
        const Math::Vector2f& size,
        float z,
        float tintR,
        float tintG,
        float tintB,
        float tintA,
        float uvScaleX,
        float uvScaleY,
        float flipX = 1.0f,
        float uvOffX = 0.0f,
        float uvOffY = 0.0f);

    void drawPawnSprite(
        ID3D12GraphicsCommandList* cmd,
        const Math::Vector2f& pos,
        float facing,
        float tintR,
        float tintG,
        float tintB);

    void updatePlayerAnim(float dt);

    static bool onNetSpawn(World& world, Entity e, NetPrefab prefab, const TransformComponent& xf, uint32_t colorRgba8, void* user);
    static void onNetDespawn(World& world, Entity e, NetId id, void* user);
    static void onNetPeer(const NetPeerInfo& info, NetPeerEvent event, void* user);

    Camera2D        m_camera;
    SpritePipeline  m_spritePipe;
    LinePipeline    m_linePipe;
    Mesh            m_quad;
    LineMesh        m_boxOutline;

    std::vector<SpriteSheet> m_playerSheets;
    SpriteSheet              m_playerSheet;
    SpriteAnimator           m_playerAnim;

    Texture2D m_texPlatform;
    Texture2D m_texCoin;
    Texture2D m_texHillFar;
    Texture2D m_texHillMid;
    Texture2D m_texWhite;

    Player                m_player;
    Entity          m_playerEntity{};
    std::vector<Platform> m_platforms;
    std::vector<Coin>     m_coins;
    std::vector<RemotePawn> m_remotePawns;
    uint32_t              m_score         = 0;
    bool                  m_showCollision = false;
    NetRole         m_netRole       = NetRole::Idle;

    Math::Vector2f m_spawn{ 3.0f, 3.5f };
    Math::Vector2f m_worldMin{ 0.0f, 0.0f };
    Math::Vector2f m_worldMax{ 96.0f, 22.0f };

    std::shared_ptr<Audio::SoundClip> m_sfxJump;
    std::shared_ptr<Audio::SoundClip> m_sfxCoin;
    std::shared_ptr<Audio::SoundClip> m_sfxReset;

    b2WorldId m_physWorld      = b2_nullWorldId;
    b2BodyId  m_playerBody     = b2_nullBodyId;
    b2ShapeId m_playerShape    = b2_nullShapeId;
    float     m_physAccum      = 0.0f;
};
