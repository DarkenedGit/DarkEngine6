#include "Render/GpuMaterial.h"
#include "Render/Texture2D.h"
#include "Core/Log.h"

namespace Dark
{

    bool GpuMaterial::pack(ID3D12Device* device, const Texture2D& albedo)
    {
        m_heap = PackedSrvHeap{};
        m_heap.shadowSlot = kShadowSlot;
        m_heap.srvCount   = kSrvCount;
        if (!albedo.valid() || albedo.cpuHandle().ptr == 0)
        {
            DE_LOG_ERROR(LogCategory::Render, "GpuMaterial::pack: albedo has no CPU SRV");
            return false;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE src[kSrvCount]{};
        src[kAlbedoSlot] = albedo.cpuHandle();
        // Slot 1 (shadow) is filled later by copyShadow.
        if (!packFromCpuHandles(device, m_heap, src, kSrvCount))
            return false;
        m_heap.shadowSlot = kShadowSlot;
        return true;
    }

    void GpuMaterial::bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const
    {
        if (!cmd || !m_heap.heap)
            return;
        ID3D12DescriptorHeap* heaps[] = { m_heap.heap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootDescriptorTable(albedoSrvRootIndex, m_heap.gpu);
    }

    void GpuMaterial::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        copyShadow(device, m_heap, shadowCpu);
    }

} // namespace Dark
