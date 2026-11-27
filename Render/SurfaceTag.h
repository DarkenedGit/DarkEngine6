#pragma once

#include <cstdint>

// Generic per-surface G-buffer channel (render-graph plan §3.4.2). R is an 8-bit tag, G is an
// optional material aux mask (today's gUv, kept generic). The target is R8G8_UNORM.

namespace Dark
{

    // Engine surface categories. These are the engine's own defaults, not game names. A material
    // may override the tag with an opaque uint8_t (0–255); 0 is a valid tag (StaticMesh).
    enum class SurfaceCategory : uint8_t
    {
        StaticMesh  = 0,
        SkinnedMesh = 1,
        Terrain     = 2,
        Grass       = 3,
        Foliage     = 4,
    };

    inline constexpr uint32_t kSurfaceTagLutSize     = 256;
    inline constexpr uint32_t kSurfaceTagOverrideBit = 1u << 8;

    // Packed into the last MeshGBufferConstants DWORD (skinned G-buffer is at the 64-DWORD cap).
    // Bit 8 set means "use bits 0–7"; otherwise the pipeline's DE_SURFACE_CATEGORY default is used.
    inline constexpr uint32_t packSurfaceTagOverride(uint8_t tag)
    {
        return kSurfaceTagOverrideBit | static_cast<uint32_t>(tag);
    }
    inline constexpr bool surfaceTagOverridePacked(uint32_t packed)
    {
        return (packed & kSurfaceTagOverrideBit) != 0;
    }
    inline constexpr uint8_t surfaceTagPackedValue(uint32_t packed)
    {
        return static_cast<uint8_t>(packed & 0xffu);
    }

} // namespace Dark
