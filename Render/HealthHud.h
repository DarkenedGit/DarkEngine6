#pragma once

#include "Geometry/Mesh.h"
#include "Math/Matrix4f.h"
#include "Render/SpritePipeline.h"
#include "Render/Texture2D.h"

namespace Dark
{
    class Renderer;

    // Screen-space health bar (lower-right). Pixel Y-up, origin bottom-left.
    class HealthHud
    {
    public:
        bool create(Renderer& renderer);
        void draw(ID3D12GraphicsCommandList* cmd, uint32_t viewW, uint32_t viewH, float ratio) const;
        bool isValid() const { return m_pipe.isValid() && m_quad.valid() && m_white.valid(); }

    private:
        void drawRect(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& viewProj, float cx, float cy, float w, float h, float r, float g, float b, float a) const;

        SpritePipeline   m_pipe;
        Geometry::Mesh   m_quad;
        Texture2D        m_white;
    };

} // namespace Dark
