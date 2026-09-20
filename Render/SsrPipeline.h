#pragma once

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/SsrSettings.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Renderer;
    class Camera3D;

    namespace Sky
    {
        class Environment;
    }

    using Microsoft::WRL::ComPtr;

    struct SsrGpuParams
    {
        float invSizeHalf[2];
        float invSizeFull[2];
        float invViewProj[16];
        float viewProj[16];
        float reprojection[16];
        float nearZ;
        float thickness;
        float stride;
        float maxRoughness;
        float edgeFade;
        float reset;
        float frameOffset;
        float hasSkyEval;
        float cameraPos[3];
        float _padCam;
    };
    static_assert(sizeof(SsrGpuParams) == 64 * sizeof(float), "SsrGpuParams is 64 floats (256-byte CBV)");

    struct SkyEvalParams
    {
        float sunDir[3];
        float coverage;
        float sunColor[3];
        float turbidity;
        float moonDir[3];
        float rain;
        float moonColor[3];
        float windSpeed;
        float windDir[2];
        float sunElevation;
        float exposure;
        float cloudTime;
        float _pad[3];
    };
    static_assert(sizeof(SkyEvalParams) == 24 * sizeof(float), "SkyEvalParams is 24 floats");

    enum class SsrMissKind : uint32_t
    {
        Hit       = 0,
        Sky       = 1,
        Offscreen = 2,
        MaxDistance = 3,
    };

    struct SsrRaySetup
    {
        bool        ready = false;
        SsrMissKind miss  = SsrMissKind::Sky;
        float       lenPx = 0.0f;
        float       clipW0 = 0.0f;
        float       clipW1 = 0.0f;
    };

    inline bool ssrIsSkyDepth(float d)
    {
        return d <= 0.0f;
    }

    inline float ssrClampDepthForReconstruct(float d)
    {
        return d > 1.0e-7f ? d : 1.0e-7f;
    }

    inline float ssrLinearizeViewZ(float depth, float nearZ)
    {
        return nearZ / ssrClampDepthForReconstruct(depth);
    }

    inline bool ssrPassesRoughGate(float roughness, float maxRoughness)
    {
        return roughness <= maxRoughness;
    }

    inline float ssrSaturate(float x)
    {
        return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
    }

    inline float ssrEdgeFadeAt(float u, float v, float edgeFade)
    {
        const float e  = edgeFade > 1.0e-5f ? edgeFade : 1.0e-5f;
        const float fu = ssrSaturate((u < 1.0f - u ? u : 1.0f - u) / e);
        const float fv = ssrSaturate((v < 1.0f - v ? v : 1.0f - v) / e);
        return fu * fv;
    }

    inline float ssrPerspectiveCorrectViewZ(float w0, float w1, float t)
    {
        const float tt   = ssrSaturate(t);
        const float inv0 = 1.0f / w0;
        const float inv1 = 1.0f / w1;
        return 1.0f / (inv0 + (inv1 - inv0) * tt);
    }

    inline bool ssrHitViewZ(float rayViewZ, float sceneViewZ, float thickness)
    {
        return rayViewZ >= sceneViewZ && (rayViewZ - sceneViewZ) <= thickness;
    }

    inline Math::Matrix4f ssrReprojectionMatrix(const Math::Matrix4f& viewProj, const Math::Matrix4f& prevViewProj)
    {
        return viewProj.Inverse() * prevViewProj;
    }

    inline Math::Vector3f ssrReconstructWorld(float ndcX, float ndcY, float depth, const Math::Matrix4f& invViewProj)
    {
        const Math::Vector4f w  = invViewProj * Math::Vector4f(ndcX, ndcY, ssrClampDepthForReconstruct(depth), 1.0f);
        const float          iw = (w.w > 1.0e-6f || w.w < -1.0e-6f) ? (1.0f / w.w) : 0.0f;
        return Math::Vector3f(w.x * iw, w.y * iw, w.z * iw);
    }

    SsrRaySetup ssrSetupDda(const Math::Vector3f& worldPos, const Math::Vector3f& R, const Math::Matrix4f& viewProj, float nearZ, float resX, float resY,
                            float rayLength);

    class SsrPipeline
    {
    public:
        static constexpr UINT kRootCbv         = 0;
        static constexpr UINT kRootSrv         = 1;
        static constexpr UINT kRootSkyCbv      = 2;
        static constexpr UINT kFrameCount      = 2;
        static constexpr UINT kCbBytes         = 256;
        static constexpr UINT kSkyEvalCbBytes  = 256;
        static constexpr UINT kCbSlotsPerFrame = 2; // draw + capture; upload CBVs are not snapshotted at bind

        static UINT cbvByteOffset(uint32_t frameIndex, bool capture)
        {
            const UINT frame = frameIndex % kFrameCount;
            return (frame * kCbSlotsPerFrame + (capture ? 1u : 0u)) * kCbBytes;
        }

        static UINT skyEvalCbvByteOffset(uint32_t frameIndex)
        {
            const UINT frame = frameIndex % kFrameCount;
            return kFrameCount * kCbSlotsPerFrame * kCbBytes + frame * kSkyEvalCbBytes;
        }

        static UINT cbUploadBytes()
        {
            return kFrameCount * kCbSlotsPerFrame * kCbBytes + kFrameCount * kSkyEvalCbBytes;
        }

        SsrPipeline() = default;

        bool create(ID3D12Device* device, uint32_t width, uint32_t height);
        bool resize(ID3D12Device* device, uint32_t width, uint32_t height);
        void draw(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const Sky::Environment* env,
                  const SsrSettings& settings, bool resetHistory);
        void captureSceneColor(ID3D12GraphicsCommandList* cmd, Renderer& renderer);

        bool isValid() const;
        bool hasSceneColor() const;
        D3D12_CPU_DESCRIPTOR_HANDLE fullSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE debugConfSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSrvCpu() const;
        void transitionFull(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);
        void transitionDebug(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES after);

    private:
        static constexpr UINT kRtvCount    = 7;
        static constexpr UINT kCpuSrvCount = 7;
        static constexpr UINT kSrvPerPass  = 5;
        static constexpr UINT kPassCount   = 4;
        static constexpr UINT kRtvHalf     = 0;
        static constexpr UINT kRtvFull     = 1;
        static constexpr UINT kRtvHistory0 = 2;
        static constexpr UINT kRtvHistory1 = 3;
        static constexpr UINT kRtvScene0   = 4;
        static constexpr UINT kRtvScene1   = 5;
        static constexpr UINT kRtvDebug    = 6;
        static constexpr UINT kCpuSrvHalf  = 0;
        static constexpr UINT kCpuSrvFull  = 1;
        static constexpr UINT kCpuSrvHist0 = 2;
        static constexpr UINT kCpuSrvHist1 = 3;
        static constexpr UINT kCpuSrvScene0 = 4;
        static constexpr UINT kCpuSrvScene1 = 5;
        static constexpr UINT kCpuSrvDebug  = 6;
        static constexpr UINT kPassDownsample = 0;
        static constexpr UINT kPassTrace      = 1;
        static constexpr UINT kPassUpsample   = 2;
        static constexpr UINT kPassDebug      = 3;

        struct Target
        {
            ComPtr<ID3D12Resource> res;
            D3D12_RESOURCE_STATES  state = D3D12_RESOURCE_STATE_COMMON;
        };

        bool createPsos(ID3D12Device* device);
        bool createCbv(ID3D12Device* device);
        bool createTargets(ID3D12Device* device, uint32_t width, uint32_t height);
        bool createColorTarget(ID3D12Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, const float clearColor[4], const wchar_t* name,
                               Target& out);
        void resetTargets();
        void resetAll();
        void transition(ID3D12GraphicsCommandList* cmd, Target& t, D3D12_RESOURCE_STATES after) const;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu(UINT slot) const;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuSrv(UINT slot) const;
        D3D12_GPU_DESCRIPTOR_HANDLE passSrvGpu(UINT frame, UINT pass) const;
        D3D12_CPU_DESCRIPTOR_HANDLE passSrvCpu(UINT frame, UINT pass) const;
        void copySrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE dst, D3D12_CPU_DESCRIPTOR_HANDLE src) const;
        void fillParams(SsrGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const SsrSettings& settings, bool resetHistory,
                        uint32_t frameIndex) const;
        void bindPass(ID3D12GraphicsCommandList* cmd, ID3D12PipelineState* pso, UINT nRtv, const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs, uint32_t w, uint32_t h,
                      D3D12_GPU_DESCRIPTOR_HANDLE tableGpu) const;

        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoTrace;
        ComPtr<ID3D12PipelineState>  m_psoUpsample;
        ComPtr<ID3D12PipelineState>  m_psoDownsample;
        ComPtr<ID3D12PipelineState>  m_psoDebug;
        ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> m_srvHeap;
        ComPtr<ID3D12DescriptorHeap> m_cpuSrvHeap;
        ComPtr<ID3D12Resource>       m_cbUpload;
        uint8_t*                     m_cbMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE  m_rtvStart{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_srvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE  m_srvGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE  m_cpuSrvStart{};
        UINT                         m_rtvIncr = 0;
        UINT                         m_srvIncr = 0;
        uint32_t                     m_width   = 0;
        uint32_t                     m_height  = 0;
        uint32_t                     m_halfW   = 0;
        uint32_t                     m_halfH   = 0;
        Target                       m_half;
        Target                       m_full;
        Target                       m_history[2];
        Target                       m_sceneColor[2];
        Target                       m_debug;
        UINT                         m_historyIndex   = 0;
        UINT                         m_sceneRead      = 0;
        UINT                         m_sceneWrite     = 1;
        bool                         m_needReset      = true;
        bool                         m_hasSceneColor  = false;
        bool                         m_loggedSkip     = false;
    };

} // namespace Dark
