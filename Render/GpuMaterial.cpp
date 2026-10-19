#include "Render/GpuMaterial.h"
#include "Render/Texture2D.h"
#include "Core/Log.h"

namespace Dark
{

    bool GpuMaterial::pack(ID3D12Device* device, const Texture2D& albedo, const Texture2D& normal, const Texture2D& orm, const Texture2D& emissive, const Texture2D& uv)
    {
        m_heap = PackedSrvHeap{};
        m_heap.shadowSlot = kShadowSlot;
        m_heap.srvCount   = kSrvCount;
        auto requireSrv = [](const Texture2D& tex, const char* name) -> bool {
            if (tex.valid() && tex.cpuHandle().ptr != 0)
                return true;
            DE_LOG_ERROR(LogCategory::Render, "GpuMaterial::pack: {} has no CPU SRV", name);
            return false;
        };
        if (!requireSrv(albedo, "albedo") || !requireSrv(normal, "normal") || !requireSrv(orm, "orm") || !requireSrv(emissive, "emissive") || !requireSrv(uv, "uv"))
            return false;
        D3D12_CPU_DESCRIPTOR_HANDLE src[kSrvCount]{};
        src[kAlbedoSlot]   = albedo.cpuHandle();
        src[kNormalSlot]   = normal.cpuHandle();
        src[kOrmSlot]      = orm.cpuHandle();
        src[kEmissiveSlot] = emissive.cpuHandle();
        src[kUvSlot]       = uv.cpuHandle();
        // Shadow is the last slot, filled later by copyShadow.
        if (!packFromCpuHandles(device, m_heap, src, kSrvCount))
            return false;
        m_heap.shadowSlot = kShadowSlot;
        return true;
    }

    void GpuMaterial::setPackedMapIds(AssetID albedo, AssetID normal, AssetID orm, AssetID emissive, AssetID uv)
    {
        m_packedMapIds[kAlbedoSlot]   = albedo;
        m_packedMapIds[kNormalSlot]   = normal;
        m_packedMapIds[kOrmSlot]      = orm;
        m_packedMapIds[kEmissiveSlot] = emissive;
        m_packedMapIds[kUvSlot]       = uv;
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
