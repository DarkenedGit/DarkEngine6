#include "Render/Material.h"
#include "Assets/AssetManager.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include <cstring>

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

    } // namespace

    Material::Material()
    {
        type = AssetType::Material;
    }

    bool Material::packSrvHeap(ID3D12Device* device)
    {
        m_srvHeap.Reset();
        m_gpuHandle = {};
        if (!device || !m_albedo || !m_albedo->valid())
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = MeshPipeline::kSrvCount;
        heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FailedHr(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap)), "CreateDescriptorHeap material SRVs"))
            return false;

        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        device->CopyDescriptorsSimple(1, dst, m_albedo->cpuHandle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        // Slot 1 (shadow) is filled later by setShadowSrv.

        m_gpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        return true;
    }

    void Material::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        if (!device || !m_srvHeap || shadowCpu.ptr == 0)
            return;

        const UINT incr = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += static_cast<SIZE_T>(incr);
        device->CopyDescriptorsSimple(1, dst, shadowCpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    bool Material::createFromAlbedoPath(Renderer& renderer, AssetManager& assets, const std::string& virtualAlbedoPath, uint8_t fallbackR, uint8_t fallbackG, uint8_t fallbackB, uint8_t fallbackA)
    {
        type = AssetType::Material;

        m_albedo = assets.loadTexture(renderer, virtualAlbedoPath);
        if (m_albedo && m_albedo->valid())
        {
            m_baseColor[0] = 1.0f;
            m_baseColor[1] = 1.0f;
            m_baseColor[2] = 1.0f;
            m_baseColor[3] = 1.0f;
            if (!packSrvHeap(renderer.device()))
            {
                DE_LOG_ERROR("Material: failed to pack SRV heap for '{}'", virtualAlbedoPath);
                return false;
            }
            DE_LOG_INFO("Material: albedo '{}' (cached {}x{})", virtualAlbedoPath, m_albedo->width(), m_albedo->height());
            return true;
        }

        DE_LOG_WARN("Material: failed to load albedo '{}' — solid fallback ({},{},{},{})", virtualAlbedoPath, fallbackR, fallbackG, fallbackB, fallbackA);

        m_albedo = assets.loadSolidTexture(renderer, fallbackR, fallbackG, fallbackB, fallbackA);
        if (!m_albedo || !m_albedo->valid())
        {
            DE_LOG_ERROR("Material: solid fallback create failed");
            return false;
        }

        m_baseColor[0] = 1.0f;
        m_baseColor[1] = 1.0f;
        m_baseColor[2] = 1.0f;
        m_baseColor[3] = 1.0f;
        if (!packSrvHeap(renderer.device()))
        {
            DE_LOG_ERROR("Material: failed to pack SRV heap for solid fallback");
            return false;
        }
        return true;
    }

    bool Material::createSolid(Renderer& renderer, AssetManager& assets, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        type = AssetType::Material;
        m_albedo = assets.textureCache().loadSolid(renderer, r, g, b, a);
        if (!m_albedo || !m_albedo->valid())
            return false;

        m_baseColor[0] = 1.0f;
        m_baseColor[1] = 1.0f;
        m_baseColor[2] = 1.0f;
        m_baseColor[3] = 1.0f;
        return packSrvHeap(renderer.device());
    }

    void Material::bind(ID3D12GraphicsCommandList* cmd, UINT albedoSrvRootIndex) const
    {
        if (!cmd || !m_srvHeap)
            return;
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootDescriptorTable(albedoSrvRootIndex, m_gpuHandle);
    }

    void Material::applySurface(MeshFrameConstants& constants) const
    {
        std::memcpy(constants.color, m_baseColor, sizeof(m_baseColor));
    }

    void Material::setBaseColor(float r, float g, float b, float a)
    {
        m_baseColor[0] = r;
        m_baseColor[1] = g;
        m_baseColor[2] = b;
        m_baseColor[3] = a;
    }

    uint64_t Material::sortKey() const
    {
        // Low bits: asset id. High bits reserved for future pipeline / blend mode.
        return id;
    }

} // namespace Dark
