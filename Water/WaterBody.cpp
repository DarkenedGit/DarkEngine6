#include "Water/WaterBody.h"

#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
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
    constexpr float kWaterChunkMeters    = 64.0f;
    constexpr int   kWaterChunkCells     = 32;
    constexpr int   kWaterMaxExtent      = 1024;
    constexpr int   kWaterMaxWetChunks   = 256;
    constexpr int   kWaterBodyMaxLod     = 3;

    void copyMatrix(float dst[16], const Matrix4f& m)
    {
        std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
    }

    float snapDown2(float v)
    {
        return floorf(v / kWaterCellMeters) * kWaterCellMeters;
    }

    float rectFadeAt(float wx, float wz, float centerX, float centerZ, float halfX, float halfZ, float& outNearest)
    {
        const float dx = halfX - fabsf(wx - centerX);
        const float dz = halfZ - fabsf(wz - centerZ);
        outNearest = dx < dz ? dx : dz;
        if (outNearest < -1.0e-3f)
            return 0.0f;
        if (kWaterEdgeFadeMeters <= 1.0e-4f)
            return 1.0f;
        return Clamp(outNearest / kWaterEdgeFadeMeters, 0.0f, 1.0f);
    }

    } // namespace

    bool WaterBody::buildOneChunk(int ix, int iz)
    {
        WaterBodyChunk c;
        c.ix        = ix;
        c.iz        = iz;
        c.lod       = 0;
        c.builtLod  = -1;
        c.builtMask = 0xFF;
        c.edges     = {};

        const float chunkSpan = static_cast<float>(m_cells) * m_cellMeters;
        const float x0 = m_originX + static_cast<float>(ix) * chunkSpan;
        const float z0 = m_originZ + static_cast<float>(iz) * chunkSpan;
        const float amp = maxWaveAmplitude(m_params);
        const float pad = amp * Clamp(m_params.steepness, 0.0f, 1.0f);
        const float y0 = m_params.waterLevel - amp;
        const float y1 = m_params.waterLevel + amp;
        c.bounds = AABox3f(Vector3f(x0 - pad, y0, z0 - pad), Vector3f(x0 + chunkSpan + pad, y1, z0 + chunkSpan + pad));

        const int slot = static_cast<int>(m_chunks.size());
        m_chunks.push_back(std::move(c));
        m_slot[static_cast<size_t>(iz * m_chunksX + ix)] = slot;
        return true;
    }

    bool WaterBody::rebuildOne(WaterBodyChunk& c)
    {
        int step = Terrain::lodStep(c.lod);
        if (step < 1)
            step = 1;
        if (m_cells % step != 0)
            step = 1;
        const int cells = m_cells / step;
        const int verts = cells + 1;
        const float span = static_cast<float>(m_cells) * m_cellMeters;
        const float x0 = m_originX + static_cast<float>(c.ix) * span;
        const float z0 = m_originZ + static_cast<float>(c.iz) * span;
        const float waterY = m_params.waterLevel;
        const float halfX = m_desc.extentX * 0.5f;
        const float halfZ = m_desc.extentZ * 0.5f;
        const float dispAmp = displacementAmplitude(m_params);
        const float stride = m_cellMeters * static_cast<float>(step);

        MeshData mesh;
        mesh.positions.reserve(static_cast<size_t>(verts) * static_cast<size_t>(verts));
        mesh.normals.reserve(mesh.positions.capacity());
        mesh.uvs.reserve(mesh.positions.capacity());
        mesh.tangents.reserve(mesh.positions.capacity());

        for (int z = 0; z < verts; ++z)
        {
            for (int x = 0; x < verts; ++x)
            {
                const float wx = x0 + static_cast<float>(x) * stride;
                const float wz = z0 + static_cast<float>(z) * stride;
                float terrainY = waterY - 10.0f;
                float shoreW = 0.0f;
                if (m_height && m_height->valid() && m_height->containsXZ(wx, wz))
                {
                    const BedSlope bed = sampleBedSlope(*m_height, wx, wz);
                    terrainY = m_height->heightAtWorld(wx, wz);
                    if (bed.ok)
                    {
                        const ShoreSample shore = evaluateShore(waterY, terrainY, bed.slope, m_params.foamWidthScale, dispAmp);
                        shoreW = shore.weight;
                    }
                }
                float nearest = 0.0f;
                const float fade = rectFadeAt(wx, wz, m_desc.center.x, m_desc.center.z, halfX, halfZ, nearest);
                const float uvx = nearest < -1.0e-3f ? 0.0f : (shoreW > 0.0f ? 1.0f : fade);
                mesh.positions.push_back(Vector3f(wx, waterY, wz));
                mesh.normals.push_back(Vector3f(0.0f, 1.0f, 0.0f));
                mesh.uvs.push_back(Vector2f(uvx, terrainY));
                mesh.tangents.push_back(Vector4f(0.0f, 0.0f, 0.0f, 0.0f));
            }
        }

        if (!Terrain::buildGridIndices(cells, c.edges, mesh))
            return false;

        c.cpu       = std::move(mesh);
        c.builtLod  = c.lod;
        c.builtMask = c.edges.bits;
        m_retire.push(std::move(c.gpu));
        return true;
    }

    bool WaterBody::build(const Terrain::HeightMap* heightMap, const WaterBodyDesc& desc, const WaterParams& params)
    {
        m_chunks.clear();
        m_slot.clear();
        m_chunksX = 0;
        m_chunksZ = 0;
        m_retire.clear();

        float extentX = desc.extentX;
        float extentZ = desc.extentZ;
        if (extentX < 4.0f)
            extentX = 4.0f;
        if (extentZ < 4.0f)
            extentZ = 4.0f;
        if (extentX > static_cast<float>(kWaterMaxExtent) || extentZ > static_cast<float>(kWaterMaxExtent))
        {
            DE_LOG_ERROR(LogCategory::Render, "WaterBody: extent {:.1f} x {:.1f} exceeds {} m", extentX, extentZ, kWaterMaxExtent);
            return false;
        }

        m_desc         = desc;
        m_desc.extentX = extentX;
        m_desc.extentZ = extentZ;
        m_params       = params;
        m_params.waterLevel = desc.center.y;
        m_height       = (heightMap && heightMap->valid()) ? heightMap : nullptr;
        m_bakedTerrain = m_height != nullptr;
        m_small        = extentX < kWaterChunkMeters && extentZ < kWaterChunkMeters;

        const float halfX = extentX * 0.5f;
        const float halfZ = extentZ * 0.5f;

        if (m_small)
        {
            const float side = extentX > extentZ ? extentX : extentZ;
            int cells = static_cast<int>(side / kWaterCellMeters);
            if (cells < 2)
                cells = 2;
            m_cells      = cells;
            m_cellMeters = side / static_cast<float>(cells);
            m_originX    = desc.center.x - side * 0.5f;
            m_originZ    = desc.center.z - side * 0.5f;
            m_chunksX    = 1;
            m_chunksZ    = 1;
            m_maxLod     = 0;
        }
        else
        {
            m_cells      = kWaterChunkCells;
            m_cellMeters = kWaterCellMeters;
            m_originX    = snapDown2(desc.center.x - halfX);
            m_originZ    = snapDown2(desc.center.z - halfZ);
            const float maxX = desc.center.x + halfX;
            const float maxZ = desc.center.z + halfZ;
            m_chunksX = static_cast<int>(ceilf((maxX - m_originX) / kWaterChunkMeters));
            m_chunksZ = static_cast<int>(ceilf((maxZ - m_originZ) / kWaterChunkMeters));
            if (m_chunksX < 1)
                m_chunksX = 1;
            if (m_chunksZ < 1)
                m_chunksZ = 1;
            int cap = Terrain::maxLodForChunkCells(kWaterChunkCells);
            if (cap > kWaterBodyMaxLod)
                cap = kWaterBodyMaxLod;
            m_maxLod = cap;
        }

        m_slot.assign(static_cast<size_t>(m_chunksX * m_chunksZ), -1);
        const float limit = m_params.waterLevel + maxWaveAmplitude(m_params);
        const bool haveMap = m_bakedTerrain;

        for (int iz = 0; iz < m_chunksZ; ++iz)
        {
            for (int ix = 0; ix < m_chunksX; ++ix)
            {
                bool wet = !haveMap;
                if (haveMap)
                {
                    const float span = static_cast<float>(m_cells) * m_cellMeters;
                    const float x0 = m_originX + static_cast<float>(ix) * span;
                    const float z0 = m_originZ + static_cast<float>(iz) * span;
                    const float step = m_cellMeters * 2.0f;
                    const int steps = m_cells / 2;
                    for (int z = 0; z <= steps && !wet; ++z)
                    {
                        for (int x = 0; x <= steps; ++x)
                        {
                            const float wx = x0 + static_cast<float>(x) * step;
                            const float wz = z0 + static_cast<float>(z) * step;
                            if (!heightMap->containsXZ(wx, wz))
                                continue;
                            if (heightMap->heightAtWorld(wx, wz) < limit)
                            {
                                wet = true;
                                break;
                            }
                        }
                    }
                    const float x1 = x0 + span;
                    const float z1 = z0 + span;
                    if (!wet && heightMap->containsXZ(x1, z1) && heightMap->heightAtWorld(x1, z1) < limit)
                        wet = true;
                }
                if (!wet)
                    continue;
                if (static_cast<int>(m_chunks.size()) >= kWaterMaxWetChunks)
                {
                    DE_LOG_ERROR(LogCategory::Render, "WaterBody: wet chunk cap {} exceeded", kWaterMaxWetChunks);
                    m_chunks.clear();
                    m_slot.clear();
                    return false;
                }
                if (!buildOneChunk(ix, iz))
                    return false;
            }
        }

        if (m_chunks.empty())
        {
            DE_LOG_ERROR(LogCategory::Render, "WaterBody: no wet chunks");
            return false;
        }

        for (WaterBodyChunk& c : m_chunks)
        {
            c.lod = 0;
            if (!rebuildOne(c))
            {
                DE_LOG_ERROR(LogCategory::Render, "WaterBody: failed to build chunk ({},{})", c.ix, c.iz);
                m_chunks.clear();
                return false;
            }
        }

        refreshBounds();
        return true;
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
        const float y0 = waterY - amp;
        const float y1 = waterY + amp;
        for (WaterBodyChunk& c : m_chunks)
        {
            c.bounds.Min.y = y0;
            c.bounds.Max.y = y1;
        }
    }

    void WaterBody::setWaveScales(float amplitudeScale, float speedScale)
    {
        m_params.amplitudeScale = amplitudeScale > 0.0f ? amplitudeScale : 0.0f;
        m_params.speedScale     = speedScale > 0.0f ? speedScale : 0.0f;
        refreshBounds();
    }

    void WaterBody::applySharedParams(const WaterParams& shared)
    {
        const float level = m_params.waterLevel;
        m_params = shared;
        m_params.waterLevel = level;
        refreshBounds();
    }

    void WaterBody::updateLod(const Vector3f& cameraPos)
    {
        if (m_chunks.empty() || m_maxLod < 0)
            return;
        m_retire.tick();
        refreshBounds();

        std::vector<int> lods(static_cast<size_t>(m_chunksX * m_chunksZ), -1);
        for (WaterBodyChunk& c : m_chunks)
        {
            const Vector3f center = c.bounds.Center();
            const float dx = center.x - cameraPos.x;
            const float dy = center.y - cameraPos.y;
            const float dz = center.z - cameraPos.z;
            const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
            const int current = (c.builtLod < 0) ? -1 : c.lod;
            c.lod = Terrain::lodFromDistance(dist, m_lodDistances, 4, m_maxLod, current);
            lods[static_cast<size_t>(c.iz * m_chunksX + c.ix)] = c.lod;
        }

        Terrain::restrictNeighborLods(lods.data(), m_chunksX, m_chunksZ);
        for (WaterBodyChunk& c : m_chunks)
        {
            c.lod   = lods[static_cast<size_t>(c.iz * m_chunksX + c.ix)];
            c.edges = Terrain::neighborCoarserMask(lods.data(), m_chunksX, m_chunksZ, c.ix, c.iz);
        }
    }

    bool WaterBody::needsRebuild() const
    {
        for (const WaterBodyChunk& c : m_chunks)
        {
            if (c.builtLod != c.lod || c.builtMask != c.edges.bits || c.cpu.indices.empty())
                return true;
        }
        return false;
    }

    void WaterBody::rebuildDirtyCpuMeshes()
    {
        for (WaterBodyChunk& c : m_chunks)
        {
            if (c.builtLod == c.lod && c.builtMask == c.edges.bits && !c.cpu.indices.empty())
                continue;
            if (!rebuildOne(c))
                DE_LOG_ERROR(LogCategory::Render, "WaterBody: failed to build chunk ({},{})", c.ix, c.iz);
        }
    }

    bool WaterBody::upload(Renderer& renderer)
    {
        bool ok = true;
        for (WaterBodyChunk& c : m_chunks)
        {
            if (c.gpu.valid() || c.cpu.indices.empty())
                continue;
            if (!Mesh::tryCreate(renderer, c.cpu, c.gpu))
            {
                DE_LOG_ERROR(LogCategory::Render, "WaterBody: GPU upload failed");
                ok = false;
            }
        }
        return ok && gpuValid();
    }

    void WaterBody::retireGpu(GpuMeshRetire& retire)
    {
        for (WaterBodyChunk& c : m_chunks)
            retire.push(std::move(c.gpu));
        retire.takeFrom(m_retire);
    }

    bool WaterBody::gpuValid() const
    {
        for (const WaterBodyChunk& c : m_chunks)
        {
            if (c.gpu.valid())
                return true;
        }
        return false;
    }

    int WaterBody::vertexCount() const
    {
        if (m_chunks.size() != 1)
            return 0;
        return static_cast<int>(m_chunks[0].cpu.positions.size());
    }

    bool WaterBody::vertex(int index, Vector3f& outPos, Vector2f& outUv) const
    {
        if (m_chunks.size() != 1)
            return false;
        const MeshData& cpu = m_chunks[0].cpu;
        if (index < 0 || index >= static_cast<int>(cpu.positions.size()) || index >= static_cast<int>(cpu.uvs.size()))
            return false;
        outPos = cpu.positions[static_cast<size_t>(index)];
        outUv  = cpu.uvs[static_cast<size_t>(index)];
        return true;
    }

    bool WaterBody::containsXZ(float x, float z) const
    {
        const float halfX = m_desc.extentX * 0.5f;
        const float halfZ = m_desc.extentZ * 0.5f;
        return x >= m_desc.center.x - halfX && x <= m_desc.center.x + halfX
            && z >= m_desc.center.z - halfZ && z <= m_desc.center.z + halfZ;
    }

    const WaterBodyChunk* WaterBody::chunk(int index) const
    {
        if (index < 0 || index >= static_cast<int>(m_chunks.size()))
            return nullptr;
        return &m_chunks[static_cast<size_t>(index)];
    }

    const WaterBodyChunk* WaterBody::chunkAt(int ix, int iz) const
    {
        if (ix < 0 || iz < 0 || ix >= m_chunksX || iz >= m_chunksZ)
            return nullptr;
        const int slot = m_slot[static_cast<size_t>(iz * m_chunksX + ix)];
        if (slot < 0 || slot >= static_cast<int>(m_chunks.size()))
            return nullptr;
        return &m_chunks[static_cast<size_t>(slot)];
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
        const Terrain::HeightMap* heightMap,
        const Sky::Environment* env) const
    {
        if (!cmd || !pipeline.isValid() || !gpuValid())
            return false;
        if (drawIndex >= WaterPipeline::kMaxWaterDrawsPerFrame)
            return false;

        const DebugFill fill = debug ? debug->fill : DebugFill::Solid;
        const bool lighting  = !debug || debug->lightingActive();
        pipeline.bind(cmd, fill, false);

        WaterFrameConstants cb{};
        const Matrix4f viewProj = camera.GetViewProj();
        copyMatrix(cb.worldViewProj, viewProj);
        const Vector3f cam = camera.GetPosition();
        const float camPos[3] = { cam.x, cam.y, cam.z };
        const float light[3]  = { 0.35f, 0.85f, -0.35f };
        WaterPipeline::fillConstants(cb, cb.worldViewProj, camPos, time, light, m_params, env, lighting);
        FogGpu fogMap{};
        fillFogHeightMap(fogMap, heightMap);
        cb.heightOriginX    = fogMap.heightOriginX;
        cb.heightOriginZ    = fogMap.heightOriginZ;
        cb.heightCellSize   = fogMap.heightCellSize;
        cb.heightWorldSizeX = fogMap.heightWorldSizeX;
        cb.heightWorldSizeZ = fogMap.heightWorldSizeZ;
        const bool hasSsr = sceneColorCpu.ptr != 0 && depthCpu.ptr != 0;
        WaterPipeline::fillSsr(cb, camera, ssrSettings, !debug || debug->ssrEnabled, hasSsr, env);
        pipeline.setConstants(cmd, cb, frameIndex, drawIndex);
        pipeline.setLights(cmd, 0);
        pipeline.setSsrSrvs(sceneColorCpu, depthCpu);
        if (pipeline.hasReceiverSrvs())
            pipeline.bindReceiverSrvs(cmd);
        else
            pipeline.setHeightMap(cmd, heightHeap, heightGpu);
        if (shadows && shadows->isValid())
            shadows->bindReceiverCbv(cmd, WaterPipeline::kRootShadowCbv);

        bool drew = false;
        const bool pointList = fill == DebugFill::Points;
        for (const WaterBodyChunk& c : m_chunks)
        {
            if (!c.gpu.valid())
                continue;
            if (frustum && !frustum->Intersects(c.bounds))
                continue;
            c.gpu.draw(cmd, pointList);
            drew = true;
        }
        return drew;
    }

} // namespace Dark
