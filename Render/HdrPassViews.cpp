#include "Render/HdrPassViews.h"

#include "Core/Log.h"
#include "Render/LightingViews.h"
#include "Render/Renderer.h"
#include "Render/RgViews.h"

namespace Dark
{

    void bindHdr(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rgRtv(c, hdr);
        c.cmd()->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        c.cmd()->RSSetViewports(1, &renderer.viewport());
        c.cmd()->RSSetScissorRects(1, &renderer.scissor());
    }

    void bindHdrDepthRead(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr, Rg::TextureHandle depth)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rgRtv(c, hdr);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = cpuHandle(c.dsv(depth, true));
        c.cmd()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        c.cmd()->RSSetViewports(1, &renderer.viewport());
        c.cmd()->RSSetScissorRects(1, &renderer.scissor());
    }

    void bindHdrDepthWrite(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle hdr, Rg::TextureHandle depth)
    {
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rgRtv(c, hdr);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = cpuHandle(c.dsv(depth, false));
        c.cmd()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        c.cmd()->RSSetViewports(1, &renderer.viewport());
        c.cmd()->RSSetScissorRects(1, &renderer.scissor());
    }

    D3D12_CPU_DESCRIPTOR_HANDLE shadowSrv(const Rg::PassContext& c, const LightingViews& v, Rg::TextureHandle shadow)
    {
        return shadow.valid() ? rgSrvCpu(c, shadow) : v.shadow;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE shadowTable(Rg::PassContext& c, const Renderer& renderer, Rg::TextureHandle shadow)
    {
        return rgTable(c, { shadowSrv(c, renderer.lightingViews(), shadow) });
    }

    void logMissingSceneTargets(const char* entry)
    {
        static bool logged = false;
        if (logged)
            return;
        logged = true;
        DE_LOG_ERROR(LogCategory::Render, "SceneRenderer: {} has no scene targets to draw into (logs once)", entry ? entry : "?");
    }

} // namespace Dark
