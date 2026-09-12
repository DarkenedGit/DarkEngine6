#include "Render/CrosshairHud.h"

#include "Core/Log.h"
#include "Core/UiPalette.h"
#include "Render/Renderer.h"
#include "Render/MeshGen.h"

#include <cstring>

namespace Dark
{
    using Math::Matrix4f;

    namespace
    {
        void copyMatrix(float dst[16], const Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
        }
    } // namespace

    bool CrosshairHud::create(Renderer& renderer)
    {
        if (!m_pipe.create(renderer.device(), false))
        {
            DE_LOG_ERROR(LogCategory::Render, "CrosshairHud: sprite pipeline failed");
            return false;
        }
        MeshData quad;
        if (!CreateQuadXY(quad, 1.0f, 1.0f) || !Mesh::tryCreate(renderer, quad, m_quad))
        {
            DE_LOG_ERROR(LogCategory::Render, "CrosshairHud: quad mesh failed");
            return false;
        }
        if (!m_white.createSolidColor(renderer, 255, 255, 255, 255))
        {
            DE_LOG_ERROR(LogCategory::Render, "CrosshairHud: white texture failed");
            return false;
        }
        return true;
    }

    void CrosshairHud::drawRect(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, float cx, float cy, float w, float h, float r, float g, float b, float a) const
    {
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(w, h, 1.0f) * Matrix4f::TranslationMatrix(cx, cy, 0.05f);
        SpriteConstants cb{};
        copyMatrix(cb.worldViewProj, world * viewProj);
        cb.color[0]    = r;
        cb.color[1]    = g;
        cb.color[2]    = b;
        cb.color[3]    = a;
        cb.uvScale[0]  = 1.0f;
        cb.uvScale[1]  = 1.0f;
        cb.uvOffset[0] = 0.0f;
        cb.uvOffset[1] = 0.0f;
        m_white.bind(cmd, SpritePipeline::kRootAlbedoSrv);
        m_pipe.setConstants(cmd, cb);
        m_quad.draw(cmd);
    }

    void CrosshairHud::draw(ID3D12GraphicsCommandList* cmd, uint32_t viewW, uint32_t viewH, WeaponKind active) const
    {
        if (!cmd || !isValid() || viewW < 8 || viewH < 8)
            return;

        const float w    = static_cast<float>(viewW);
        const float h    = static_cast<float>(viewH);
        const Matrix4f proj = Matrix4f::OrthographicOffCenterLHMatrix(0.0f, w, 0.0f, h, 0.0f, 1.0f);
        const float cx   = w * 0.5f;
        const float cy   = h * 0.5f;

        const UiColor accent = active == WeaponKind::Projectile ? UiPalette::kWarn : UiPalette::kText;
        const UiColor dim    = UiPalette::kTextMuted;

        m_pipe.bind(cmd);

        constexpr float arm   = 11.0f;
        constexpr float thick = 2.0f;
        constexpr float gap   = 5.0f;
        drawRect(cmd, proj, cx - gap - arm * 0.5f, cy, arm, thick, accent.r, accent.g, accent.b, 0.92f);
        drawRect(cmd, proj, cx + gap + arm * 0.5f, cy, arm, thick, accent.r, accent.g, accent.b, 0.92f);
        drawRect(cmd, proj, cx, cy + gap + arm * 0.5f, thick, arm, accent.r, accent.g, accent.b, 0.92f);
        drawRect(cmd, proj, cx, cy - gap - arm * 0.5f, thick, arm, accent.r, accent.g, accent.b, 0.92f);
        drawRect(cmd, proj, cx, cy, 2.0f, 2.0f, accent.r, accent.g, accent.b, 0.85f);

        const float slotY = cy - 28.0f;
        const float s     = 7.0f;
        const UiColor meleeCol = active == WeaponKind::Melee ? UiPalette::kText : dim;
        const UiColor gunCol   = active == WeaponKind::Projectile ? UiPalette::kWarn : dim;
        drawRect(cmd, proj, cx - 10.0f, slotY, s, s, meleeCol.r, meleeCol.g, meleeCol.b, active == WeaponKind::Melee ? 0.95f : 0.45f);
        drawRect(cmd, proj, cx + 10.0f, slotY, s, s, gunCol.r, gunCol.g, gunCol.b, active == WeaponKind::Projectile ? 0.95f : 0.45f);
    }

} // namespace Dark
