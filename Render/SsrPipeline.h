#pragma once

#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"
#include "Render/EnvironmentProvider.h"
#include "Render/GraphTargets.h"
#include "Render/SsrSettings.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    class Camera3D;

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

    // SSR's own targets, imported into the frame graph each frame (render-graph plan Phase 6 part 3).
    enum class SsrTarget : uint8_t
    {
        Half,        // trace output (ceil half res)
        Full,        // upsample + temporal output, read by lighting
        SceneColor0, // capture ping-pong (ceil half res), read by the next trace and by water
        SceneColor1,
        Debug, // ssrDebug == 2 confidence tile
    };

    // One draw. Each is its own declared graph pass binding one ring table of kSrvCount SRVs:
    //   trace:    depth, attrib, scene color, scene color, scene color -> Half
    //   upsample: Half, depth, attrib, history prev, velocity          -> Full + history curr (MRT)
    //   debug:    Full x5                                              -> Debug
    //   capture:  scene HDR x5                                         -> the capture target
    enum class SsrStep : uint8_t
    {
        Trace,
        Upsample,
        Debug,
        Capture,
    };

    struct SsrFrameParams
    {
        D3D12_GPU_VIRTUAL_ADDRESS params = 0; // SsrGpuParams
        D3D12_GPU_VIRTUAL_ADDRESS sky    = 0; // SkyEvalParams
    };

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
        static constexpr UINT kSrvCount        = 5; // every step's ring table

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
        ~SsrPipeline();
        SsrPipeline(const SsrPipeline&)            = delete;
        SsrPipeline& operator=(const SsrPipeline&) = delete;

        // Graph targets (render-graph plan Phase 5): Half/SceneColor0/1 (ceil half), Full and Debug are
        // registered output-relative; Renderer::resize recreates them. The temporal history is the
        // graph's SsrHistory pair (historyDesc), passed to draw. `targets` must outlive this.
        bool create(GraphTargets& targets);
        void release();
        static Rg::TargetDesc historyDesc();

        // Declared graph passes (render-graph plan Phase 6 part 3). The uploads write this frame's
        // constants (call them from the step's execute: the camera is jittered by then).
        SsrFrameParams uploadParams(const Camera3D& camera, const Math::Matrix4f& prevViewProj, const IEnvironmentProvider* env, const SsrSettings& settings, bool resetHistory, uint32_t frameIndex);
        SsrFrameParams uploadCaptureParams(uint32_t frameIndex);
        SsrFrameParams paramsAddress(uint32_t frameIndex) const; // what uploadParams returned this frame
        // Records `step` into rtv (and historyRtv for Upsample) from `table`. No barriers.
        void drawStep(ID3D12GraphicsCommandList* cmd, SsrStep step, const SsrFrameParams& params, D3D12_GPU_DESCRIPTOR_HANDLE table, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                      D3D12_CPU_DESCRIPTOR_HANDLE historyRtv = {}) const;

        ID3D12Resource*             resource(SsrTarget target) const;
        D3D12_CPU_DESCRIPTOR_HANDLE srv(SsrTarget target) const;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv(SsrTarget target) const;
        static const char*          name(SsrTarget target); // the registered target's name
        // The capture ping-pong at graph setup: sceneColorRead() is what this frame's trace samples
        // (the last capture); beginCapture() returns the target this frame's capture writes and
        // makes it the read one, so later readers this frame (water) see it.
        SsrTarget sceneColorRead() const;
        SsrTarget beginCapture();

        bool isValid() const;
        bool hasSceneColor() const;
        D3D12_CPU_DESCRIPTOR_HANDLE fullSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE debugConfSrvCpu() const;
        D3D12_CPU_DESCRIPTOR_HANDLE sceneColorSrvCpu() const;

    private:
        bool createPsos(ID3D12Device* device);
        bool createCbv(ID3D12Device* device);
        bool               createTargets();
        void resetTargets();
        const GraphTarget& target(SsrTarget which) const;
        void               fillParams(SsrGpuParams& out, const Camera3D& camera, const Math::Matrix4f& prevViewProj, const SsrSettings& settings, bool resetHistory, uint32_t frameIndex) const;

        GraphTargets*                m_targets = nullptr;
        ComPtr<ID3D12RootSignature>  m_rootSignature;
        ComPtr<ID3D12PipelineState>  m_psoTrace;
        ComPtr<ID3D12PipelineState>  m_psoUpsample;
        ComPtr<ID3D12PipelineState>  m_psoDownsample;
        ComPtr<ID3D12PipelineState>  m_psoDebug;
        ComPtr<ID3D12Resource>       m_cbUpload;
        uint8_t*                     m_cbMapped = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS    m_cbGpu    = 0;
        GraphTarget                  m_half;
        GraphTarget                  m_full;
        GraphTarget                  m_sceneColor[2];
        GraphTarget                  m_debug;
        UINT                         m_sceneRead            = 0;
        UINT                         m_sceneWrite           = 1;
        uint64_t                     m_sceneColorGeneration = 0; // generation of the capture hasSceneColor refers to
        bool                         m_hasSceneColor        = false;
    };

} // namespace Dark
