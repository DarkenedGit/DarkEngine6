#pragma once

#include "Animation/Pose.h"
#include "Math/Matrix4f.h"
#include "Render/GpuResourceCache.h"
#include "Render/GpuModel.h"
#include "Render/MeshPipeline.h"
#include "Render/SkinnedInstanceBuffer.h"
#include "Render/SkinnedMeshPipeline.h"
#include "Render/SkinningUploadRing.h"

#include <d3d12.h>

namespace Dark
{

    class GpuResourceCache;
    class Model;
    class ShadowPipeline;
    class ShadowSystem;
    struct AnimGraphComponent;

    using Math::Matrix4f;

    const AnimPose* skinnedPose(const Model& model, const AnimGraphComponent* ag);

    // Opaque skinned parts only, all of them or none. Does not call SkinningUploadRing::alloc.
    // world and prevWorld are baked here as part.localToRoot times the entity matrix.
    bool submitSkinnedModelOpaque(
        SkinnedInstanceBuffer& buffer,
        GpuResourceCache& gpu,
        const GpuModel& model,
        const AnimPose* pose,
        const Matrix4f& entityWorld,
        const Matrix4f& prevEntityWorld,
        StaticPopulation population,
        bool castShadow);

    void drawModelOpaqueGBuffer(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const MeshPipeline& pipeline,
        const Model& model,
        const Matrix4f& world,
        const Matrix4f& viewProj,
        const Matrix4f& prevViewProj,
        DebugFill fill = DebugFill::Solid);

    void drawModelForward(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const MeshPipeline& pipeline,
        const ShadowSystem& shadows,
        const Model& model,
        bool translucentOnly,
        const Matrix4f& world,
        const Matrix4f& viewProj,
        const MeshFrameConstants& lighting,
        DebugFill fill = DebugFill::Solid);

    void drawModelDepth(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const ShadowPipeline& pipeline,
        const Matrix4f& lightViewProj,
        const Model& model,
        const Matrix4f& world);

    void drawModelDepth(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const ShadowSystem& shadows,
        int cascade,
        const Model& model,
        const Matrix4f& world);

    void drawSkinnedModelOpaqueGBuffer(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const SkinnedMeshPipeline& skinned,
        const MeshPipeline& staticPipeline,
        SkinningUploadRing& ring,
        const Model& model,
        const AnimPose& pose,
        const Matrix4f& world,
        const Matrix4f& prevWorld,
        const Matrix4f& viewProj,
        const Matrix4f& prevViewProj,
        DebugFill fill = DebugFill::Solid);

    // Skips parts with skinned == false. Used when those parts were accepted by DrawList.
    void drawSkinnedModelForwardSkinnedOnly(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const SkinnedMeshPipeline& skinned,
        const MeshPipeline& staticPipeline,
        const ShadowSystem& shadows,
        SkinningUploadRing& ring,
        const Model& model,
        const AnimPose& pose,
        const Matrix4f& world,
        const Matrix4f& viewProj,
        const MeshFrameConstants& lighting,
        DebugFill fill = DebugFill::Solid);

    void drawSkinnedModelForward(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const SkinnedMeshPipeline& skinned,
        const MeshPipeline& staticPipeline,
        const ShadowSystem& shadows,
        SkinningUploadRing& ring,
        const Model& model,
        bool translucentOnly,
        const AnimPose& pose,
        const Matrix4f& world,
        const Matrix4f& viewProj,
        const MeshFrameConstants& lighting,
        DebugFill fill = DebugFill::Solid);

    void drawSkinnedModelDepth(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const ShadowPipeline& staticDepth,
        const Matrix4f& lightViewProj,
        const SkinnedMeshPipeline& skinnedShadow,
        SkinningUploadRing& ring,
        const Model& model,
        const AnimPose& pose,
        const Matrix4f& world);

    void drawSkinnedModelDepth(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const ShadowSystem& shadows,
        int cascade,
        const SkinnedMeshPipeline& skinnedShadow,
        SkinningUploadRing& ring,
        const Model& model,
        const AnimPose& pose,
        const Matrix4f& world);

    // Skips parts with skinned == false. Used when those parts were accepted by DrawList.
    void drawSkinnedModelOpaqueGBufferSkinnedOnly(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const SkinnedMeshPipeline& skinned,
        const MeshPipeline& staticPipeline,
        SkinningUploadRing& ring,
        const Model& model,
        const AnimPose& pose,
        const Matrix4f& world,
        const Matrix4f& prevWorld,
        const Matrix4f& viewProj,
        const Matrix4f& prevViewProj,
        DebugFill fill = DebugFill::Solid);

    void drawSkinnedModelDepthSkinnedOnly(
        ID3D12GraphicsCommandList* cmd,
        GpuResourceCache& gpu,
        const ShadowPipeline& staticDepth,
        const Matrix4f& lightViewProj,
        const SkinnedMeshPipeline& skinnedShadow,
        SkinningUploadRing& ring,
        const Model& model,
        const AnimPose& pose,
        const Matrix4f& world);

} // namespace Dark
