#include "Water/WaterBody.h"

#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/Matrix4f.h"
#include "Render/Camera3D.h"
#include "Render/DebugRenderState.h"
#include "Render/Fog.h"
#include "Render/Frustum3f.h"
#include "Render/ShadowSystem.h"
#include "Render/SsrSettings.h"
#include "Render/WaterPipeline.h"
#include "Terrain/HeightMap.h"

#include <cmath>
#include <cstring>

namespace Dark
{
    using namespace Math;

    namespace
    {

    constexpr float kWaterEdgeFadeMeters = 4.0f;
    constexpr float kWaterCellMeters     = 2.0f;
    constexpr int   kWaterMaxCells       = 64;

    void copyMatrix(float dst[16], const Matrix4f& m)
    {
        std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
    }

    int cellsForExtent(float extent)
    {
        int cells = static_cast<int>(extent / kWaterCellMeters);
        if (cells < 2)
            cells = 2;
        if (cells > kWaterMaxCells)
            cells = kWaterMaxCells;
        return cells;
    }

    float edgeFade(float localX, float localZ, float halfX, float halfZ)
    {
        const float dx = halfX - fabsf(localX);
        const float dz = halfZ - fabsf(localZ);
        const float nearest = dx < dz ? dx : dz;
        if (kWaterEdgeFadeMeters <= 1.0e-4f)
            return 1.0f;
        return Clamp(nearest / kWaterEdgeFadeMeters, 0.0f, 1.0f);
    }

    } // namespace

    bool WaterBody::build(const Terrain::HeightMap* heightMap, const WaterBodyDesc& desc, const WaterParams& params)
    {
        float extentX = desc.extentX;
        float extentZ = desc.extentZ;
        if (extentX < 4.0f)
            extentX = 4.0f;
        if (extentZ < 4.0f)
            extentZ = 4.0f;

        const int cellsX = cellsForExtent(extentX);
        const int cellsZ = cellsForExtent(extentZ);
        const int vertsX = cellsX + 1;
        const int vertsZ = cellsZ + 1;
        const float halfX = extentX * 0.5f;
        const float halfZ = extentZ * 0.5f;
        const float waterY = desc.center.y;

        MeshData mesh;
        mesh.positions.reserve(static_cast<size_t>(vertsX) * vertsZ);
        mesh.normals.reserve(static_cast<size_t>(vertsX) * vertsZ);
        mesh.uvs.reserve(static_cast<size_t>(vertsX) * vertsZ);
        mesh.indices.reserve(static_cast<size_t>(cellsX) * cellsZ * 6u);

        const bool haveTerrain = heightMap && heightMap->valid();
        for (int z = 0; z < vertsZ; ++z)
        {
            for (int x = 0; x < vertsX; ++x)
            {
                const float tx = static_cast<float>(x) / static_cast<float>(cellsX);
                const float tz = static_cast<float>(z) / static_cast<float>(cellsZ);
                const float wx = desc.center.x - halfX + tx * extentX;
                const float wz = desc.center.z - halfZ + tz * extentZ;
                float terrainY = waterY - 10.0f;
                if (haveTerrain && heightMap->containsXZ(wx, wz))
                    terrainY = heightMap->heightAtWorld(wx, wz);
                const float fade = edgeFade(wx - desc.center.x, wz - desc.center.z, halfX, halfZ);
                mesh.positions.push_back(Vector3f(wx, waterY, wz));
                mesh.normals.push_back(Vector3f(0.0f, 1.0f, 0.0f));
                // uv.x is the rectangle fade. uv.y is terrain height for the shore fade.
                mesh.uvs.push_back(Vector2f(fade, terrainY));
            }
        }

        auto indexOf = [vertsX](int x, int z) -> uint32_t
        {
            return static_cast<uint32_t>(z * vertsX + x);
        };
        for (int z = 0; z < cellsZ; ++z)
        {
            for (int x = 0; x < cellsX; ++x)
            {
                const uint32_t bl = indexOf(x, z);
                const uint32_t br = indexOf(x + 1, z);
                const uint32_t tr = indexOf(x + 1, z + 1);
                const uint32_t tl = indexOf(x, z + 1);
                mesh.indices.push_back(bl);
                mesh.indices.push_back(br);
                mesh.indices.push_back(tr);
                mesh.indices.push_back(bl);
                mesh.indices.push_back(tr);
                mesh.indices.push_back(tl);
            }
        }

        m_desc            = desc;
        m_desc.extentX    = extentX;
        m_desc.extentZ    = extentZ;
        m_params          = params;
        m_params.waterLevel = waterY;
        m_cpu             = std::move(mesh);
        m_bakedTerrain    = haveTerrain;
        refreshBounds();
        return !m_cpu.indices.empty();
    }

    bool WaterBody::matches(const WaterBodyDesc& desc) const
    {
        const float extentX = desc.extentX < 4.0f ? 4.0f : desc.extentX;
        const float extentZ = desc.extentZ < 4.0f ? 4.0f : desc.extentZ;
        return fabsf(m_desc.center.x - desc.center.x) < 1.0e-3f
            && fabsf(m_desc.center.y - desc.center.y) < 1.0e-3f
            && fabsf(m_desc.center.z - desc.center.z) < 1.0e-3f
            && fabsf(m_desc.extentX - extentX) < 1.0e-3f
            && fabsf(m_desc.extentZ - extentZ) < 1.0e-3f;
    }

    void WaterBody::refreshBounds()
    {
        const float halfX  = m_desc.extentX * 0.5f;
        const float halfZ  = m_desc.extentZ * 0.5f;
        const float waterY = m_desc.center.y;
        const float amp    = maxWaveAmplitude(m_params);
        const float pad    = amp * Clamp(m_params.steepness, 0.0f, 1.0f);
        m_bounds = AABox3f(
            Vector3f(m_desc.center.x - halfX - pad, waterY - amp, m_desc.center.z - halfZ - pad),
            Vector3f(m_desc.center.x + halfX + pad, waterY + amp, m_desc.center.z + halfZ + pad));
    }

    void WaterBody::setWaveScales(float amplitudeScale, float speedScale)
    {
        m_params.amplitudeScale = amplitudeScale > 0.0f ? amplitudeScale : 0.0f;
        m_params.speedScale     = speedScale > 0.0f ? speedScale : 0.0f;
        refreshBounds();
    }

    bool WaterBody::upload(Renderer& renderer)
    {
        if (m_cpu.indices.empty())
            return false;
        if (!Mesh::tryCreate(renderer, m_cpu, m_gpu))
        {
            DE_LOG_ERROR(LogCategory::Render, "WaterBody: GPU upload failed");
            return false;
        }
        return true;
    }

    Mesh WaterBody::takeGpu()
    {
        return std::move(m_gpu);
    }

    bool WaterBody::vertex(int index, Vector3f& outPos, Vector2f& outUv) const
    {
        if (index < 0 || index >= static_cast<int>(m_cpu.positions.size()) || index >= static_cast<int>(m_cpu.uvs.size()))
            return false;
        outPos = m_cpu.positions[static_cast<size_t>(index)];
        outUv  = m_cpu.uvs[static_cast<size_t>(index)];
        return true;
    }

    bool WaterBody::containsXZ(float x, float z) const
    {
        const float halfX = m_desc.extentX * 0.5f;
        const float halfZ = m_desc.extentZ * 0.5f;
        return x >= m_desc.center.x - halfX && x <= m_desc.center.x + halfX
            && z >= m_desc.center.z - halfZ && z <= m_desc.center.z + halfZ;
    }

    bool WaterBody::draw(
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
        const Terrain::HeightMap* heightMap) const
    {
        if (!cmd || !pipeline.isValid() || !m_gpu.valid())
            return false;
        if (drawIndex >= WaterPipeline::kMaxWaterDrawsPerFrame)
            return false;
        if (frustum && !frustum->Intersects(m_bounds))
            return false;

        const DebugFill fill = debug ? debug->fill : DebugFill::Solid;
        const bool lighting  = !debug || debug->lightingActive();
        pipeline.bind(cmd, fill);

        WaterFrameConstants cb{};
        const Matrix4f viewProj = camera.GetViewProj();
        copyMatrix(cb.worldViewProj, viewProj);
        const Vector3f cam = camera.GetPosition();
        const float camPos[3] = { cam.x, cam.y, cam.z };
        const float light[3]  = { 0.35f, 0.85f, -0.35f };
        WaterPipeline::fillConstants(cb, cb.worldViewProj, camPos, time, light, m_params, nullptr, lighting);
        FogGpu fogMap{};
        fillFogHeightMap(fogMap, heightMap);
        cb.heightOriginX    = fogMap.heightOriginX;
        cb.heightOriginZ    = fogMap.heightOriginZ;
        cb.heightCellSize   = fogMap.heightCellSize;
        cb.heightWorldSizeX = fogMap.heightWorldSizeX;
        cb.heightWorldSizeZ = fogMap.heightWorldSizeZ;
        const bool hasSsr = sceneColorCpu.ptr != 0 && depthCpu.ptr != 0;
        WaterPipeline::fillSsr(cb, camera, ssrSettings, !debug || debug->ssrEnabled, hasSsr, nullptr);
        pipeline.setConstants(cmd, cb, frameIndex, drawIndex);
        pipeline.setLights(cmd, 0);
        pipeline.setSsrSrvs(sceneColorCpu, depthCpu);
        if (pipeline.hasReceiverSrvs())
            pipeline.bindReceiverSrvs(cmd);
        else
            pipeline.setHeightMap(cmd, heightHeap, heightGpu);
        if (shadows && shadows->isValid())
            shadows->bindReceiverCbv(cmd, WaterPipeline::kRootShadowCbv);
        m_gpu.draw(cmd, fill == DebugFill::Points);
        return true;
    }

} // namespace Dark
