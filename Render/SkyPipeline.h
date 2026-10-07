#pragma once

#include "Sky/Environment.h"

#include <cstddef>
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
    float clLightColor[4];
    float clLightDir[4];
    float clSkyTop[4];
    float clSkyBottom[4];
    float clWind[4];
};

static_assert(sizeof(SkyFrameConstants) == 76 * sizeof(float), "sky CBV is 76 floats");
static_assert(offsetof(SkyFrameConstants, clLightColor) == 56 * sizeof(float), "cloud tail follows the 56-float prefix");
static_assert(offsetof(SkyFrameConstants, clWind) == offsetof(SkyFrameConstants, clLightColor) + 16 * sizeof(float), "cloud tail is contiguous");

class SkyPipeline
{
public:
    static constexpr UINT kRootCbv            = 0;
    static constexpr UINT kRootShadowCbv      = 1;
    static constexpr UINT kRootShadowSrv      = 2;
    static constexpr UINT kRootParameterCount = 3;

    SkyPipeline() = default;

    bool create(ID3D12Device* device, SkyPass pass = SkyPass::ForwardFirst, DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM);
    void setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu);

    void bind(ID3D12GraphicsCommandList* cmd) const;
    // Writes slot frameIndex % 2. exposure < 0 uses Environment::exposure(). Pass 1 on HybridDeferred.
    void upload(uint32_t frameIndex, const Camera3D& camera, const Sky::Environment& env, float exposure = -1.0f, float waterLevel = 0.0f, float fogScale = 1.0f);
    void draw(ID3D12GraphicsCommandList* cmd, uint32_t frameIndex, const ShadowSystem* shadows = nullptr) const;

    bool isValid() const { return m_pso != nullptr; }
    bool hasCloudLayer() const { return m_psoCloud != nullptr; }

private:
    static constexpr UINT kFrameCount   = 2;
    static constexpr UINT kSkySlotBytes = 512; // 76 floats align up to a 256-byte CBV

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12PipelineState> m_psoCloud;
    ComPtr<ID3D12DescriptorHeap> m_shadowHeap;
    D3D12_GPU_DESCRIPTOR_HANDLE m_shadowGpu{};
    ComPtr<ID3D12Resource>      m_skyCb;
    uint8_t*                    m_skyMapped = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS   m_skyGpu    = 0;
};

} // namespace Dark
