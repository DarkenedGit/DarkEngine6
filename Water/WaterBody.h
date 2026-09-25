#pragma once

#include "Assets/MeshData.h"
#include "Math/AABox3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Render/Mesh.h"
#include "Water/WaterWaves.h"

#include <cstdint>

namespace Dark
{

    class Renderer;
    class WaterPipeline;
    class Camera3D;
    class Frustum3f;
    class ShadowSystem;
    struct SsrSettings;
    struct DebugRenderState;

    namespace Terrain
    {
    class HeightMap;
    }

    // One finite water surface. Position Y is the rest height. extentX/extentZ are the full footprint in meters.
    struct WaterBodyDesc
    {
        Math::Vector3f center{ 0.0f, 0.0f, 0.0f };
        float          extentX = 48.0f;
        float          extentZ = 48.0f;
    };

    class WaterBody
    {
    public:
        bool build(const Terrain::HeightMap* heightMap, const WaterBodyDesc& desc, const WaterParams& params);
        bool matches(const WaterBodyDesc& desc) const;
        bool bakedTerrain() const { return m_bakedTerrain; }

        void setWaveScales(float amplitudeScale, float speedScale);
        WaterParams&       params() { return m_params; }
        const WaterParams& params() const { return m_params; }

        bool upload(Renderer& renderer);
        Mesh takeGpu();
        bool gpuValid() const { return m_gpu.valid(); }

        bool draw(
            ID3D12GraphicsCommandList* cmd,
            WaterPipeline& pipeline,
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
            const Terrain::HeightMap* heightMap) const;

        int vertexCount() const { return static_cast<int>(m_cpu.positions.size()); }
        bool vertex(int index, Math::Vector3f& outPos, Math::Vector2f& outUv) const;
        bool containsXZ(float x, float z) const;
        const Math::AABox3f& bounds() const { return m_bounds; }

    private:
        WaterBodyDesc   m_desc{};
        WaterParams     m_params{};
        MeshData        m_cpu{};
        Mesh            m_gpu{};
        Math::AABox3f   m_bounds{};
        bool            m_bakedTerrain = false;

        void refreshBounds();
    };

} // namespace Dark
