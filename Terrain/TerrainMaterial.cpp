#include "Terrain/TerrainMaterial.h"
#include "Assets/Image.h"
#include "Core/Log.h"
#include "Render/GpuResourceCache.h"
#include "Render/Renderer.h"
#include "Terrain/HeightBlend.h"

#include <cstring>
#include <utility>
#include <vector>

namespace Dark
{

    namespace
    {
        bool CreateChecker(
            Renderer& renderer,
            Texture2D& out,
            uint8_t r0, uint8_t g0, uint8_t b0,
            uint8_t r1, uint8_t g1, uint8_t b1,
            uint32_t size = 64,
            uint32_t cell = 8)
        {
            std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4u);
            for (uint32_t y = 0; y < size; ++y)
            {
                for (uint32_t x = 0; x < size; ++x)
                {
                    const bool   alt = ((x / cell) + (y / cell)) & 1u;
                    const size_t i   = (static_cast<size_t>(y) * size + x) * 4u;
                    px[i + 0]        = alt ? r1 : r0;
                    px[i + 1]        = alt ? g1 : g0;
                    px[i + 2]        = alt ? b1 : b0;
                    px[i + 3]        = 255;
                }
            }
            Image img;
            if (!img.createFromRGBA(px.data(), size, size, size * 4u))
                return false;
            return out.createFromImage(renderer, img, Color::TextureUsage::Albedo);
        }

        void BindInternedMap(
            GpuResourceCache& cache,
            const AssetRef<Image>& image,
            Color::TextureUsage usage,
            int layer,
            const char* kind,
            UINT slot,
            std::shared_ptr<Texture2D>& hold)
        {
            hold.reset();
            if (!image || image->id == NULL_ASSET)
                return;
            if (!image->valid())
            {
                DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: missing '{}' — default slot {}", kind, slot);
                return;
            }
            const uint32_t dim = image->width() > image->height() ? image->width() : image->height();
            if (dim > TerrainMaterial::kMaxLayerImageSize)
            {
                DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: layer {} {} {}x{} exceeds {} — default slot {}", layer, kind, image->width(), image->height(),
                             TerrainMaterial::kMaxLayerImageSize, slot);
                return;
            }
            const Texture2D* resolved = cache.resolveMapOrDefault(image, usage, nullptr);
            if (!resolved || !resolved->valid())
            {
                DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: missing '{}' — default slot {}", kind, slot);
                return;
            }
            hold = cache.texture(image->id);
            if (!hold || hold.get() != resolved)
            {
                hold.reset();
                DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: missing '{}' — default slot {}", kind, slot);
            }
        }
    } // namespace

    TerrainMaterial::~TerrainMaterial()
    {
        unbindCache();
    }

    TerrainMaterial::TerrainMaterial(TerrainMaterial&& other) noexcept
    {
        *this = std::move(other);
    }

    TerrainMaterial& TerrainMaterial::operator=(TerrainMaterial&& other) noexcept
    {
        if (this == &other)
            return *this;
        unbindCache();
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            m_albedoOwned[i] = std::move(other.m_albedoOwned[i]);
            m_albedoGpu[i]   = std::move(other.m_albedoGpu[i]);
            m_normalGpu[i]   = std::move(other.m_normalGpu[i]);
            m_ormGpu[i]      = std::move(other.m_ormGpu[i]);
            m_layers[i]      = other.m_layers[i];
            if (m_albedoGpu[i] && m_albedoGpu[i]->valid())
                m_albedoSrv[i] = m_albedoGpu[i].get();
            else if (m_albedoOwned[i].valid())
                m_albedoSrv[i] = &m_albedoOwned[i];
            else
                m_albedoSrv[i] = other.m_albedoSrv[i]; // cache default; process-lifetime
            other.m_albedoSrv[i] = nullptr;
        }
        m_splat              = std::move(other.m_splat);
        m_params             = other.m_params;
        m_heap               = std::move(other.m_heap);
        std::memcpy(m_packedSrc, other.m_packedSrc, sizeof(m_packedSrc));
        std::memset(other.m_packedSrc, 0, sizeof(other.m_packedSrc));
        m_layerSamplingRaw   = other.m_layerSamplingRaw;
        m_cache              = other.m_cache;
        other.m_cache            = nullptr;
        other.m_layerSamplingRaw = false;
        other.m_params           = {};
        if (m_cache)
            m_cache->registerPackedHeap(&m_heap);
        return *this;
    }

    void TerrainMaterial::unbindCache()
    {
        if (m_cache)
        {
            m_cache->unregisterPackedHeap(&m_heap);
            m_cache = nullptr;
        }
    }

    const Texture2D* TerrainMaterial::albedoTex(int i) const
    {
        if (i < 0 || i >= Terrain::kMaxTerrainLayers)
            return nullptr;
        if (m_albedoSrv[i] && m_albedoSrv[i]->valid())
            return m_albedoSrv[i];
        if (m_albedoGpu[i] && m_albedoGpu[i]->valid())
            return m_albedoGpu[i].get();
        if (m_albedoOwned[i].valid())
            return &m_albedoOwned[i];
        return nullptr;
    }

    bool TerrainMaterial::packSrvHeap(Renderer& renderer)
    {
        unbindCache();
        m_heap = PackedSrvHeap{};
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            m_albedoSrv[i] = nullptr;
        ID3D12Device* device = renderer.device();
        if (!device)
            return false;

        GpuResourceCache& cache = renderer.gpuResources();
        if (!cache.ensureTerrainDefaults())
            return false;
        const Texture2D* defAlbedo = cache.defaultTerrainAlbedo();
        const Texture2D* defNormal = cache.defaultNormal();
        const Texture2D* defOrm    = cache.defaultTerrainOrm();
        if (!defAlbedo || !defAlbedo->valid() || !defNormal || !defNormal->valid() || !defOrm || !defOrm->valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: terrain default maps missing");
            return false;
        }

        if (!m_splat.valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: splat is invalid");
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE src[TerrainPipeline::kSrvCount]{};
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            const Texture2D* alb = albedoTex(i);
            if (!alb)
                alb = defAlbedo;
            const Texture2D* nrm = (m_normalGpu[i] && m_normalGpu[i]->valid()) ? m_normalGpu[i].get() : defNormal;
            const Texture2D* orm = (m_ormGpu[i] && m_ormGpu[i]->valid()) ? m_ormGpu[i].get() : defOrm;
            if (!alb->valid() || alb->cpuHandle().ptr == 0)
            {
                DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: layer {} is invalid", i);
                return false;
            }
            src[layerAlbedoSlot(static_cast<UINT>(i))] = alb->cpuHandle();
            src[layerNormalSlot(static_cast<UINT>(i))] = nrm->cpuHandle();
            src[layerOrmSlot(static_cast<UINT>(i))]    = orm->cpuHandle();
            m_albedoSrv[i] = alb;
        }
        src[kSplatSlot] = m_splat.cpuHandle();
        // Slot 13 (shadow) filled by GpuResourceCache::setShadowSrv.
        std::memcpy(m_packedSrc, src, sizeof(m_packedSrc));
        if (!packFromCpuHandles(device, m_heap, src, TerrainPipeline::kSrvCount))
            return false;
        m_heap.shadowSlot = kShadowSlot;
        m_heap.splatSlot  = kSplatSlot;
        m_heap.srvCount   = TerrainPipeline::kSrvCount;
        m_cache           = &cache;
        m_cache->registerPackedHeap(&m_heap);
        if (m_layerSamplingRaw)
            copyLayerSampling(device);
        DE_LOG_INFO(LogCategory::Render, "TerrainMaterial: 14-slot heap ready (bindLayout=1)");
        return true;
    }

    bool TerrainMaterial::fillPackedCpuHandles(D3D12_CPU_DESCRIPTOR_HANDLE out[kSrvCount]) const
    {
        if (!out)
            return false;
        if (!isValid())
            return false;
        std::memcpy(out, m_packedSrc, sizeof(m_packedSrc));
        return true;
    }

    bool TerrainMaterial::packTileHeap(ID3D12Device* device, PackedSrvHeap& out, D3D12_CPU_DESCRIPTOR_HANDLE splatCpu) const
    {
        out.shadowSlot = kShadowSlot;
        out.splatSlot  = kSplatSlot;
        out.srvCount   = kSrvCount;

        D3D12_CPU_DESCRIPTOR_HANDLE src[kSrvCount]{};
        const bool haveTemplate = fillPackedCpuHandles(src);
        if (haveTemplate)
            src[kSplatSlot] = splatCpu.ptr != 0 ? splatCpu : src[kSplatSlot];

        if (device && haveTemplate)
        {
            if (!packFromCpuHandles(device, out, src, kSrvCount))
                return false;
            out.shadowSlot = kShadowSlot;
            out.splatSlot  = kSplatSlot;
            out.srvCount   = kSrvCount;
        }

        copySplat(device, out, splatCpu.ptr != 0 ? splatCpu : src[kSplatSlot]);
        return true;
    }

    bool TerrainMaterial::createDefault(Renderer& renderer, const Terrain::SplatMap& splat)
    {
        if (!splat.valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial::createDefault: invalid splat");
            return false;
        }

        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            m_albedoGpu[i].reset();
            m_normalGpu[i].reset();
            m_ormGpu[i].reset();
            m_albedoSrv[i] = nullptr;
        }

        if (!CreateChecker(renderer, m_albedoOwned[0], 132, 96, 58, 110, 78, 44)
            || !CreateChecker(renderer, m_albedoOwned[1], 62, 122, 48, 48, 98, 38)
            || !CreateChecker(renderer, m_albedoOwned[2], 118, 114, 108, 88, 86, 82)
            || !CreateChecker(renderer, m_albedoOwned[3], 236, 240, 244, 210, 218, 226))
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: failed to create layer textures");
            return false;
        }

        Image splatImg;
        if (!splatImg.createFromRGBA(splat.rgba(), splat.width(), splat.height(), splat.width() * 4u)
            || !m_splat.createFromImage(renderer, splatImg, Color::TextureUsage::Data))
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: failed to upload splat");
            return false;
        }

        m_layers[0].tiling = 24.0f;
        m_layers[1].tiling = 20.0f;
        m_layers[2].tiling = 16.0f;
        m_layers[3].tiling = 12.0f;
        m_params               = {};
        m_params.heightBlendK  = 0.0f; // splat branch; dummy ORM.a unused so checkers stay dull

        return packSrvHeap(renderer);
    }

    bool TerrainMaterial::create(
        Renderer& renderer,
        Texture2D layers[Terrain::kMaxTerrainLayers],
        Texture2D&& splat,
        const Terrain::TerrainLayerDesc layerDescs[Terrain::kMaxTerrainLayers])
    {
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            m_albedoOwned[i] = std::move(layers[i]);
            m_albedoGpu[i].reset();
            m_normalGpu[i].reset();
            m_ormGpu[i].reset();
            m_albedoSrv[i] = nullptr;
        }
        m_splat  = std::move(splat);
        m_params = {};

        if (layerDescs)
        {
            for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
                m_layers[i] = layerDescs[i];
        }

        return packSrvHeap(renderer);
    }

    bool TerrainMaterial::create(Renderer& renderer, const Terrain::TerrainSurfaceDesc& desc, Texture2D&& splat)
    {
        GpuResourceCache& cache = renderer.gpuResources();
        if (!cache.ensureTerrainDefaults())
            return false;

        m_splat  = std::move(splat);
        m_params = desc.params;
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            m_layers[i] = desc.layers[i];
            m_albedoOwned[i] = Texture2D{};
            m_albedoSrv[i]   = nullptr;
            BindInternedMap(cache, desc.albedo[i], Color::TextureUsage::Albedo, i, "albedo", layerAlbedoSlot(static_cast<UINT>(i)), m_albedoGpu[i]);
            BindInternedMap(cache, desc.normal[i], Color::TextureUsage::Normal, i, "normal", layerNormalSlot(static_cast<UINT>(i)), m_normalGpu[i]);
            BindInternedMap(cache, desc.orm[i], Color::TextureUsage::Orm, i, "orm", layerOrmSlot(static_cast<UINT>(i)), m_ormGpu[i]);
        }
        return packSrvHeap(renderer);
    }

    bool TerrainMaterial::uploadSplat(Renderer& renderer, const Terrain::SplatMap& splat)
    {
        if (!splat.valid())
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial::uploadSplat: invalid splat");
            return false;
        }
        Image splatImg;
        if (!splatImg.createFromRGBA(splat.rgba(), splat.width(), splat.height(), splat.width() * 4u)
            || !m_splat.createFromImage(renderer, splatImg, Color::TextureUsage::Data))
        {
            DE_LOG_ERROR(LogCategory::Render, "TerrainMaterial: failed to upload splat");
            return false;
        }
        return packSrvHeap(renderer);
    }

    bool TerrainMaterial::applySurfaceDesc(Renderer& renderer, const Terrain::TerrainSurfaceDesc& desc)
    {
        GpuResourceCache& cache = renderer.gpuResources();
        if (!cache.ensureTerrainDefaults())
            return false;

        m_params = desc.params;
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            m_layers[i] = desc.layers[i];
            BindInternedMap(cache, desc.albedo[i], Color::TextureUsage::Albedo, i, "albedo", layerAlbedoSlot(static_cast<UINT>(i)), m_albedoGpu[i]);
            if (m_albedoGpu[i] && m_albedoGpu[i]->valid())
                m_albedoOwned[i] = Texture2D{};
            BindInternedMap(cache, desc.normal[i], Color::TextureUsage::Normal, i, "normal", layerNormalSlot(static_cast<UINT>(i)), m_normalGpu[i]);
            BindInternedMap(cache, desc.orm[i], Color::TextureUsage::Orm, i, "orm", layerOrmSlot(static_cast<UINT>(i)), m_ormGpu[i]);
        }
        return packSrvHeap(renderer);
    }

    void TerrainMaterial::copyLayerSampling(ID3D12Device* device)
    {
        if (!device || !m_heap.heap)
            return;
        const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const D3D12_CPU_DESCRIPTOR_HANDLE start = m_heap.heap->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            const Texture2D* tex = albedoTex(i);
            if (!tex)
                continue;
            const D3D12_CPU_DESCRIPTOR_HANDLE src = m_layerSamplingRaw ? tex->cpuHandleRaw() : tex->cpuHandle();
            if (src.ptr == 0)
                continue;
            D3D12_CPU_DESCRIPTOR_HANDLE dst = start;
            dst.ptr += static_cast<SIZE_T>(layerAlbedoSlot(static_cast<UINT>(i))) * incr;
            device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    void TerrainMaterial::setLayerSamplingRaw(ID3D12Device* device, bool raw)
    {
        m_layerSamplingRaw = raw;
        copyLayerSampling(device);
    }

    void TerrainMaterial::bind(ID3D12GraphicsCommandList* cmd, UINT srvTableRootIndex) const
    {
        if (!cmd || !m_heap.heap)
            return;
        ID3D12DescriptorHeap* heaps[] = { m_heap.heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootDescriptorTable(srvTableRootIndex, m_heap.gpu);
    }

    void TerrainMaterial::applySurface(TerrainFrameConstants& constants) const
    {
        constants.color[0] = 1.0f;
        constants.color[1] = 1.0f;
        constants.color[2] = 1.0f;
        constants.color[3] = 1.0f;
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            constants.layerTiling[i] = m_layers[i].tiling;
    }

    void TerrainMaterial::applySurface(TerrainGBufferConstants& constants, float worldSizeX, float worldSizeZ) const
    {
        constants.color[0] = 1.0f;
        constants.color[1] = 1.0f;
        constants.color[2] = 1.0f;
        constants.color[3] = 0.0f;
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            constants.layerTiling[i] = Terrain::layerTilingWorldScale(m_layers[i].tiling, worldSizeX, worldSizeZ);
        constants.heightBlendK    = m_params.heightBlendK;
        constants.heightBlendT    = m_params.heightBlendT;
        constants.triplanarSlope  = m_params.triplanarSlope;
        constants.layerTint0      = Terrain::packLayerTintLinear(m_layers[0].tint);
        constants.layerTint1      = Terrain::packLayerTintLinear(m_layers[1].tint);
        constants.layerTint2      = Terrain::packLayerTintLinear(m_layers[2].tint);
        constants.layerTint3      = Terrain::packLayerTintLinear(m_layers[3].tint);
    }

} // namespace Dark
