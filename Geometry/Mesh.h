#pragma once

#include "Geometry/MeshGen.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{
    class Renderer;

    namespace Geometry
    {
        using Microsoft::WRL::ComPtr;

        // Interleaved vertex matching the BasicMesh shader input layout.
        struct MeshVertex
        {
            float px, py, pz;
            float nx, ny, nz;
            float u, v;
        };

        // GPU mesh built from MeshGen::MeshData (default-heap VB/IB).
        class Mesh
        {
        public:
            Mesh() = default;

            // Uploads mesh data to the GPU. Blocks until the copy completes.
            // Returns an invalid mesh on failure (does not throw).
            static Mesh Create(Renderer& renderer, const MeshData& data);

            // Non-throwing upload. On failure `out` is left empty and false is returned.
            static bool tryCreate(Renderer& renderer, const MeshData& data, Mesh& out);

            // pointList: POINTLIST + DrawInstanced(vertexCount). Shadow capture
            // must keep the default (indexed triangle list).
            void draw(ID3D12GraphicsCommandList* cmd, bool pointList = false) const;

            uint32_t indexCount() const
            {
                return m_indexCount;
            }
            uint32_t vertexCount() const
            {
                return m_vertexCount;
            }
            bool valid() const
            {
                return m_vb != nullptr && m_indexCount > 0;
            }

        private:
            ComPtr<ID3D12Resource>   m_vb;
            ComPtr<ID3D12Resource>   m_ib;
            D3D12_VERTEX_BUFFER_VIEW m_vbv{};
            D3D12_INDEX_BUFFER_VIEW  m_ibv{};
            uint32_t                 m_indexCount  = 0;
            uint32_t                 m_vertexCount = 0;
        };
    } // namespace Geometry
} // namespace Dark
