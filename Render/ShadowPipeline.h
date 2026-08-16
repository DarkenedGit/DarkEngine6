#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

using Microsoft::WRL::ComPtr;

class ShadowPipeline
{
public:
    static constexpr UINT kRootWvp = 0;

    ShadowPipeline() = default;

    bool create(ID3D12Device* device);

    void bind(ID3D12GraphicsCommandList* cmd) const;
    void setWvp(ID3D12GraphicsCommandList* cmd, const float wvp[16]) const;

    bool isValid() const { return m_pso != nullptr; }

private:
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pso;
};

} // namespace Dark
