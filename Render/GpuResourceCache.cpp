#include "Render/GpuResourceCache.h"
#include "Render/GpuMaterial.h"
#include "Render/Material.h"
#include "Render/Renderer.h"
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

        GpuMaterial* gpu = material->gpuMaterial();
        if (!gpu || !gpu->isValid())
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: id={} is not packed", material->id);
            return false;
        }

        registerPackedHeap(&gpu->packedHeap());
        if (m_shadowCpu.ptr != 0 && m_renderer && m_renderer->device())
            copyShadow(m_renderer->device(), gpu->packedHeap(), m_shadowCpu);

        MatEntry entry{};
        entry.cpu = material;
        entry.gpu = gpu;
        m_materials[material->id] = entry;
        material->setGpuCache(this);
        return true;
    }

    GpuMaterial* GpuResourceCache::material(AssetID materialId) const
    {
        if (materialId == NULL_ASSET)
            return nullptr;
        const auto it = m_materials.find(materialId);
        if (it == m_materials.end())
            return nullptr;
        return it->second.gpu;
    }

    void GpuResourceCache::bindMaterial(ID3D12GraphicsCommandList* cmd, const Material& material, UINT albedoSrvRootIndex) const
    {
        if (GpuMaterial* gpu = this->material(material.id))
        {
            gpu->bind(cmd, albedoSrvRootIndex);
            return;
        }
        material.bind(cmd, albedoSrvRootIndex);
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
                // Heap was unregistered in Material::~Material while still alive.
                it = m_materials.erase(it);
            }
            else
                ++it;
        }
    }

    void GpuResourceCache::clear()
    {
        for (auto& kv : m_materials)
        {
            if (AssetRef<Material> mat = kv.second.cpu.lock())
                mat->setGpuCache(nullptr);
        }
        m_packedHeaps.clear();
        m_materials.clear();
        m_shadowCpu      = {};
        m_shadowPatches  = 0;
    }

    GpuResourceCache::Stats GpuResourceCache::stats() const
    {
        Stats s{};
        s.materials     = static_cast<uint32_t>(m_materials.size());
        s.packedHeaps   = static_cast<uint32_t>(m_packedHeaps.size());
        s.shadowPatches = m_shadowPatches;
        return s;
    }

} // namespace Dark
