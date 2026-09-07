#include "Render/ModelDraw.h"
#include "Assets/Model.h"
#include "Render/ShadowSystem.h"

#include <cstring>

namespace Dark
{
    namespace
    {
        void copyMatrix(float dst[16], const Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
        }
    } // namespace

    void drawModelOpaqueGBuffer(
        ID3D12GraphicsCommandList* cmd,
        const MeshPipeline& pipeline,
        const Model& model,
        const Matrix4f& world,
        const Matrix4f& viewProj,
        const Matrix4f& prevViewProj,
        DebugFill fill)
    {
        if (!cmd || !model.hasOpaque())
            return;
        pipeline.bind(cmd, fill);
        const bool points = fill == DebugFill::Points;
        for (const Model::Part& part : model.opaque())
        {
            if (!part.mesh.valid())
                continue;
            const Matrix4f w = part.localToRoot * world;
            if (part.material && part.material->isValid())
                part.material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
            MeshGBufferConstants cb{};
            copyMatrix(cb.worldViewProj, w * viewProj);
            copyMatrix(cb.world, w);
            copyMatrix(cb.prevWorldViewProj, w * prevViewProj);
            if (part.material)
                part.material->applySurface(cb);
            else
            {
                cb.color[0] = 1.0f;
                cb.color[1] = 1.0f;
                cb.color[2] = 1.0f;
                cb.roughness = part.roughness;
                cb.metallic  = part.metallic;
            }
            pipeline.setGBufferConstants(cmd, cb);
            part.mesh.draw(cmd, points);
        }
    }

    void drawModelForward(
        ID3D12GraphicsCommandList* cmd,
        const MeshPipeline& pipeline,
        const ShadowSystem& shadows,
        const Model& model,
        bool translucentOnly,
        const Matrix4f& world,
        const Matrix4f& viewProj,
        const MeshFrameConstants& lighting,
        DebugFill fill)
    {
        if (!cmd)
            return;
        const std::vector<Model::Part>& parts = translucentOnly ? model.translucent() : model.opaque();
        if (parts.empty())
            return;
        pipeline.bind(cmd, fill);
        shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
        const bool points = fill == DebugFill::Points;
        for (const Model::Part& part : parts)
        {
            if (!part.mesh.valid())
                continue;
            const Matrix4f w = part.localToRoot * world;
            if (part.material && part.material->isValid())
                part.material->bind(cmd, MeshPipeline::kRootAlbedoSrv);
            MeshFrameConstants cb = lighting;
            copyMatrix(cb.worldViewProj, w * viewProj);
            copyMatrix(cb.world, w);
            if (part.material)
                part.material->applySurface(cb);
            pipeline.setConstants(cmd, cb);
            part.mesh.draw(cmd, points);
        }
    }

    void drawModelDepth(
        ID3D12GraphicsCommandList* cmd,
        const ShadowSystem& shadows,
        int cascade,
        const Model& model,
        const Matrix4f& world)
    {
        if (!cmd || !model.hasOpaque())
            return;
        for (const Model::Part& part : model.opaque())
        {
            if (!part.mesh.valid())
                continue;
            const Matrix4f w   = part.localToRoot * world;
            const Matrix4f wvp = w * shadows.cascade(cascade).viewProj;
            shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
            part.mesh.draw(cmd);
        }
    }

} // namespace Dark
