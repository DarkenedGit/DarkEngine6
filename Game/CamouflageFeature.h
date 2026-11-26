#pragma once

#include "Game/CamouflagePipeline.h"
#include "Render/SceneRenderer.h"

#include <d3d12.h>

namespace Dark
{
    class Renderer;

    // The camouflaged body in the game slot (SceneRenderer::addGameFeature): an HDR copy, then the
    // body, which samples the copy and writes depth.
    class CamouflageFeature final : public IGameFeature
    {
    public:
        // The host's body draw, `fn(ctx, pass, sceneTable, draws)`, with HDR and writable depth bound;
        // `sceneTable` holds the HDR copy (CamouflagePipeline::drawModel). Held by reference: the
        // callable must outlive execute().
        struct Draw
        {
            void* ctx                                                                                                           = nullptr;
            void (*fn)(void* ctx, Rg::PassContext& pass, D3D12_GPU_DESCRIPTOR_HANDLE sceneTable, const WorldDrawContext& draws) = nullptr;

            template <class F> static Draw of(F& f)
            {
                return { const_cast<void*>(static_cast<const void*>(&f)),
                         [](void* c, Rg::PassContext& pass, D3D12_GPU_DESCRIPTOR_HANDLE sceneTable, const WorldDrawContext& draws) { (*static_cast<F*>(c))(pass, sceneTable, draws); } };
            }
            void operator()(Rg::PassContext& pass, D3D12_GPU_DESCRIPTOR_HANDLE sceneTable, const WorldDrawContext& draws) const
            {
                fn(ctx, pass, sceneTable, draws);
            }
        };

        // False (logged as an error with `tag`) when the pipeline cannot be created; camouflage is
        // then disabled.
        bool create(Renderer& renderer, const char* tag);
        void release();

        bool isValid() const
        {
            return m_pipeline.isValid();
        }
        CamouflagePipeline& pipeline()
        {
            return m_pipeline;
        }

        // This frame's body draw; empty (camouflage off) adds no passes. Only the next addPasses
        // uses it, so set it every frame.
        void setFrameDraw(const Draw& draw)
        {
            m_draw = draw;
        }

        void addPasses(FeatureBuilder& b, Renderer& renderer, const WorldDrawContext& draws) override;

    private:
        CamouflagePipeline m_pipeline;
        Draw               m_draw;
    };

} // namespace Dark
