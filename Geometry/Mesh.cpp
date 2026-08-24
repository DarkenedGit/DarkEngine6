#include "Geometry/Mesh.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <vector>

namespace Dark
{
    namespace Geometry
    {
        using namespace Math;
        namespace
        {

            bool FailedHr(HRESULT hr, const char* what)
            {
                if (SUCCEEDED(hr))
                    return false;
                DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
                return true;
            }

            ComPtr<ID3D12Resource> CreateBuffer(ID3D12Device* device, uint64_t size, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState)
            {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = heapType;

                D3D12_RESOURCE_DESC desc{};
                desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
                desc.Width            = size;
                desc.Height           = 1;
                desc.DepthOrArraySize = 1;
                desc.MipLevels        = 1;
                desc.Format           = DXGI_FORMAT_UNKNOWN;
                desc.SampleDesc       = { 1, 0 };
                desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                desc.Flags            = D3D12_RESOURCE_FLAG_NONE;

                ComPtr<ID3D12Resource> res;
                if (FailedHr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&res)), "CreateCommittedResource buffer"))
                {
                    return nullptr;
                }
                return res;
            }

        } // namespace

        bool Mesh::tryCreate(Renderer& renderer, const MeshData& data, Mesh& out)
        {
            out = Mesh{};

            if (data.positions.empty() || data.indices.empty())
            {
                DE_LOG_ERROR("Mesh::tryCreate: empty mesh data");
                return false;
            }
            if (data.normals.size() != data.positions.size())
            {
                DE_LOG_ERROR("Mesh::tryCreate: normals size mismatch");
                return false;
            }

            ID3D12Device* device = renderer.device();
            if (!device)
            {
                DE_LOG_ERROR("Mesh::tryCreate: null device");
                return false;
            }

            const size_t            nVerts = data.positions.size();
            std::vector<MeshVertex> verts(nVerts);
            for (size_t i = 0; i < nVerts; ++i)
            {
                verts[i].point = data.positions[i];
                verts[i].normal = data.normals[i];
                if (i < data.uvs.size())
                {
                    verts[i].uv = data.uvs[i];
                }
                else
                {
                    verts[i].uv = Vect2f::ZERO;
                    ;
                }
            }

            const uint64_t vbBytes = verts.size() * sizeof(MeshVertex);
            const uint64_t ibBytes = data.indices.size() * sizeof(uint32_t);

            out.m_vertexCount = static_cast<uint32_t>(nVerts);
            out.m_indexCount  = static_cast<uint32_t>(data.indices.size());

            out.m_vb = CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            out.m_ib = CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            if (!out.m_vb || !out.m_ib)
            {
                out = Mesh{};
                return false;
            }

            ComPtr<ID3D12Resource> uploadVb = CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            ComPtr<ID3D12Resource> uploadIb = CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
            if (!uploadVb || !uploadIb)
            {
                out = Mesh{};
                return false;
            }

            {
                void* mapped = nullptr;
                if (FailedHr(uploadVb->Map(0, nullptr, &mapped), "Map VB upload"))
                {
                    out = Mesh{};
                    return false;
                }
                memcpy(mapped, verts.data(), static_cast<size_t>(vbBytes));
                uploadVb->Unmap(0, nullptr);
            }
            {
                void* mapped = nullptr;
                if (FailedHr(uploadIb->Map(0, nullptr, &mapped), "Map IB upload"))
                {
                    out = Mesh{};
                    return false;
                }
                memcpy(mapped, data.indices.data(), static_cast<size_t>(ibBytes));
                uploadIb->Unmap(0, nullptr);
            }

            // One-shot copy list so we don't disturb the frame-recording list.
            ComPtr<ID3D12CommandAllocator>    alloc;
            ComPtr<ID3D12GraphicsCommandList> list;
            if (FailedHr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)), "CreateCommandAllocator (mesh upload)") ||
                FailedHr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)), "CreateCommandList (mesh upload)"))
            {
                out = Mesh{};
                return false;
            }

            list->CopyBufferRegion(out.m_vb.Get(), 0, uploadVb.Get(), 0, vbBytes);
            list->CopyBufferRegion(out.m_ib.Get(), 0, uploadIb.Get(), 0, ibBytes);

            D3D12_RESOURCE_BARRIER barriers[2]{};
            barriers[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[0].Transition.pResource   = out.m_vb.Get();
            barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
            barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barriers[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[1].Transition.pResource   = out.m_ib.Get();
            barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
            barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            // CopyBufferRegion leaves dest in COPY_DEST after implicit state; resources
            // started in COMMON which promotes to COPY_DEST on first copy.
            list->ResourceBarrier(2, barriers);

            if (FailedHr(list->Close(), "Close mesh upload list"))
            {
                out = Mesh{};
                return false;
            }

            ID3D12CommandList* lists[] = { list.Get() };
            renderer.queue()->ExecuteCommandLists(1, lists);
            renderer.waitForGpu();

            out.m_vbv.BufferLocation = out.m_vb->GetGPUVirtualAddress();
            out.m_vbv.StrideInBytes  = sizeof(MeshVertex);
            out.m_vbv.SizeInBytes    = static_cast<UINT>(vbBytes);

            out.m_ibv.BufferLocation = out.m_ib->GetGPUVirtualAddress();
            out.m_ibv.Format         = DXGI_FORMAT_R32_UINT;
            out.m_ibv.SizeInBytes    = static_cast<UINT>(ibBytes);

            DE_LOG_INFO("Mesh: uploaded {} verts, {} indices", out.m_vertexCount, out.m_indexCount);
            return true;
        }

        Mesh Mesh::Create(Renderer& renderer, const MeshData& data)
        {
            Mesh mesh;
            if (!tryCreate(renderer, data, mesh))
                return Mesh{};
            return mesh;
        }

        void Mesh::draw(ID3D12GraphicsCommandList* cmd, bool pointList) const
        {
            if (!cmd || !valid())
                return;

            cmd->IASetVertexBuffers(0, 1, &m_vbv);
            if (pointList)
            {
                cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
                cmd->DrawInstanced(m_vertexCount, 1, 0, 0);
                return;
            }

            cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmd->IASetIndexBuffer(&m_ibv);
            cmd->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
        }
    } // namespace Geometry
} // namespace Dark
