#include "Render/PsoUtil.h"
#include "Core/Log.h"

namespace Dark
{

    namespace
    {

        bool FailedHr(HRESULT hr, const char* what)
        {
            if (SUCCEEDED(hr))
                return false;
            DE_LOG_ERROR(LogCategory::Render, "{} failed (HRESULT 0x{:08X})", what, static_cast<unsigned>(hr));
            return true;
        }

    } // namespace

    bool createFillVariantPsos(
        ID3D12Device* device,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& solidTriangle,
        ComPtr<ID3D12PipelineState>& outSolid,
        ComPtr<ID3D12PipelineState>& outWire,
        ComPtr<ID3D12PipelineState>& outPoints)
    {
        outSolid.Reset();
        outWire.Reset();
        outPoints.Reset();
        if (!device)
            return false;

        if (FailedHr(
                device->CreateGraphicsPipelineState(&solidTriangle, IID_PPV_ARGS(&outSolid)),
                "CreateGraphicsPipelineState (solid)"))
        {
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC wire = solidTriangle;
        wire.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        wire.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        if (FailedHr(
                device->CreateGraphicsPipelineState(&wire, IID_PPV_ARGS(&outWire)),
                "CreateGraphicsPipelineState (wireframe)"))
        {
            outSolid.Reset();
            return false;
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC points = solidTriangle;
        points.RasterizerState.FillMode        = D3D12_FILL_MODE_SOLID;
        points.RasterizerState.CullMode        = D3D12_CULL_MODE_NONE;
        points.PrimitiveTopologyType           = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        if (FailedHr(
                device->CreateGraphicsPipelineState(&points, IID_PPV_ARGS(&outPoints)),
                "CreateGraphicsPipelineState (points)"))
        {
            outSolid.Reset();
            outWire.Reset();
            return false;
        }

        return true;
    }

} // namespace Dark
