#include "Render/GpuResourceCache.h"
#include "Assets/Image.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Render/GpuMaterial.h"
#include "Render/Mesh.h"
#include "Render/Renderer.h"
#include "Render/Texture2D.h"
#include "Core/Log.h"

namespace Dark
{

    namespace
    {
        void copyAlbedoSlot(ID3D12Device* device, GpuMaterial& gpu, const Texture2D& tex, bool raw)
        {
            if (!device || !gpu.isValid())
                return;
            const D3D12_CPU_DESCRIPTOR_HANDLE src = raw ? tex.cpuHandleRaw() : tex.cpuHandle();
            if (src.ptr == 0)
                return;
            PackedSrvHeap& heap = gpu.packedHeap();
            const UINT     incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_CPU_DESCRIPTOR_HANDLE dst = heap.heap->GetCPUDescriptorHandleForHeapStart();
            dst.ptr += static_cast<SIZE_T>(GpuMaterial::kAlbedoSlot) * incr;
            device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        AssetID mapAssetId(const AssetRef<Image>& image)
        {
            return (image && image->id != NULL_ASSET) ? image->id : NULL_ASSET;
        }
    } // namespace

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
        if (m_shadowCpu.ptr != 0 && m_renderer && m_renderer->device())
            copyShadow(m_renderer->device(), *heap, m_shadowCpu);
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

    bool GpuResourceCache::ensureTexture(const AssetRef<Image>& image, Color::TextureUsage usage)
    {
        if (!image || image->id == NULL_ASSET)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureTexture: null or unregistered image");
            return false;
        }
        const auto                it          = m_textures.find(image->id);
        const bool                gpuValid    = it != m_textures.end() && it->second.gpu && it->second.gpu->valid();
        const Color::TextureUsage cachedUsage = gpuValid ? it->second.usage : usage;
        const CachedTextureReuse  reuse       = classifyCachedTextureReuse(gpuValid, cachedUsage, usage);
        if (reuse == CachedTextureReuse::Reuse)
            return true;
        if (reuse == CachedTextureReuse::Conflict)
        {
            DE_LOG_WARN(LogCategory::Render, "GpuResourceCache::ensureTexture: id={} keeping first usage {} (ignored {})", image->id,
                        static_cast<unsigned>(cachedUsage), static_cast<unsigned>(usage));
            return true;
        }
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
        if (!gpu->createFromImage(*m_renderer, *image, usage))
            return false;
        TexEntry entry{};
        entry.cpu             = image;
        entry.gpu             = std::move(gpu);
        entry.usage           = usage;
        m_textures[image->id] = std::move(entry);
        return true;
    }

    bool GpuResourceCache::ensureDefaultMaps()
    {
        if (m_defaultNormal && m_defaultNormal->valid() && m_defaultOrm && m_defaultOrm->valid() && m_defaultEmissive && m_defaultEmissive->valid())
            return true;
        if (!m_renderer)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureDefaultMaps: no renderer");
            return false;
        }
        if (!m_defaultNormal)
            m_defaultNormal = std::make_unique<Texture2D>();
        if (!m_defaultNormal->valid() && !m_defaultNormal->createSolidColor(*m_renderer, 128, 128, 255, 255, Color::TextureUsage::Normal))
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureDefaultMaps: default normal failed");
            return false;
        }
        if (!m_defaultOrm)
            m_defaultOrm = std::make_unique<Texture2D>();
        if (!m_defaultOrm->valid() && !m_defaultOrm->createSolidColor(*m_renderer, 255, 255, 255, 255, Color::TextureUsage::Orm))
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureDefaultMaps: default ORM failed");
            return false;
        }
        if (!m_defaultEmissive)
            m_defaultEmissive = std::make_unique<Texture2D>();
        if (!m_defaultEmissive->valid() && !m_defaultEmissive->createSolidColor(*m_renderer, 255, 255, 255, 255, Color::TextureUsage::Emissive))
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureDefaultMaps: default emissive failed");
            return false;
        }
        return true;
    }

    const Texture2D* GpuResourceCache::resolveMapOrDefault(const AssetRef<Image>& image, Color::TextureUsage usage, const Texture2D* fallback)
    {
        if (!image || image->id == NULL_ASSET)
            return fallback;
        if (!ensureTexture(image, usage))
        {
            DE_LOG_WARN(LogCategory::Render, "GpuResourceCache: map id={} usage={} upload failed, using default", image->id, static_cast<unsigned>(usage));
            return fallback;
        }
        const auto it = m_textures.find(image->id);
        if (it == m_textures.end() || !it->second.gpu || !it->second.gpu->valid())
            return fallback;
        if (it->second.usage != usage)
        {
            DE_LOG_WARN(LogCategory::Render, "GpuResourceCache: map id={} keeping first usage {}, packing default for {}", image->id,
                        static_cast<unsigned>(it->second.usage), static_cast<unsigned>(usage));
            return fallback;
        }
        return it->second.gpu.get();
    }

    bool GpuResourceCache::ensureMaterial(const AssetRef<Material>& material)
    {
        if (!material || material->id == NULL_ASSET)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: null or unregistered material");
            return false;
        }

        const AssetID albedoId = mapAssetId(material->albedo());
        const AssetID normalId = mapAssetId(material->normalImage());
        const AssetID ormId    = mapAssetId(material->ormImage());
        const AssetID emisId   = mapAssetId(material->emissiveImage());

        auto it = m_materials.find(material->id);
        if (it != m_materials.end() && it->second.gpu && it->second.gpu->isValid())
        {
            const AssetID* ids = it->second.gpu->packedMapIds();
            if (ids[GpuMaterial::kAlbedoSlot] == albedoId && ids[GpuMaterial::kNormalSlot] == normalId && ids[GpuMaterial::kOrmSlot] == ormId
                && ids[GpuMaterial::kEmissiveSlot] == emisId)
                return true;
            unregisterPackedHeap(&it->second.gpu->packedHeap());
        }

        if (!ensureDefaultMaps())
            return false;

        const AssetRef<Image>& albedo = material->albedo();
        if (!albedo || albedoId == NULL_ASSET || !ensureTexture(albedo, Color::TextureUsage::Albedo))
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: id={} albedo upload failed", material->id);
            return false;
        }
        std::shared_ptr<Texture2D> albedoTex = texture(albedoId);
        if (!albedoTex || !albedoTex->valid() || !m_renderer || !m_renderer->device())
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: no device/texture");
            return false;
        }
        const auto albedoIt = m_textures.find(albedoId);
        if (albedoIt != m_textures.end() && albedoIt->second.usage != Color::TextureUsage::Albedo)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: id={} albedo lost to first-usage-wins", material->id);
            return false;
        }

        const Texture2D* normalTex = resolveMapOrDefault(material->normalImage(), Color::TextureUsage::Normal, m_defaultNormal.get());
        const Texture2D* ormTex    = resolveMapOrDefault(material->ormImage(), Color::TextureUsage::Orm, m_defaultOrm.get());
        const Texture2D* emisTex   = resolveMapOrDefault(material->emissiveImage(), Color::TextureUsage::Emissive, m_defaultEmissive.get());
        if (!normalTex || !ormTex || !emisTex)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureMaterial: id={} missing default map SRV", material->id);
            return false;
        }

        GpuMaterial* packed = nullptr;
        std::unique_ptr<GpuMaterial> created;
        if (it != m_materials.end() && it->second.gpu)
            packed = it->second.gpu.get();
        else
        {
            created = std::make_unique<GpuMaterial>();
            packed  = created.get();
        }

        if (!packed->pack(m_renderer->device(), *albedoTex, *normalTex, *ormTex, *emisTex))
            return false;
        packed->setPackedMapIds(albedoId, normalId, ormId, emisId);

        registerPackedHeap(&packed->packedHeap());
        if (m_shadowCpu.ptr != 0)
            copyShadow(m_renderer->device(), packed->packedHeap(), m_shadowCpu);
        if (m_albedoSamplingRaw)
            copyAlbedoSlot(m_renderer->device(), *packed, *albedoTex, true);

        if (created)
        {
            MatEntry entry{};
            entry.cpu                 = material;
            entry.gpu                 = std::move(created);
            m_materials[material->id] = std::move(entry);
        }
        else
            it->second.cpu = material;
        return true;
    }

    bool GpuResourceCache::ensureModel(const AssetRef<Model>& model)
    {
        if (!model || model->id == NULL_ASSET)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureModel: null or unregistered model");
            return false;
        }
        const auto existing = m_models.find(model->id);
        if (existing != m_models.end() && existing->second.gpu && existing->second.gpu->valid())
            return true;
        if (!m_renderer)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureModel: no renderer");
            return false;
        }

        auto gpu = std::make_unique<GpuModel>();
        auto upload = [&](const std::vector<Model::Part>& src, std::vector<GpuModel::Part>& dst) {
            for (size_t i = 0; i < src.size(); ++i)
            {
                const Model::Part& part = src[i];
                if (part.mesh.positions.empty() || part.mesh.indices.empty())
                    continue;
                if (part.material && !ensureMaterial(part.material))
                {
                    DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureModel: material upload failed for part {}", i);
                    continue;
                }
                GpuModel::Part gp;
                const bool ok = part.skinned ? Mesh::tryCreateSkinned(*m_renderer, part.mesh, gp.mesh)
                                             : Mesh::tryCreate(*m_renderer, part.mesh, gp.mesh);
                if (!ok)
                {
                    DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureModel: mesh upload failed for part {}", i);
                    continue;
                }
                gp.materialId  = part.material ? part.material->id : NULL_ASSET;
                gp.localToRoot = part.localToRoot;
                gp.translucent = part.translucent;
                gp.skinned     = part.skinned;
                dst.push_back(std::move(gp));
            }
        };
        upload(model->opaque(), gpu->m_opaque);
        upload(model->translucent(), gpu->m_translucent);
        if (!gpu->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::ensureModel: id={} has no drawable GPU parts", model->id);
            return false;
        }
        ModEntry entry{};
        entry.cpu = model;
        entry.gpu = std::move(gpu);
        m_models[model->id] = std::move(entry);
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

    GpuModel* GpuResourceCache::model(AssetID modelId) const
    {
        if (modelId == NULL_ASSET)
            return nullptr;
        const auto it = m_models.find(modelId);
        if (it == m_models.end())
            return nullptr;
        return it->second.gpu.get();
    }

    AssetRef<Material> GpuResourceCache::cpuMaterial(AssetID materialId) const
    {
        if (materialId == NULL_ASSET)
            return {};
        const auto it = m_materials.find(materialId);
        if (it == m_materials.end())
            return {};
        return it->second.cpu.lock();
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

    void GpuResourceCache::bindMaterial(ID3D12GraphicsCommandList* cmd, AssetID materialId, UINT albedoSrvRootIndex) const
    {
        if (GpuMaterial* gpu = material(materialId))
        {
            gpu->bind(cmd, albedoSrvRootIndex);
            return;
        }
        if (materialId == NULL_ASSET)
            return;
        static bool logged = false;
        if (!logged)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuResourceCache::bindMaterial: no GpuMaterial for id={}", materialId);
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

    void GpuResourceCache::setAlbedoSamplingRaw(bool raw)
    {
        m_albedoSamplingRaw = raw;
        if (!m_renderer || !m_renderer->device())
            return;
        ID3D12Device* device = m_renderer->device();
        for (auto& kv : m_materials)
        {
            MatEntry& entry = kv.second;
            if (!entry.gpu || !entry.gpu->isValid())
                continue;
            const AssetRef<Material> cpu = entry.cpu.lock();
            if (!cpu || !cpu->albedo() || cpu->albedo()->id == NULL_ASSET)
                continue;
            std::shared_ptr<Texture2D> tex = texture(cpu->albedo()->id);
            if (!tex || !tex->valid())
                continue;
            copyAlbedoSlot(device, *entry.gpu, *tex, raw);
        }
    }

    void GpuResourceCache::collectUnused()
    {
        for (auto it = m_models.begin(); it != m_models.end();)
        {
            if (it->second.cpu.expired())
                it = m_models.erase(it);
            else
                ++it;
        }
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
        m_models.clear();
        m_materials.clear();
        m_textures.clear();
        m_defaultNormal.reset();
        m_defaultOrm.reset();
        m_defaultEmissive.reset();
        m_shadowCpu     = {};
        m_shadowPatches = 0;
    }

    GpuResourceCache::Stats GpuResourceCache::stats() const
    {
        Stats s{};
        s.textures      = static_cast<uint32_t>(m_textures.size());
        s.materials     = static_cast<uint32_t>(m_materials.size());
        s.models        = static_cast<uint32_t>(m_models.size());
        s.packedHeaps   = static_cast<uint32_t>(m_packedHeaps.size());
        s.shadowPatches = m_shadowPatches;
        return s;
    }

} // namespace Dark
