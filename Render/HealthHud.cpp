#include "Render/HealthHud.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
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

    bool HealthHud::create(Renderer& renderer)
    {
        if (!m_pipe.create(renderer.device(), false))
        {
            DE_LOG_ERROR(LogCategory::Render, "HealthHud: sprite pipeline failed");
            return false;
        }
        MeshData quad;
        if (!CreateQuadXY(quad, 1.0f, 1.0f) || !Mesh::tryCreate(renderer, quad, m_quad))
        {
            DE_LOG_ERROR(LogCategory::Render, "HealthHud: quad mesh failed");
            return false;
        }
        if (!m_white.createSolidColor(renderer, 255, 255, 255, 255))
        {
            DE_LOG_ERROR(LogCategory::Render, "HealthHud: white texture failed");
            return false;
        }
        return true;
    }

    void HealthHud::drawRect(ID3D12GraphicsCommandList* cmd, const Matrix4f& viewProj, float cx, float cy, float w, float h, float r, float g, float b, float a) const
    {
        const Matrix4f world = Matrix4f::ScaleMatrixXYZ(w, h, 1.0f) * Matrix4f::TranslationMatrix(cx, cy, 0.05f);
        SpriteConstants cb{};
        copyMatrix(cb.worldViewProj, world * viewProj);
        cb.color[0]   = r;
        cb.color[1]   = g;
        cb.color[2]   = b;
        cb.color[3]   = a;
        cb.uvScale[0] = 1.0f;
        cb.uvScale[1] = 1.0f;
        cb.uvOffset[0] = 0.0f;
        cb.uvOffset[1] = 0.0f;
        m_white.bind(cmd, SpritePipeline::kRootAlbedoSrv);
        m_pipe.setConstants(cmd, cb);
        m_quad.draw(cmd);
    }

    void HealthHud::draw(ID3D12GraphicsCommandList* cmd, uint32_t viewW, uint32_t viewH, float ratio) const
    {
        if (!cmd || !isValid() || viewW < 8 || viewH < 8)
            return;

        const float w = static_cast<float>(viewW);
        const float h = static_cast<float>(viewH);
        const Matrix4f proj = Matrix4f::OrthographicOffCenterLHMatrix(0.0f, w, 0.0f, h, 0.0f, 1.0f);

        const float barW   = 280.0f;
        const float barH   = 22.0f;
        const float pad    = 28.0f;
        const float border = 3.0f;
        const float x0     = w - pad - barW;
        const float y0     = pad;

        const float fill = Math::Clamp(ratio, 0.0f, 1.0f);
        float fr = 0.18f, fg = 0.78f, fb = 0.28f;
        if (fill <= 0.25f)
        {
            fr = 0.86f;
            fg = 0.16f;
            fb = 0.14f;
        }
        else if (fill <= 0.5f)
        {
            fr = 0.92f;
            fg = 0.72f;
            fb = 0.16f;
        }

        m_pipe.bind(cmd);
        drawRect(cmd, proj, x0 + barW * 0.5f, y0 + barH * 0.5f, barW, barH, 0.04f, 0.04f, 0.05f, 0.82f);
        const float innerW = barW - border * 2.0f;
        const float innerH = barH - border * 2.0f;
        drawRect(cmd, proj, x0 + barW * 0.5f, y0 + barH * 0.5f, innerW, innerH, 0.12f, 0.10f, 0.10f, 0.90f);
        if (fill > 1.0e-4f)
        {
            const float fillW = innerW * fill;
            const float fillCx = x0 + border + fillW * 0.5f;
            drawRect(cmd, proj, fillCx, y0 + barH * 0.5f, fillW, innerH, fr, fg, fb, 0.95f);
        }
    }

} // namespace Dark
