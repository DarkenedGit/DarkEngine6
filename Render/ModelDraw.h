#pragma once

#include "Animation/Pose.h"
#include "Math/Matrix4f.h"
#include "Render/MeshPipeline.h"
#include "Render/SkinnedMeshPipeline.h"
#include "Render/SkinningUploadRing.h"

#include <d3d12.h>

namespace Dark
{

    class Model;
    class ShadowSystem;
    struct AnimGraphComponent;

    using Math::Matrix4f;

    const AnimPose* skinnedPose(const Model& model, const AnimGraphComponent* ag);

    void drawModelOpaqueGBuffer(
        ID3D12GraphicsCommandList* cmd,
        const MeshPipeline& pipeline,
        const Model& model,
        const Matrix4f& world,
        const Matrix4f& viewProj,
        const Matrix4f& prevViewProj,
        DebugFill fill = DebugFill::Solid);

    void drawModelForward(
        ID3D12GraphicsCommandList* cmd,
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
        const ShadowSystem& shadows,
        int cascade,
        const Model& model,
        const Matrix4f& world);

    void drawSkinnedModelOpaqueGBuffer(
        ID3D12GraphicsCommandList* cmd,
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

    void drawSkinnedModelForward(
        ID3D12GraphicsCommandList* cmd,
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
        const ShadowSystem& shadows,
        int cascade,
        const SkinnedMeshPipeline& skinnedShadow,
        SkinningUploadRing& ring,
        const Model& model,
        const AnimPose& pose,
        const Matrix4f& world);

} // namespace Dark
