#pragma once

#include "Render/Texture2D.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;

    using Microsoft::WRL::ComPtr;

    enum class LoadingPhase : uint8_t
    {
        Engine,
        Host,
        FadeOut,
        Done
    };

    struct LoadingScreenConstants
    {
        float timeSec;
        float fade;            // 1 = opaque splash
        float phase;           // 0 engine, 1 host, 2 fade
        float reducedMotion;   // 0/1
        float background[4];
        float spinnerColor[3]; // RGB
        float pass;            // 0 = logo/spinner fullscreen, 1 = font
        float resolution[2];   // back-buffer pixels
        float logoAspect;      // texel width/height; 1 if 1x1 fallback
        float spinnerOpacity;
    };

    static_assert(sizeof(LoadingScreenConstants) == 16 * sizeof(float));

    struct LoadingDrawState
    {
        LoadingPhase phase         = LoadingPhase::Engine;
        float        timeSec       = 0.0f;
        float        fade          = 1.0f;
        bool         reducedMotion = false;
        const char*  versionLine   = "";
    };

    class LoadingScreen
    {
    public:
        static constexpr UINT kRootConstants = 0;
        static constexpr UINT kRootSrv       = 1;

        bool create(Renderer& renderer);
        void draw(Renderer& renderer, const LoadingDrawState& state);
        void shutdown(Renderer& renderer);
        bool isReady() const { return m_pso != nullptr; }

    private:
        static constexpr UINT kSrvPerFrame = 2;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        Texture2D                    m_logo;
        Texture2D                    m_font;
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        UINT                         m_srvIncr = 0;
    };

} // namespace Dark
