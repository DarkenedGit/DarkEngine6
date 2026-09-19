#pragma once

#include <d3d12.h>

namespace Dark
{

    inline constexpr float kDepthClear = 0.0f;

    inline D3D12_COMPARISON_FUNC sceneDepthFunc()
    {
        return D3D12_COMPARISON_FUNC_GREATER;
    }

    inline D3D12_COMPARISON_FUNC sceneDepthFuncGreaterEqual()
    {
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    }

    inline D3D12_COMPARISON_FUNC skyDepthFunc()
    {
        return D3D12_COMPARISON_FUNC_EQUAL;
    }

    inline D3D12_COMPARISON_FUNC shadowDepthFunc()
    {
        return D3D12_COMPARISON_FUNC_GREATER;
    }

    inline D3D12_COMPARISON_FUNC shadowCmpFunc()
    {
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    }

} // namespace Dark
