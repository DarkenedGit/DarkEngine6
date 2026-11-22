#pragma once

#include <d3d12.h>

namespace Dark
{
    // CPU views (non-shader-visible, so legal CopyDescriptors sources) the declared lighting passes
    // copy into their descriptor-ring tables (render-graph plan Phase 6 part 4). SceneBuffers fills it;
    // a view the host has not set is a null SRV of the right dimension.
    struct LightingViews
    {
        D3D12_CPU_DESCRIPTOR_HANDLE albedo{}; // UNORM_SRGB (raw UNORM with setLightingAlbedoRaw)
        D3D12_CPU_DESCRIPTOR_HANDLE attrib{};
        D3D12_CPU_DESCRIPTOR_HANDLE shadow{}; // always the null view: passes without a shadow map bind it
        D3D12_CPU_DESCRIPTOR_HANDLE height{};
        D3D12_CPU_DESCRIPTOR_HANDLE iblIrradiance{};
        D3D12_CPU_DESCRIPTOR_HANDLE iblPrefilter{};
        D3D12_CPU_DESCRIPTOR_HANDLE iblBrdfLut{};
        D3D12_CPU_DESCRIPTOR_HANDLE localShadow{};
        D3D12_CPU_DESCRIPTOR_HANDLE localShadowRecords{};
    };
} // namespace Dark
