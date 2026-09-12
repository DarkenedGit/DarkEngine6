#pragma once

#include "Render/BloomPipeline.h"
#include "Render/Camera3D.h"
#include "Render/DebugOverlay.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/LocalLightGpuList.h"
#include "Render/LocalLightVolumePipeline.h"
#include "Render/Mesh.h"
#include "Render/MeshPipeline.h"
#include "Render/MotionBlurPipeline.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedMeshPipeline.h"
#include "Render/SkinningUploadRing.h"
#include "Render/SkyPipeline.h"
#include "Render/TaaPipeline.h"
#include "Render/TerrainPipeline.h"
#include "Render/TonemapPipeline.h"
#include "Render/WaterPipeline.h"

#include <cstdint>

namespace Dark
{

class Renderer;
class World;

// Shared mesh / lighting / post / (optional) terrain-water-sky pipelines used by
// Sandbox and Editor. Hosts still own world draw order; this owns pipeline
// lifetime and the deferred present/post path (bloom → TAA → motion blur → tonemap).
struct SceneRendererDesc
{
    // When true, also create TerrainPipeline / WaterPipeline / SkyPipeline (Sandbox).
    bool createWorldEnvironment = false;
    // Log prefix for create failures (e.g. "SandboxApp", "EditorApp").
    const char* logTag = "SceneRenderer";
};

class SceneRenderer
{
public:
    SceneRenderer() = default;

    bool init(Renderer& renderer, const SceneRendererDesc& desc = {});
    void shutdown();

    // TAA jitter + view-proj bookkeeping for the current camera.
    // Returns the jittered (or plain) viewProj; stores previous for velocity/post.
    Math::Matrix4f beginCameraFrame(Camera3D& camera, Renderer& renderer);
    void           endCameraFrame(Camera3D& camera, const Math::Matrix4f& viewProj);

    bool wantsTaa(const Renderer& renderer) const;

    // Bloom (resize as needed) → TAA → motion blur → tonemap to the swap chain color target.
    // Caller must have finished HDR scene draws. tonemap.usePostHdr is set by this helper.
    void applyPost(Renderer& renderer, ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& viewProj, TonemapSettings tonemap);

    void drawDeferredLighting(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const LightingConstants& lc) const;
    void drawLocalLights(
        ID3D12GraphicsCommandList* cmd,
        Renderer&                 renderer,
        World&                    world,
        const Camera3D&           camera,
        const Math::Matrix4f&     viewProj,
        const LightingConstants&  lc);

    // --- Pipeline accessors (hosts use these for special / world draws) ---
    MeshPipeline&             meshPipeline() { return m_meshPipeline; }
    MeshPipeline&             meshTransparentPipeline() { return m_meshTransparentPipeline; }
    SkinnedMeshPipeline&      skinnedPipeline() { return m_skinnedPipeline; }
    SkinnedMeshPipeline&      skinnedTransparentPipeline() { return m_skinnedTransparentPipeline; }
    SkinnedMeshPipeline&      skinnedShadowPipeline() { return m_skinnedShadowPipeline; }
    SkinningUploadRing&       skinRing() { return m_skinRing; }
    TonemapPipeline&          tonemap() { return m_tonemap; }
    DeferredLightingPipeline& lighting() { return m_lighting; }
    LocalLightVolumePipeline& localLightVolumes() { return m_localLightVolumes; }
    LocalLightGpuList&        localLightGpu() { return m_localLightGpu; }
    Mesh&                     pointVolumeMesh() { return m_pointVolumeMesh; }
    Mesh&                     spotVolumeMesh() { return m_spotVolumeMesh; }
    BloomPipeline&            bloom() { return m_bloom; }
    MotionBlurPipeline&       motionBlur() { return m_motionBlur; }
    TaaPipeline&              taa() { return m_taa; }
    ShadowSystem&             shadows() { return m_shadows; }
    DebugOverlay&             debugOverlay() { return m_debugOverlay; }

    TerrainPipeline& terrainPipeline() { return m_terrainPipeline; }
    WaterPipeline&   waterPipeline() { return m_waterPipeline; }
    SkyPipeline&     skyPipeline() { return m_skyPipeline; }

    const MeshPipeline&             meshPipeline() const { return m_meshPipeline; }
    const MeshPipeline&             meshTransparentPipeline() const { return m_meshTransparentPipeline; }
    const SkinnedMeshPipeline&      skinnedPipeline() const { return m_skinnedPipeline; }
    const SkinnedMeshPipeline&      skinnedTransparentPipeline() const { return m_skinnedTransparentPipeline; }
    const SkinnedMeshPipeline&      skinnedShadowPipeline() const { return m_skinnedShadowPipeline; }
    const SkinningUploadRing&       skinRing() const { return m_skinRing; }
    const TonemapPipeline&          tonemap() const { return m_tonemap; }
    const DeferredLightingPipeline& lighting() const { return m_lighting; }
    const LocalLightVolumePipeline& localLightVolumes() const { return m_localLightVolumes; }
    const LocalLightGpuList&        localLightGpu() const { return m_localLightGpu; }
    const Mesh&                     pointVolumeMesh() const { return m_pointVolumeMesh; }
    const Mesh&                     spotVolumeMesh() const { return m_spotVolumeMesh; }
    const BloomPipeline&            bloom() const { return m_bloom; }
    const MotionBlurPipeline&       motionBlur() const { return m_motionBlur; }
    const TaaPipeline&              taa() const { return m_taa; }
    const ShadowSystem&             shadows() const { return m_shadows; }
    const DebugOverlay&             debugOverlay() const { return m_debugOverlay; }

    const TerrainPipeline& terrainPipeline() const { return m_terrainPipeline; }
    const WaterPipeline&   waterPipeline() const { return m_waterPipeline; }
    const SkyPipeline&     skyPipeline() const { return m_skyPipeline; }

    const Math::Matrix4f& prevViewProj() const { return m_prevViewProj; }
    bool                  havePrevViewProj() const { return m_havePrevViewProj; }
    bool                  hasWorldEnvironment() const { return m_haveWorldEnv; }
    bool                  isValid() const { return m_initialized; }

private:
    bool createCorePipelines(Renderer& renderer, const char* tag);
    bool createPostPipelines(Renderer& renderer, const char* tag);
    bool createWorldEnvPipelines(Renderer& renderer, const char* tag);

    MeshPipeline             m_meshPipeline;
    MeshPipeline             m_meshTransparentPipeline;
    SkinnedMeshPipeline      m_skinnedPipeline;
    SkinnedMeshPipeline      m_skinnedTransparentPipeline;
    SkinnedMeshPipeline      m_skinnedShadowPipeline;
    SkinningUploadRing       m_skinRing;
    TonemapPipeline          m_tonemap;
    DeferredLightingPipeline m_lighting;
    LocalLightVolumePipeline m_localLightVolumes;
    LocalLightGpuList        m_localLightGpu;
    Mesh                     m_pointVolumeMesh;
    Mesh                     m_spotVolumeMesh;
    BloomPipeline            m_bloom;
    MotionBlurPipeline       m_motionBlur;
    TaaPipeline              m_taa;
    ShadowSystem             m_shadows;
    DebugOverlay             m_debugOverlay;

    TerrainPipeline m_terrainPipeline;
    WaterPipeline   m_waterPipeline;
    SkyPipeline     m_skyPipeline;
    bool            m_haveWorldEnv = false;

    Math::Matrix4f m_prevViewProj{};
    bool           m_havePrevViewProj = false;
    bool           m_taaHistoryValid  = false;
    uint32_t       m_taaHistoryW      = 0;
    uint32_t       m_taaHistoryH      = 0;
    uint32_t       m_bloomW           = 0;
    uint32_t       m_bloomH           = 0;
    bool           m_initialized      = false;
};

} // namespace Dark
