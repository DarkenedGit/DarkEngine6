#pragma once

#include "Core/Application.h"
#include "Geometry/Mesh.h"
#include "Geometry/LineMesh.h"
#include "Render/Camera2D.h"
#include "Render/SpritePipeline.h"
#include "Render/LinePipeline.h"
#include "Render/Texture2D.h"
#include "Sprite/SpriteAnimator.h"
#include "Sprite/SpriteSheet.h"
#include "Math/Aabb2f.h"
#include "Math/Vector2f.h"

#include <cstdint>
#include <memory>
#include <vector>

class Sandbox2DApp : public Dark::Application
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
        Dark::Math::Aabb2f box;
        float              z = 2.0f;
    };

    struct Coin
    {
        Dark::Math::Vector2f pos;
        bool                 collected = false;
    };

    struct Player
    {
        Dark::Math::Vector2f pos;
        Dark::Math::Vector2f vel;
        Dark::Math::Vector2f half{ 0.38f, 0.68f };
        bool                 grounded   = false;
        float                facing     = 1.0f;
        float                coyote      = 0.0f;
        float                jumpBuffer  = 0.0f;
        bool                 wasGrounded = false;
    };

    void registerActions();
    void buildLevel();
    bool tryLoadLevel();
    void resetPlayer();
    void updatePlayer(float dt);
    void updateCamera(float dt);
    void moveAxis(int axis, float dt);
    void depenetrate();

    void drawSprite(
        ID3D12GraphicsCommandList* cmd,
        const Dark::Texture2D& texture,
        const Dark::Math::Vector2f& pos,
        const Dark::Math::Vector2f& size,
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

    void updatePlayerAnim(float dt);

    Dark::Camera2D        m_camera;
    Dark::SpritePipeline  m_spritePipe;
    Dark::LinePipeline    m_linePipe;
    Dark::Geometry::Mesh  m_quad;
    Dark::Geometry::LineMesh m_boxOutline;

    std::vector<Dark::SpriteSheet> m_playerSheets;
    Dark::SpriteSheet              m_playerSheet;
    Dark::SpriteAnimator           m_playerAnim;

    Dark::Texture2D m_texPlatform;
    Dark::Texture2D m_texCoin;
    Dark::Texture2D m_texHillFar;
    Dark::Texture2D m_texHillMid;
    Dark::Texture2D m_texWhite;

    Player               m_player;
    std::vector<Platform> m_platforms;
    std::vector<Coin>     m_coins;
    uint32_t              m_score        = 0;
    bool                  m_showCollision = false;

    Dark::Math::Vector2f m_spawn{ 3.0f, 3.5f };
    Dark::Math::Vector2f m_worldMin{ 0.0f, 0.0f };
    Dark::Math::Vector2f m_worldMax{ 96.0f, 22.0f };
};
