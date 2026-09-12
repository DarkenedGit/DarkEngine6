#pragma once

#include "Math/Matrix4f.h"
#include "Render/Mesh.h"
#include "Render/SpritePipeline.h"
#include "Render/Texture2D.h"
#include "Weapons/Weapon.h"

namespace Dark
{
    class Renderer;

    // Screen-center reticle plus 1/2 weapon slots. Pixel Y-up, origin bottom-left.
    class CrosshairHud
    {
    public:
        bool create(Renderer& renderer);
        void draw(ID3D12GraphicsCommandList* cmd, uint32_t viewW, uint32_t viewH, WeaponKind active) const;
        bool isValid() const { return m_pipe.isValid() && m_quad.valid() && m_white.valid(); }

    private:
        void drawRect(ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& viewProj, float cx, float cy, float w, float h, float r, float g, float b, float a) const;

        SpritePipeline m_pipe;
        Mesh           m_quad;
        Texture2D      m_white;
    };

} // namespace Dark
