#pragma once

#include "Render/DebugRenderState.h"
#include "Sky/Environment.h"
#include "Water/WaterWaves.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

using Microsoft::WRL::ComPtr;

// 64 dwords — packed to the root-constant limit. Matches Water.hlsl.
struct WaterFrameConstants
{
    float worldViewProj[16];
    float cameraPos[3];
    float time;
    float lightDir[3];
    float waterLevel;
    float flowDir[2];
    float flowStrength;
    float specPower;
    float waves[4][4]; // dirX, dirZ, freq, amp
    float waveSpeed[4];
    float deepColor[3];
    float opacity;
    float shallowColor[3];
    float shoreDepth;
    float skyZenith[3];
    float fresnelF0;
    float skyHorizon[3];
    float steepness;
};

static_assert(sizeof(WaterFrameConstants) == 64 * sizeof(float), "water root constant size");

class WaterPipeline
{
public:
    static constexpr UINT kRootConstants = 0;

    WaterPipeline() = default;

    bool create(ID3D12Device* device, DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM);

    void bind(ID3D12GraphicsCommandList* cmd, DebugFill fill = DebugFill::Solid) const;
    void setConstants(ID3D12GraphicsCommandList* cmd, const WaterFrameConstants& constants) const;

    bool isValid() const
    {
        return m_psoSolid != nullptr && m_psoWire != nullptr && m_psoPoint != nullptr;
    }

    static void fillConstants(
        WaterFrameConstants& out,
        const float worldViewProj[16],
        const float cameraPos[3],
        float time,
        const float lightDir[3],
        const Water::WaterParams& params,
        const Sky::Environment* env = nullptr,
        bool lighting = true);

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_psoSolid;
    ComPtr<ID3D12PipelineState> m_psoWire;
    ComPtr<ID3D12PipelineState> m_psoPoint;
};

} // namespace Dark
