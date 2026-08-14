#pragma once

#include "Sky/Environment.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

class Camera3D;

using Microsoft::WRL::ComPtr;

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
    float pad;
};

static_assert(sizeof(SkyFrameConstants) == 36 * sizeof(float), "sky root constant size");

class SkyPipeline
{
public:
    static constexpr UINT kRootConstants = 0;

    SkyPipeline() = default;

    bool create(ID3D12Device* device);

    void bind(ID3D12GraphicsCommandList* cmd) const;
    void draw(ID3D12GraphicsCommandList* cmd, const Camera3D& camera, const Sky::Environment& env) const;

    bool isValid() const { return m_pso != nullptr; }

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
};

} // namespace Dark
