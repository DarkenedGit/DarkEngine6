#include "Geometry/Mesh.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <stdexcept>
#include <vector>

namespace Dark
{
namespace {

void ThrowIfFailed(HRESULT hr, const char* what)
{
    if (FAILED(hr)) {
        DE_LOG_ERROR("{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
        throw std::runtime_error(what);
    }
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
    ThrowIfFailed(
        device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr, IID_PPV_ARGS(&res)),
        "CreateCommittedResource buffer");
    return res;
}

} // namespace

Mesh Mesh::Create(Renderer& renderer, const MeshGen::MeshData& data) 
{
    if (data.positions.empty() || data.indices.empty()) 
    {
        throw std::runtime_error("Mesh::Create: empty mesh data");
    }
    if (data.normals.size() != data.positions.size())
    {
        throw std::runtime_error("Mesh::Create: normals size mismatch");
    }

    const size_t nVerts = data.positions.size();
    std::vector<MeshVertex> verts(nVerts);
    for (size_t i = 0; i < nVerts; ++i) 
    {
        verts[i].px = data.positions[i].x;
        verts[i].py = data.positions[i].y;
        verts[i].pz = data.positions[i].z;
        verts[i].nx = data.normals[i].x;
        verts[i].ny = data.normals[i].y;
        verts[i].nz = data.normals[i].z;
        if (i < data.uvs.size()) 
        {
            verts[i].u = data.uvs[i].x;
            verts[i].v = data.uvs[i].y;
        } 
        else 
        {
            verts[i].u = 0.0f;
            verts[i].v = 0.0f;
        }
    }

    ID3D12Device* device = renderer.device();
    const uint64_t vbBytes = verts.size() * sizeof(MeshVertex);
    const uint64_t ibBytes = data.indices.size() * sizeof(uint32_t);

    Mesh mesh;
    mesh.m_vertexCount = static_cast<uint32_t>(nVerts);
    mesh.m_indexCount  = static_cast<uint32_t>(data.indices.size());

    mesh.m_vb = CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
    mesh.m_ib = CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

    ComPtr<ID3D12Resource> uploadVb =
        CreateBuffer(device, vbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ComPtr<ID3D12Resource> uploadIb =
        CreateBuffer(device, ibBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

    {
        void* mapped = nullptr;
        ThrowIfFailed(uploadVb->Map(0, nullptr, &mapped), "Map VB upload");
        memcpy(mapped, verts.data(), static_cast<size_t>(vbBytes));
        uploadVb->Unmap(0, nullptr);
    }
    {
        void* mapped = nullptr;
        ThrowIfFailed(uploadIb->Map(0, nullptr, &mapped), "Map IB upload");
        memcpy(mapped, data.indices.data(), static_cast<size_t>(ibBytes));
        uploadIb->Unmap(0, nullptr);
    }

    // One-shot copy list so we don't disturb the frame-recording list.
    ComPtr<ID3D12CommandAllocator> alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ThrowIfFailed(
        device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)),
        "CreateCommandAllocator (mesh upload)");
    ThrowIfFailed(
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)),
        "CreateCommandList (mesh upload)");

    list->CopyBufferRegion(mesh.m_vb.Get(), 0, uploadVb.Get(), 0, vbBytes);
    list->CopyBufferRegion(mesh.m_ib.Get(), 0, uploadIb.Get(), 0, ibBytes);

    D3D12_RESOURCE_BARRIER barriers[2]{};
    barriers[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource   = mesh.m_vb.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource   = mesh.m_ib.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    // CopyBufferRegion leaves dest in COPY_DEST after implicit state; resources
    // started in COMMON which promotes to COPY_DEST on first copy.
    list->ResourceBarrier(2, barriers);

    ThrowIfFailed(list->Close(), "Close mesh upload list");

    ID3D12CommandList* lists[] = { list.Get() };
    renderer.queue()->ExecuteCommandLists(1, lists);
    renderer.waitForGpu();

    mesh.m_vbv.BufferLocation = mesh.m_vb->GetGPUVirtualAddress();
    mesh.m_vbv.StrideInBytes  = sizeof(MeshVertex);
    mesh.m_vbv.SizeInBytes    = static_cast<UINT>(vbBytes);

    mesh.m_ibv.BufferLocation = mesh.m_ib->GetGPUVirtualAddress();
    mesh.m_ibv.Format         = DXGI_FORMAT_R32_UINT;
    mesh.m_ibv.SizeInBytes    = static_cast<UINT>(ibBytes);

    DE_LOG_INFO("Mesh: uploaded {} verts, {} indices", mesh.m_vertexCount, mesh.m_indexCount);
    return mesh;
}

void Mesh::draw(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd || !valid())
        return;

    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->IASetVertexBuffers(0, 1, &m_vbv);
    cmd->IASetIndexBuffer(&m_ibv);
    cmd->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
}

} // namespace DE
