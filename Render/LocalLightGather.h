#pragma once

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/Frustum3f.h"

#include <cstdint>
#include <d3d12.h>

namespace Dark
{

    static constexpr uint32_t kMaxLocalLights     = 256;
    static constexpr uint32_t kWaterLocalLightMax = 8;

    struct GpuLocalLight
    {
        float pos[3];
        float range;
        float color[3]; // linear rgb * candela
        float invRange2;
        float dir[3]; // world axis, unit (spot); (0,0,0) for point
        float type;   // 0 point, 1 spot
        float innerCos;
        float outerCos;
        float sourceRadius;
        float pad;
    };
    static_assert(sizeof(GpuLocalLight) == 64, "GpuLocalLight stride");

    struct LocalLightCullInput
    {
        const Frustum3f*      frustum    = nullptr; // required
        Math::Vector3f        cameraPos{ 0.0f, 0.0f, 0.0f };
        Math::Vector3f        cameraLook{ 0.0f, 0.0f, 1.0f }; // unit, for near-plane test
        float                 nearZ      = 0.18f;
        uint32_t              maxOut     = kMaxLocalLights;
        float                 maxRange   = 80.0f;
        uint32_t              viewportW  = 2560;
        uint32_t              viewportH  = 1600;
        const Math::Matrix4f* viewProj   = nullptr; // for scissor; required for inside path
    };

    struct LocalLightDrawLists
    {
        GpuLocalLight  lights[kMaxLocalLights];
        Math::Matrix4f volumeWorld[kMaxLocalLights];   // row-vector S * R * T
        D3D12_RECT     insideScissor[kMaxLocalLights]; // densely packed 0..insideCount-1
        uint32_t       count         = 0;              // GPU buffer length: [pointOut | spotOut | inside]
        uint32_t       pointOutCount = 0;              // lights[0 .. pointOutCount)
        uint32_t       spotOutCount  = 0;              // lights[pointOutCount .. pointOutCount+spotOutCount)
        uint32_t       insideCount   = 0;              // lights[pointOut+spotOut .. count); insideScissor[i] ↔ lights[pointOut+spotOut+i]
        uint32_t       waterIndex[kWaterLocalLightMax];
        uint32_t       waterCount    = 0;
    };

    // Fills out. Never throws. Returns false only if frustum/viewProj missing.
    bool gatherLocalLights(World& world, const LocalLightCullInput& in, LocalLightDrawLists& out);

} // namespace Dark
