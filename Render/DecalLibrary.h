#pragma once

#include "Render/DecalTypes.h"
#include "Render/Texture2D.h"

#include <d3d12.h>

namespace Dark
{

    class Renderer;

    // Procedural albedo and normal maps for the six definitions. One shader samples them.
    class DecalLibrary
    {
    public:
        DecalLibrary() = default;

        bool create(Renderer& renderer);
        void destroy(Renderer& renderer);

        bool isValid() const { return m_ready; }

        D3D12_CPU_DESCRIPTOR_HANDLE albedoSrv(DecalDefId id) const;
        D3D12_CPU_DESCRIPTOR_HANDLE normalSrv(DecalDefId id) const;

    private:
        const Texture2D* albedoTexture(DecalDefId id) const;
        const Texture2D* normalTexture(DecalDefId id) const;

        Texture2D m_bloodAlbedo;
        Texture2D m_footAlbedo;
        Texture2D m_bulletAlbedo;
        Texture2D m_slashAlbedo;
        Texture2D m_burnAlbedo;
        Texture2D m_bloodNormal;
        Texture2D m_footNormal;
        Texture2D m_bulletNormal;
        Texture2D m_slashNormal;
        Texture2D m_flatNormal;
        bool      m_ready = false;
    };

    // Batch key. Stable fields only; lifetime fade does not change the texture pair.
    DecalDefId decalDefForGpuInstance(const DecalGpuInstance& gpu);

} // namespace Dark
