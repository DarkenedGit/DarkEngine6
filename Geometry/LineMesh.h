#pragma once

#include "Geometry/MeshGen.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Renderer;

using Microsoft::WRL::ComPtr;

// GPU line-list mesh (positions only) for grids / debug draw.
class LineMesh
{
public:
    LineMesh() = default;

    static LineMesh Create(Renderer& renderer, const MeshGen::LineMeshData& data);

    void draw(ID3D12GraphicsCommandList* cmd) const;

    bool     valid()      const { return m_vb != nullptr && m_indexCount > 0; }
    uint32_t indexCount() const { return m_indexCount; }

private:
    ComPtr<ID3D12Resource>   m_vb;
    ComPtr<ID3D12Resource>   m_ib;
    D3D12_VERTEX_BUFFER_VIEW m_vbv{};
    D3D12_INDEX_BUFFER_VIEW  m_ibv{};
    uint32_t                 m_indexCount  = 0;
    uint32_t                 m_vertexCount = 0;
};

} // namespace Dark
