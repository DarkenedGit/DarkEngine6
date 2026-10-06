#pragma once

#include "Sky/Environment.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Camera3D;
class ShadowSystem;

using Microsoft::WRL::ComPtr;

enum class SkyPass : uint8_t
{
    ForwardFirst = 0, // depth off, z=0, draw first (today / PR1)
    DeferredLast,     // PR2: EQUAL, z=w, after lighting
};

struct SkyFrameConstants
{
    float cameraPos[3];
    float coverage;
    float sunDir[3];
    float turbidity;
    float sunColor[3];
    float cloudTime;
    float moonDir[3];
    float windSpeed;
    float moonColor[3];
    float rain;
    float windDir[2];
    float sunElevation;
    float exposure;
    float cameraRight[3];
    float tanHalfFovX;
    float cameraUp[3];
    float tanHalfFovY;
    float cameraLook[3];
    float fogDensity;
    float fogColor[3];
    float heightFogDensity;
    float lightColor[3];
    float heightFogFalloff;
    float ambientColor[3];
    float heightFogHeight;
    float volumetricFogDensity;
    float volumetricHeight;
    float waterLevel;
    float fogScale;
    float fogLightDir[3];
    float padFog;
};

static_assert(sizeof(SkyFrameConstants) == 56 * sizeof(float), "sky root constant size");

struct CloudLayerGpu
{
    float lightColor[4];    // rgb: light radiance at cloud altitude; a: enable
    float lightDir[4];      // xyz: dominant light dir; w: tauMax
    float skyTop[4];        // rgb: ambient from above; a: kAmb
    float skyBottom[4];     // rgb: ambient from below; a: coverage
    float geom[4];          // x: altitude (m), y: thickness (m), z: R (m), w: tMax (m)
    float wind[4];          // xy: base offset (m), zw: detail offset (m)
    float scale[4];         // x: base freq (1/m), y: detail freq, z: kErode, w: D_haze (m)
    float phase[4];         // x: g0, y: g1, z: w, w: silverLining
    float ms[4];            // x: a, y: b, z: c, w: kPowder
    float misc[4];          // x: kappa, y: rMax (m), z: albedo, w: unused
};

static_assert(sizeof(CloudLayerGpu) == 160, "cloud layer constants size");

class SkyPipeline
{
public:
    static constexpr UINT kRootConstants = 0;
    static constexpr UINT kRootShadowCbv = 1;
    static constexpr UINT kRootShadowSrv = 2;
    static constexpr UINT kRootCloudCbv  = 3;

    SkyPipeline() = default;

    bool create(ID3D12Device* device, SkyPass pass = SkyPass::ForwardFirst, DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
    void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

    void bind(ID3D12GraphicsCommandList* cmd) const;
    // exposure < 0 uses Environment::exposure(). Pass 1 on HybridDeferred (tonemap owns exposure).
    void draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const Sky::Environment& env, float exposure = -1.0f, float waterLevel = 0.0f, float fogScale = 1.0f, const ShadowSystem* shadows = nullptr) const;

    bool isValid() const { return m_pso != nullptr; }

private:
    static constexpr UINT kFrameCount    = 2;
    static constexpr UINT kCloudSlotSize = 256;

    ComPtr<ID3D12RootSignature>  m_rootSignature;
    ComPtr<ID3D12PipelineState>  m_pso;
    ComPtr<ID3D12DescriptorHeap> m_shadowHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE  m_shadowGpu{};
    ComPtr<ID3D12Resource>       m_cloudBuffer;
    uint8_t*                     m_cloudMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS    m_cloudGpu    = 0;
    uint32_t                     m_cloudSlot   = 0;
};

} // namespace Dark
