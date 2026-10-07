#pragma once

#include "Assets/MeshData.h"
#include "Math/AABox3f.h"
#include "Math/Vector2f.h"
#include "Render/Mesh.h"
#include "Water/WaterWaves.h"

#include <string>
#include <vector>

namespace Dark
{

    class Renderer;
    class WaterPipeline;
    class Camera3D;
    class Frustum3f;
    class ShadowSystem;
    struct SsrSettings;
    struct DebugRenderState;

    namespace Sky
    {
    class Environment;
    }

    namespace Terrain
    {
    class HeightMap;
    }

    // Visual only. Walkability::cellWet and editor-play waterLevel stay on the still lake.
    // Swimming a creek is out of scope.
    struct StreamDesc
    {
        float                       width        = 3.5f;
        float                       flowSpeed    = 1.6f;
        float                       bedClearance = 0.45f;
        std::vector<Math::Vector2f> pointsXZ;
    };

    struct StreamComponent
    {
        float                       width      = 3.5f;
        float                       flowSpeed  = 1.6f;
        std::vector<Math::Vector2f> points;
        bool                        snapToPoints = true;
        std::string                 stopReason;
    };

    // w == 0 returns lakeDir. A vanished lerp returns the unit tangent (or lakeDir if the tangent is degenerate).
    Math::Vector2f blendFlowDirection(Math::Vector2f lakeDir, Math::Vector2f tangentXZ, float w);

    // Walks downhill in 2 m steps. errorOut is a status string (reached lake / flat / length / point cap).
    // True when the polyline has at least two points. Does not log per step.
    bool followDownhill(
        const Terrain::HeightMap& height,
        Math::Vector2f source,
        float waterLevel,
        std::vector<Math::Vector2f>& outPoints,
        std::string* errorOut);

    // Ribbon mesh. Width is clamped to [1.5, 12]. Over-cap input returns false and logs.
    bool buildStreamRibbon(
        const Terrain::HeightMap& height,
        const StreamDesc& desc,
        float lakeLevel,
        MeshData& out,
        Math::AABox3f* outBounds,
        std::string* errorOut);

    bool drawStreamRibbon(
        ID3D12GraphicsCommandList* cmd,
        WaterPipeline& pipeline,
        const Mesh& mesh,
        const Math::AABox3f& bounds,
        const WaterParams& params,
        const Camera3D& camera,
        const Frustum3f* frustum,
        const DebugRenderState* debug,
        float time,
        uint32_t frameIndex,
        uint32_t drawIndex,
        ID3D12DescriptorHeap* heightHeap,
        D3D12_GPU_DESCRIPTOR_HANDLE heightGpu,
        const ShadowSystem* shadows,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneColorCpu,
        D3D12_CPU_DESCRIPTOR_HANDLE depthCpu,
        const SsrSettings* ssrSettings,
        const Terrain::HeightMap* heightMap,
        const Sky::Environment* env);

} // namespace Dark
