#pragma once
#include <string>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    struct PipelineDesc
    {
        std::string vertexShaderPath;
        std::string pixelShaderPath;
        bool        depthWrite = true;
        bool        depthTest  = true;
        bool        wireframe  = false;
    };

    // Graphics pipeline state object (PSO) + root signature.
    // Full HLSL compile / PSO creation lands here once shaders are authored.
    class Pipeline
    {
    public:
        explicit Pipeline(const PipelineDesc& desc);
        ~Pipeline();

        Pipeline(const Pipeline&)            = delete;
        Pipeline& operator=(const Pipeline&) = delete;

        void bind(ID3D12GraphicsCommandList* cmd) const;

        ID3D12PipelineState* pso() const
        {
            return m_pso.Get();
        }
        ID3D12RootSignature* rootSignature() const
        {
            return m_rootSignature.Get();
        }
        bool isValid() const
        {
            return m_pso != nullptr;
        }

    private:
        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pso;
    };

} // namespace Dark
