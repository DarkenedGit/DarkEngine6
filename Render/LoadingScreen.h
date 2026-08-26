#pragma once

#include "Render/LoadingScreenConfig.h"
#include "Render/Texture2D.h"

#include <chrono>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <wrl/client.h>

namespace Dark
{

    struct AppConfig;
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

        bool tryLoadConfig(const AppConfig& cfg);
        void tryLoadAssets(Renderer& renderer);
        void setPhase(LoadingPhase phase);
        void skipCurrentPhaseDwell();
        float remainingDwell() const;
        LoadingPhase phase() const { return m_phase; }
        const LoadingScreenConfig& config() const { return m_config; }

    private:
        static constexpr UINT kSrvPerFrame = 2;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_pso;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        Texture2D                    m_engineLogo;
        Texture2D                    m_hostLogo;
        Texture2D                    m_font;
        LoadingScreenConfig          m_config;
        std::string                  m_versionLine;
        D3D12_GPU_DESCRIPTOR_HANDLE  m_gpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpu{};
        UINT                         m_srvIncr = 0;
        LoadingPhase                 m_phase = LoadingPhase::Engine;
        std::chrono::steady_clock::time_point m_phaseStart{};
        bool                         m_skipDwell = false;
        bool                         m_triedAssets = false;
    };

} // namespace Dark
