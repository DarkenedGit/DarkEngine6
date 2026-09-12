#include "Render/GpuResourceCache.h"
#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Render/GpuMaterial.h"
#include "Render/Renderer.h"
#include "Render/Texture2D.h"
#include "Core/Log.h"

namespace Dark
{

    GpuResourceCache::GpuResourceCache(Renderer& renderer)
        : m_renderer(&renderer)
    {
    }

    void GpuResourceCache::registerPackedHeap(PackedSrvHeap* heap)
    {
        if (!heap)
            return;
        for (PackedSrvHeap* p : m_packedHeaps)
        {
            if (p == heap)
                return;
        }
        m_packedHeaps.push_back(heap);
    }

    void GpuResourceCache::unregisterPackedHeap(PackedSrvHeap* heap)
    {
        if (!heap)
            return;
        for (size_t i = 0; i < m_packedHeaps.size(); ++i)
        {
            if (m_packedHeaps[i] == heap)
            {
                m_packedHeaps[i] = m_packedHeaps.back();
                m_packedHeaps.pop_back();
                return;
            }
        }
    }

    bool GpuResourceCache::ensureTexture(const AssetRef<Image>& image)
    {
        if (!image || image->id == NULL_ASSET)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureTexture: null or unregistered image");
            return false;
        }
        const auto it = m_textures.find(image->id);
        if (it != m_textures.end() && it->second.gpu && it->second.gpu->valid())
            return true;
        if (!image->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureTexture: id={} has no pixels", image->id);
            return false;
        }
        if (!m_renderer)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureTexture: no renderer");
            return false;
        }
        auto gpu = std::make_shared<Texture2D>();
        if (!gpu->createFromImage(*m_renderer, *image))
            return false;
        TexEntry entry{};
        entry.cpu = image;
        entry.gpu = std::move(gpu);
        m_textures[image->id] = std::move(entry);
        return true;
    }

    bool GpuResourceCache::ensureMaterial(const AssetRef<Material>& material)
    {
        if (!material || material->id == NULL_ASSET)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: null or unregistered material");
            return false;
        }
        const auto it = m_materials.find(material->id);
        if (it != m_materials.end() && it->second.gpu && it->second.gpu->isValid())
            return true;

        const AssetRef<Image>& albedo = material->albedo();
        if (!albedo || albedo->id == NULL_ASSET || !ensureTexture(albedo))
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: id={} albedo upload failed", material->id);
            return false;
        }
        std::shared_ptr<Texture2D> tex = texture(albedo->id);
        if (!tex || !tex->valid() || !m_renderer || !m_renderer->device())
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: no device/texture");
            return false;
        }

        auto gpu = std::make_unique<GpuMaterial>();
        if (!gpu->pack(m_renderer->device(), *tex))
            return false;

        registerPackedHeap(&gpu->packedHeap());
        if (m_shadowCpu.ptr != 0)
            copyShadow(m_renderer->device(), gpu->packedHeap(), m_shadowCpu);

        MatEntry entry{};
        entry.cpu = material;
        entry.gpu = std::move(gpu);
        m_materials[material->id] = std::move(entry);
        return true;
    }

    std::shared_ptr<Texture2D> GpuResourceCache::texture(AssetID imageId) const
    {
        if (imageId == NULL_ASSET)
            return {};
        const auto it = m_textures.find(imageId);
        if (it == m_textures.end())
            return {};
        return it->second.gpu;
    }

    GpuMaterial* GpuResourceCache::material(AssetID materialId) const
    {
        if (materialId == NULL_ASSET)
            return nullptr;
        const auto it = m_materials.find(materialId);
        if (it == m_materials.end())
            return nullptr;
        return it->second.gpu.get();
    }

    void GpuResourceCache::bindMaterial(ID3D12GraphicsCommandList* cmd, const Material& material, UINT albedoSrvRootIndex) const
    {
        if (GpuMaterial* gpu = this->material(material.id))
        {
            gpu->bind(cmd, albedoSrvRootIndex);
            return;
        }
        static bool logged = false;
        if (!logged)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::bindMaterial: no GpuMaterial for id={}", material.id);
            logged = true;
        }
    }

    void GpuResourceCache::setShadowSrv(D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        m_shadowCpu = shadowCpu;
        if (shadowCpu.ptr == 0 || !m_renderer || !m_renderer->device())
            return;
        ID3D12Device* device = m_renderer->device();
        for (PackedSrvHeap* heap : m_packedHeaps)
        {
            if (!heap)
                continue;
            copyShadow(device, *heap, shadowCpu);
            ++m_shadowPatches;
        }
    }

    void GpuResourceCache::collectUnused()
    {
        for (auto it = m_materials.begin(); it != m_materials.end();)
        {
            if (it->second.cpu.expired())
            {
                if (it->second.gpu)
                    unregisterPackedHeap(&it->second.gpu->packedHeap());
                it = m_materials.erase(it);
            }
            else
                ++it;
        }
        for (auto it = m_textures.begin(); it != m_textures.end();)
        {
            const bool imageGone = it->second.cpu.expired();
            const bool onlyCache = it->second.gpu && it->second.gpu.use_count() == 1;
            if (imageGone && onlyCache)
                it = m_textures.erase(it);
            else
                ++it;
        }
    }

    void GpuResourceCache::clear()
    {
        m_packedHeaps.clear();
        m_materials.clear();
        m_textures.clear();
        m_shadowCpu     = {};
        m_shadowPatches = 0;
    }

    GpuResourceCache::Stats GpuResourceCache::stats() const
    {
        Stats s{};
        s.textures      = static_cast<uint32_t>(m_textures.size());
        s.materials     = static_cast<uint32_t>(m_materials.size());
        s.packedHeaps   = static_cast<uint32_t>(m_packedHeaps.size());
        s.shadowPatches = m_shadowPatches;
        return s;
    }

} // namespace Dark
