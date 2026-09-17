#include "Terrain/TerrainMaterial.h"
#include "Assets/Image.h"
#include "Render/GpuResourceCache.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

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
            m_layerTex[i] = std::move(other.m_layerTex[i]);
            m_layers[i]   = other.m_layers[i];
        }
        m_splat              = std::move(other.m_splat);
        m_heap               = std::move(other.m_heap);
        m_layerSamplingRaw   = other.m_layerSamplingRaw;
        m_cache              = other.m_cache;
        other.m_cache            = nullptr;
        other.m_layerSamplingRaw = false;
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

    bool TerrainMaterial::packSrvHeap(Renderer& renderer)
    {
        unbindCache();
        m_heap = PackedSrvHeap{};
        ID3D12Device* device = renderer.device();
        if (!device)
            return false;

        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            if (!m_layerTex[i].valid())
            {
                DE_LOG_ERROR("TerrainMaterial: layer {} is invalid", i);
                return false;
            }
        }
        if (!m_splat.valid())
        {
            DE_LOG_ERROR("TerrainMaterial: splat is invalid");
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE src[TerrainPipeline::kSrvCount]{};
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            src[i] = m_layerTex[i].cpuHandle();
        src[kSplatSlot] = m_splat.cpuHandle();
        // Slot 5 (shadow) filled by GpuResourceCache::setShadowSrv.
        if (!packFromCpuHandles(device, m_heap, src, TerrainPipeline::kSrvCount))
            return false;
        m_heap.shadowSlot    = kShadowSlot;
        m_heap.srvCount      = TerrainPipeline::kSrvCount;
        m_layerSamplingRaw   = false; // packed from cpuHandle()
        m_cache              = &renderer.gpuResources();
        m_cache->registerPackedHeap(&m_heap);
        return true;
    }

    bool TerrainMaterial::createDefault(Renderer& renderer, const Terrain::SplatMap& splat)
    {
        if (!splat.valid())
        {
            DE_LOG_ERROR("TerrainMaterial::createDefault: invalid splat");
            return false;
        }

        if (!CreateChecker(renderer, m_layerTex[0], 132, 96, 58, 110, 78, 44)
            || !CreateChecker(renderer, m_layerTex[1], 62, 122, 48, 48, 98, 38)
            || !CreateChecker(renderer, m_layerTex[2], 118, 114, 108, 88, 86, 82)
            || !CreateChecker(renderer, m_layerTex[3], 236, 240, 244, 210, 218, 226))
        {
            DE_LOG_ERROR("TerrainMaterial: failed to create layer textures");
            return false;
        }

        Image splatImg;
        if (!splatImg.createFromRGBA(splat.rgba(), splat.width(), splat.height(), splat.width() * 4u)
            || !m_splat.createFromImage(renderer, splatImg, Color::TextureUsage::Data))
        {
            DE_LOG_ERROR("TerrainMaterial: failed to upload splat");
            return false;
        }

        m_layers[0].tiling = 24.0f;
        m_layers[1].tiling = 20.0f;
        m_layers[2].tiling = 16.0f;
        m_layers[3].tiling = 12.0f;

        return packSrvHeap(renderer);
    }

    bool TerrainMaterial::create(
        Renderer& renderer,
        Texture2D layers[Terrain::kMaxTerrainLayers],
        Texture2D&& splat,
        const Terrain::TerrainLayerDesc layerDescs[Terrain::kMaxTerrainLayers])
    {
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            m_layerTex[i] = std::move(layers[i]);
        m_splat = std::move(splat);

        if (layerDescs)
        {
            for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
                m_layers[i] = layerDescs[i];
        }

        return packSrvHeap(renderer);
    }

    void TerrainMaterial::setLayerSamplingRaw(ID3D12Device* device, bool raw)
    {
        const bool changed = m_layerSamplingRaw != raw;
        m_layerSamplingRaw = raw;
        if (!changed || !device || !m_heap.heap)
            return;
        const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_heap.heap->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
        {
            const D3D12_CPU_DESCRIPTOR_HANDLE src = raw ? m_layerTex[i].cpuHandleRaw() : m_layerTex[i].cpuHandle();
            if (src.ptr != 0)
                device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            dst.ptr += static_cast<SIZE_T>(incr);
        }
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

    void TerrainMaterial::applySurface(TerrainGBufferConstants& constants) const
    {
        constants.color[0] = 1.0f;
        constants.color[1] = 1.0f;
        constants.color[2] = 1.0f;
        constants.color[3] = 0.0f;
        for (int i = 0; i < Terrain::kMaxTerrainLayers; ++i)
            constants.layerTiling[i] = m_layers[i].tiling;
    }

} // namespace Dark
