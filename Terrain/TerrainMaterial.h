#pragma once

#include "Render/Texture2D.h"
#include "Render/TerrainPipeline.h"
#include "Terrain/SplatMap.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Renderer;

using Microsoft::WRL::ComPtr;

// Four albedo layers + one RGBA splat map packed into a single SRV heap
// matching TerrainPipeline::kSrvCount.
class TerrainMaterial
{
public:
    TerrainMaterial() = default;

    TerrainMaterial(TerrainMaterial&&) noexcept            = default;
    TerrainMaterial& operator=(TerrainMaterial&&) noexcept = default;
    TerrainMaterial(const TerrainMaterial&)            = delete;
    TerrainMaterial& operator=(const TerrainMaterial&) = delete;

    bool createDefault(Renderer& renderer, const Terrain::SplatMap& splat);

    bool create(
        Renderer& renderer,
        Texture2D layers[Terrain::kMaxTerrainLayers],
        Texture2D&& splat,
        const Terrain::TerrainLayerDesc layerDescs[Terrain::kMaxTerrainLayers]);

    void bind(ID3D12GraphicsCommandList* cmd, UINT srvTableRootIndex) const;
    void applySurface(TerrainFrameConstants& constants) const;
    void applySurface(TerrainGBufferConstants& constants) const;
    void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

    bool isValid() const { return m_srvHeap != nullptr && m_splat.valid(); }

    Terrain::TerrainLayerDesc&       layer(int i) { return m_layers[i]; }
    const Terrain::TerrainLayerDesc& layer(int i) const { return m_layers[i]; }

private:
    bool packSrvHeap(ID3D12Device* device);

    Texture2D                     m_layerTex[Terrain::kMaxTerrainLayers];
    Texture2D                     m_splat;
    Terrain::TerrainLayerDesc     m_layers[Terrain::kMaxTerrainLayers];
    ComPtr<ID3D12DescriptorHeap>  m_srvHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE   m_gpuHandle{};
};

} // namespace Dark
