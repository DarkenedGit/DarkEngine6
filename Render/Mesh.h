#pragma once

#include "Assets/MeshData.h"

#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Dark
{
    class Renderer;

    using Microsoft::WRL::ComPtr;

    // Interleaved vertex matching the BasicMesh shader input layout.
    struct MeshVertex
    {
        Math::Vector3f point;   // 0
        Math::Vector3f normal;  // 12
        Math::Vector2f uv;      // 24
        Math::Vector4f tangent; // 32 xyz + w bitangent sign
    };
    static_assert(sizeof(MeshVertex) == 48, "static VB stride");

    struct SkinnedMeshVertex
    {
        Math::Vector3f point;         // 0
        Math::Vector3f normal;        // 12
        Math::Vector2f uv;            // 24
        Math::Vector4f tangent;       // 32
        uint32_t       joints;        // 48
        uint32_t       packedWeights; // 52
        uint32_t       pad[2];        // 56; 8 bytes → 64
    };
    static_assert(sizeof(SkinnedMeshVertex) == 64, "skinned VB stride");

    uint32_t packBlendWeightsUnorm8(float w0, float w1, float w2, float w3);

    // GPU mesh built from MeshData (default-heap VB/IB).
    class Mesh
    {
    public:
        Mesh() = default;

        // Uploads mesh data to the GPU. The copy is queued (COPY queue when
        // available); the CPU does not wait. Returns an invalid mesh on failure.
        [[nodiscard]] static Mesh Create(Renderer& renderer, const MeshData& data);

        // Non-throwing upload. On failure `out` is left empty and false is returned.
        static bool tryCreate(Renderer& renderer, const MeshData& data, Mesh& out);
        static bool tryCreateSkinned(Renderer& renderer, const MeshData& data, Mesh& out);

        // pointList: POINTLIST + DrawInstanced(vertexCount). Shadow capture
        // must keep the default (indexed triangle list).
        void draw(ID3D12GraphicsCommandList* cmd, bool pointList = false) const;
        void drawInstanced(ID3D12GraphicsCommandList* cmd, uint32_t instanceCount) const;

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

        // Hands default-heap buffers to the renderer. tryCreate returns before the
        // copy queue finishes, so dropping a Mesh immediately deletes them early.
        void deferRelease(Renderer& renderer);

    private:
        static bool uploadBuffers(Renderer& renderer, const void* verts, uint64_t vbBytes, uint32_t stride, const uint32_t* indices, uint32_t indexCount, Mesh& out);

        ComPtr<ID3D12Resource>   m_vb;
        ComPtr<ID3D12Resource>   m_ib;
        D3D12_VERTEX_BUFFER_VIEW m_vbv{};
        D3D12_INDEX_BUFFER_VIEW  m_ibv{};
        uint32_t                 m_indexCount  = 0;
        uint32_t                 m_vertexCount = 0;
    };

    // Keep a Mesh's GPU buffers alive for a few frames after swap-out so the
    // in-flight draw that still references them cannot hit OBJECT_DELETED.
    class GpuMeshRetire
    {
    public:
        static constexpr int kFrames = 3;

        void push(Mesh&& mesh);
        void takeFrom(GpuMeshRetire& other);
        void tick();
        void clear() { m_items.clear(); }

    private:
        struct Item
        {
            Mesh mesh;
            int  frames = kFrames;
        };
        std::vector<Item> m_items;
    };
} // namespace Dark
