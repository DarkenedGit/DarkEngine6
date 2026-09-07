#pragma once

#include "Math/Matrix4f.h"
#include "Render/MeshPipeline.h"

#include <d3d12.h>

namespace Dark
{

    class Model;
    class ShadowSystem;

    using Math::Matrix4f;

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

} // namespace Dark
