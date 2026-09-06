#pragma once

#include "Render/DebugRenderState.h"
#include "Render/LocalLightGather.h"
#include "Sky/Environment.h"
#include "Water/WaterWaves.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

using Microsoft::WRL::ComPtr;

// CPU/HLSL layout: 64 floats plus lightCount + waterIndex[8]. Lives in a CBV — root constants are the 64-DWORD cap.
struct WaterFrameConstants
{
    float    worldViewProj[16];
    float    cameraPos[3];
    float    time;
    float    lightDir[3];
    float    waterLevel;
    float    flowDir[2];
    float    flowStrength;
    float    specPower;
    float    waves[4][4]; // dirX, dirZ, freq, amp
    float    waveSpeed[4];
    float    deepColor[3];
    float    opacity;
    float    shallowColor[3];
    float    shoreDepth;
    float    skyZenith[3];
    float    fresnelF0;
    float    skyHorizon[3];
    float    steepness;
    uint32_t lightCount;
    uint32_t waterIndex[kWaterLocalLightMax];
};

static_assert(sizeof(WaterFrameConstants) == 64 * sizeof(float) + sizeof(uint32_t) * (1 + kWaterLocalLightMax), "water CBV payload");
static_assert(offsetof(WaterFrameConstants, lightCount) == 64 * sizeof(float), "lightCount follows the old 64-float block");
static_assert(offsetof(WaterFrameConstants, waterIndex) == 64 * sizeof(float) + sizeof(uint32_t), "waterIndex packs tightly after lightCount");

class WaterPipeline
{
public:
    static constexpr UINT kRootCbv       = 0;
    static constexpr UINT kRootLightsSrv = 1;
    static constexpr UINT kBufferedFrames = 2;

    WaterPipeline() = default;

    bool create(ID3D12Device* device, DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM);

    void bind(ID3D12GraphicsCommandList* cmd, DebugFill fill = DebugFill::Solid) const;
    void setConstants(ID3D12GraphicsCommandList* cmd, const WaterFrameConstants& constants, uint32_t frameIndex);
    void setLights(ID3D12GraphicsCommandList* cmd, D3D12_GPU_VIRTUAL_ADDRESS lightsVa) const;

    D3D12_GPU_VIRTUAL_ADDRESS dummyLightsGpuVa() const { return m_dummyGpu; }

    bool isValid() const
    {
        return m_psoSolid != nullptr && m_psoWire != nullptr && m_psoPoint != nullptr && m_cbUpload != nullptr && m_cbMapped != nullptr;
    }

    static void fillConstants(
        WaterFrameConstants& out,
        const float worldViewProj[16],
        const float cameraPos[3],
        float time,
        const float lightDir[3],
        const WaterParams& params,
        const Sky::Environment* env = nullptr,
        bool lighting = true);

private:
    bool createConstantBuffers(ID3D12Device* device);
    UINT cbBytes() const;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_psoSolid;
    ComPtr<ID3D12PipelineState> m_psoWire;
    ComPtr<ID3D12PipelineState> m_psoPoint;
    ComPtr<ID3D12Resource>      m_cbUpload;
    ComPtr<ID3D12Resource>      m_dummyLights;
    UINT8*                      m_cbMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS   m_cbGpu    = 0;
    D3D12_GPU_VIRTUAL_ADDRESS   m_dummyGpu = 0;
    uint32_t                    m_cbSlot   = 0;
};

} // namespace Dark
