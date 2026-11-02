#pragma once

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/Frustum3f.h"

#include <cstddef>
#include <cstdint>
#include <d3d12.h>

namespace Dark
{

    class LocalShadowSystem;

    static constexpr uint32_t kMaxLocalLights     = 256;
    static constexpr uint32_t kWaterLocalLightMax = 8;
    // Nearest walls a light can actually see. A farther wall can still leak.
    static constexpr int   kMaxFogOccluders = 8;
    static constexpr float kFogOccluderSkin = 0.02f;

    struct GpuFogOccluder
    {
        float bmin[4];
        float bmax[4];
    };

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
        float pad; // shadow record, or -1
        float occluderCount;
        float occluderPad[3];
        GpuFogOccluder occluders[kMaxFogOccluders];
    };
    static_assert(sizeof(GpuLocalLight) == 336, "GpuLocalLight stride");
    static_assert(offsetof(GpuLocalLight, occluderCount) == 64, "fog occluders follow the 64-byte light");

    // True when `box` sits between `from` and `to`. A hit within kFogOccluderSkin of either end stays clear,
    // so the face the light is touching and the face being shaded do not black out.
    bool fogBoxBlocks(const Math::AABox3f& box, const Math::Vector3f& from, const Math::Vector3f& to);

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
        const LocalShadowSystem* localShadows = nullptr; // null → every pad is -1
        const Math::AABox3f*  fogBoxes    = nullptr; // walls and shut doors; each light keeps the nearest few in its beam
        int                   fogBoxCount = 0;
    };

    struct LocalLightDrawLists
    {
        GpuLocalLight  lights[kMaxLocalLights];
        Math::Matrix4f volumeWorld[kMaxLocalLights];   // row-vector S * R * T
        D3D12_RECT     insideScissor[kMaxLocalLights]; // densely packed 0..insideCount-1
        uint32_t       count         = 0;              // GPU buffer: [pointOut | spotOut | inside | failed-scissor]
        uint32_t       pointOutCount = 0;              // unused (points always fullscreen; kept for layout)
        uint32_t       spotOutCount  = 0;              // unused (spots always fullscreen; kept for layout)
        uint32_t       insideCount   = 0;              // lights[pointOut+spotOut ..) fullscreen+scissor
        uint32_t       waterIndex[kWaterLocalLightMax];
        uint32_t       waterCount    = 0;
    };

    // Fills out. Never throws. Returns false only if frustum/viewProj missing.
    bool gatherLocalLights(World& world, const LocalLightCullInput& in, LocalLightDrawLists& out);

} // namespace Dark
