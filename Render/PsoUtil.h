#pragma once

#include "Render/DebugRenderState.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace Dark
{

    using Microsoft::WRL::ComPtr;

    // Builds solid / wireframe / point PSOs from a solid-triangle base desc.
    // Wireframe keeps triangle topology (FillMode WIREFRAME, no cull).
    // Points switch PrimitiveTopologyType to POINT (1px vertices).
    bool createFillVariantPsos(
        ID3D12Device* device,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& solidTriangle,
        ComPtr<ID3D12PipelineState>& outSolid,
        ComPtr<ID3D12PipelineState>& outWire,
        ComPtr<ID3D12PipelineState>& outPoints);

    inline ID3D12PipelineState* selectFillPso(
        DebugFill fill,
        ID3D12PipelineState* solid,
        ID3D12PipelineState* wire,
        ID3D12PipelineState* points)
    {
        if (fill == DebugFill::Wireframe)
            return wire;
        if (fill == DebugFill::Points)
            return points;
        return solid;
    }

} // namespace Dark
