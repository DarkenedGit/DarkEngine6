#include "Terrain/TerrainMaterial.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <utility>
#include <vector>

namespace Dark
{

namespace
{

bool FailedHr(HRESULT hr, const char* what)
{
    if (SUCCEEDED(hr))
        return false;
    DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
    return true;
}

// Cheap tileable checker so layer tiling is visible without art assets.
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
            const bool alt = ((x / cell) + (y / cell)) & 1u;
            const size_t i = (static_cast<size_t>(y) * size + x) * 4u;
            px[i + 0] = alt ? r1 : r0;
            px[i + 1] = alt ? g1 : g0;
            px[i + 2] = alt ? b1 : b0;
            px[i + 3] = 255;
        }
    }
    return out.createFromRGBA(renderer, px.data(), size, size, size * 4u);
}

} // namespace

bool TerrainMaterial::packSrvHeap(ID3D12Device* device)
{
    m_srvHeap.Reset();
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

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.NumDescriptors = TerrainPipeline::kSrvCount;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FailedHr(
            device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)),
            "CreateDescriptorHeap terrain SRVs"))
    {
        return false;
    }

    const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    Texture2D* src[] = {
        &m_layerTex[0], &m_layerTex[1], &m_layerTex[2], &m_layerTex[3], &m_splat
    };
    for (UINT i = 0; i < 5; ++i)
    {
        device->CopyDescriptorsSimple(1, dst, src[i]->cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        dst.ptr += incr;
    }
    // Slot 5 (shadow) is filled later by setShadowSrv.

    m_gpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    return true;
}

void TerrainMaterial::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
{
    if (!device || !m_srvHeap || shadowCpu.ptr == 0)
        return;

    const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    dst.ptr += static_cast<SIZE_T>(incr) * 5u;
    device->CopyDescriptorsSimple(1, dst, shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

bool TerrainMaterial::createDefault(Renderer& renderer, const Terrain::SplatMap& splat)
{
    if (!splat.valid())
    {
        DE_LOG_ERROR("TerrainMaterial::createDefault: invalid splat");
        return false;
    }

    // Dirt, grass, rock, snow — two-tone checkers so tiling reads clearly.
    if (!CreateChecker(renderer, m_layerTex[0], 132, 96, 58, 110, 78, 44)
        || !CreateChecker(renderer, m_layerTex[1], 62, 122, 48, 48, 98, 38)
        || !CreateChecker(renderer, m_layerTex[2], 118, 114, 108, 88, 86, 82)
        || !CreateChecker(renderer, m_layerTex[3], 236, 240, 244, 210, 218, 226))
    {
        DE_LOG_ERROR("TerrainMaterial: failed to create layer textures");
        return false;
    }

    if (!m_splat.createFromRGBA(renderer, splat.rgba(), splat.width(), splat.height(), splat.width() * 4u))
    {
        DE_LOG_ERROR("TerrainMaterial: failed to upload splat");
        return false;
    }

    m_layers[0].tiling = 24.0f;
    m_layers[1].tiling = 20.0f;
    m_layers[2].tiling = 16.0f;
    m_layers[3].tiling = 12.0f;

    return packSrvHeap(renderer.device());
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

    return packSrvHeap(renderer.device());
}

void TerrainMaterial::bind(ID3D12GraphicsCommandList* cmd, UINT srvTableRootIndex) const
{
    if (!cmd || !m_srvHeap)
        return;
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmd->SetDescriptorHeaps(1, heaps);
    cmd->SetGraphicsRootDescriptorTable(srvTableRootIndex, m_gpuHandle);
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

} // namespace Dark
