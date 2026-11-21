#include "Render/FrameComposer.h"

#include "Render/Renderer.h"

#include <d3d12.h>

namespace Dark
{

    void FrameComposer::begin(Renderer& renderer, const FeaturePass* list, uint32_t count, ID3D12Resource* shadowMap, ID3D12Resource* localShadowArray)
    {
        m_renderer                 = &renderer;
        Rg::CompileOptions options = m_graph.options();
        options.asyncCompute       = false; // one graphics queue until Phase 10
        m_graph.setOptions(options);
        m_graph.beginFrame(renderer.width(), renderer.height());
        m_features.begin(list, count);
        importResource(RenderResource::Swap, renderer.backBufferResource());
        importResource(RenderResource::Depth, renderer.depthResource());
        importResource(RenderResource::Shadow, shadowMap);
        importResource(RenderResource::LocalShadow, localShadowArray);
        importResource(RenderResource::Albedo, renderer.albedoResource());
        importResource(RenderResource::Attrib, renderer.attribResource());
        importResource(RenderResource::Velocity, renderer.velocityResource());
        importResource(RenderResource::Ao, renderer.aoResource());
        importResource(RenderResource::Hdr, renderer.hdr());
        importResource(RenderResource::Post, renderer.postResource());
    }

    void FrameComposer::importResource(RenderResource resource, ID3D12Resource* native)
    {
        // The real resource and its real size and format; a missing target imports as an empty slot.
        Rg::TextureDesc desc;
        desc.sizeMode = Rg::SizeMode::Absolute;
        desc.width    = m_renderer->width();
        desc.height   = m_renderer->height();
        if (native)
        {
            const D3D12_RESOURCE_DESC rd = native->GetDesc();
            desc.width                   = static_cast<uint32_t>(rd.Width);
            desc.height                  = static_cast<uint32_t>(rd.Height);
            desc.format                  = static_cast<uint32_t>(rd.Format);
            desc.mips                    = static_cast<uint16_t>(rd.MipLevels);
            desc.arraySize               = static_cast<uint16_t>(rd.DepthOrArraySize);
        }
        m_features.importResource(m_graph, resource, native, desc);
    }

    bool FrameComposer::execute(const char* host)
    {
        if (!m_renderer)
            return false;
        if (!m_features.compileFrame(m_graph, host))
            return false;
        m_executor.beginFrame(*m_renderer);
        if (!m_graph.execute(&m_executor))
            return false;
        // No finish(): the main list is closed and submitted by Renderer::endFrame (after the shadow list).
        return m_executor.ok();
    }

} // namespace Dark
