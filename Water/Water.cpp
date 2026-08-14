#include "Water/Water.h"
#include "Render/WaterPipeline.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Render/Renderer.h"
#include "Sky/Environment.h"
#include "Terrain/HeightMap.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Vector2f.h"

#include <cstring>

namespace Dark
{
namespace Water
{

using namespace Math;
using namespace Terrain;

namespace
{

void CopyMatrix(float dst[16], const Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

} // namespace

bool WaterWorld::create(const HeightMap& heightMap, WaterDesc desc)
{
    m_chunks.clear();
    m_lods.clear();
    m_chunksX    = 0;
    m_chunksZ    = 0;
    m_heightMap  = nullptr;
    m_time       = 0.0f;

    if (!heightMap.valid())
    {
        DE_LOG_ERROR("WaterWorld: invalid height map");
        return false;
    }
    if (!isPowerOfTwo(desc.chunkCells))
    {
        DE_LOG_ERROR("WaterWorld: chunkCells must be a power of two");
        return false;
    }

    const int cellsX = static_cast<int>(heightMap.width()) - 1;
    const int cellsZ = static_cast<int>(heightMap.height()) - 1;
    if (cellsX < desc.chunkCells || cellsZ < desc.chunkCells)
    {
        DE_LOG_ERROR("WaterWorld: height map smaller than one chunk");
        return false;
    }

    m_heightMap        = &heightMap;
    m_chunkCells       = desc.chunkCells;
    m_maxLod           = maxLodForChunkCells(m_chunkCells);
    m_lodDistanceCount = desc.lodDistanceCount;
    if (m_lodDistanceCount < 1)
        m_lodDistanceCount = 1;
    if (m_lodDistanceCount > kMaxLodLevels)
        m_lodDistanceCount = kMaxLodLevels;
    std::memcpy(m_lodDistances, desc.lodDistances, sizeof(float) * kMaxLodLevels);

    m_params = desc.params;
    m_params.waterLevel = desc.waterLevel;

    m_chunksX = cellsX / m_chunkCells;
    m_chunksZ = cellsZ / m_chunkCells;
    m_chunks.resize(static_cast<size_t>(m_chunksX) * m_chunksZ);
    m_lods.assign(m_chunks.size(), 0);

    const float amp = maxWaveAmplitude(m_params);
    int wet = 0;
    for (int z = 0; z < m_chunksZ; ++z)
    {
        for (int x = 0; x < m_chunksX; ++x)
        {
            WaterChunk& c = m_chunks[static_cast<size_t>(z * m_chunksX + x)];
            c.ix        = x;
            c.iz        = z;
            c.wet       = chunkIsWet(heightMap, x, z);
            c.lod       = 0;
            c.edges     = {};
            c.builtLod  = -1;
            c.builtMask = 0xFF;

            const int ox = x * m_chunkCells;
            const int oz = z * m_chunkCells;
            const float x0 = heightMap.worldX(ox);
            const float z0 = heightMap.worldZ(oz);
            const float x1 = heightMap.worldX(ox + m_chunkCells);
            const float z1 = heightMap.worldZ(oz + m_chunkCells);
            const float y0 = m_params.waterLevel - amp;
            const float y1 = m_params.waterLevel + amp;
            c.bounds = Aabb3f(Vector3f(x0, y0, z0), Vector3f(x1, y1, z1));
            if (c.wet)
                ++wet;
        }
    }

    DE_LOG_INFO(
        "WaterWorld: {}x{} chunks of {}, {} wet, waterLevel {:.2f}",
        m_chunksX,
        m_chunksZ,
        m_chunkCells,
        wet,
        m_params.waterLevel);
    return true;
}

bool WaterWorld::chunkIsWet(const HeightMap& hm, int ix, int iz) const
{
    const int x0 = ix * m_chunkCells;
    const int z0 = iz * m_chunkCells;
    const int x1 = x0 + m_chunkCells;
    const int z1 = z0 + m_chunkCells;
    const float margin = maxWaveAmplitude(m_params);
    const float limit  = m_params.waterLevel + margin;

    // Step by 2 samples — enough to catch valleys without scanning every post.
    for (int z = z0; z <= z1; z += 2)
    {
        for (int x = x0; x <= x1; x += 2)
        {
            if (hm.heightAtWorld(hm.worldX(x), hm.worldZ(z)) < limit)
                return true;
        }
    }
    // Always test the far corner (loop may skip it when chunkCells is odd-stepped).
    return hm.heightAtWorld(hm.worldX(x1), hm.worldZ(z1)) < limit;
}

const WaterChunk* WaterWorld::chunk(int ix, int iz) const
{
    if (ix < 0 || iz < 0 || ix >= m_chunksX || iz >= m_chunksZ)
        return nullptr;
    return &m_chunks[static_cast<size_t>(iz * m_chunksX + ix)];
}

int WaterWorld::wetChunkCount() const
{
    int n = 0;
    for (const WaterChunk& c : m_chunks)
    {
        if (c.wet)
            ++n;
    }
    return n;
}

void WaterWorld::tick(float dt)
{
    m_time += dt;
}

void WaterWorld::updateLod(const Vector3f& cameraPos)
{
    if (m_chunks.empty())
        return;

    for (WaterChunk& c : m_chunks)
    {
        if (!c.wet)
        {
            c.lod = m_maxLod;
            m_lods[static_cast<size_t>(c.iz * m_chunksX + c.ix)] = c.lod;
            continue;
        }
        const Vector3f center = c.bounds.Center();
        const float dx = center.x - cameraPos.x;
        const float dy = center.y - cameraPos.y;
        const float dz = center.z - cameraPos.z;
        const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        c.lod = lodFromDistance(dist, m_lodDistances, m_lodDistanceCount, m_maxLod);
        m_lods[static_cast<size_t>(c.iz * m_chunksX + c.ix)] = c.lod;
    }

    restrictNeighborLods(m_lods.data(), m_chunksX, m_chunksZ);

    for (WaterChunk& c : m_chunks)
    {
        c.lod   = m_lods[static_cast<size_t>(c.iz * m_chunksX + c.ix)];
        c.edges = neighborCoarserMask(m_lods.data(), m_chunksX, m_chunksZ, c.ix, c.iz);
        // A dry neighbor is not a mesh, so don't stitch to it.
        auto clearIfDry = [&](int nx, int nz, uint8_t bit)
        {
            if (nx < 0 || nz < 0 || nx >= m_chunksX || nz >= m_chunksZ)
                return;
            if (!m_chunks[static_cast<size_t>(nz * m_chunksX + nx)].wet)
                c.edges.bits = static_cast<uint8_t>(c.edges.bits & ~bit);
        };
        clearIfDry(c.ix, c.iz + 1, 1u);
        clearIfDry(c.ix + 1, c.iz, 2u);
        clearIfDry(c.ix, c.iz - 1, 4u);
        clearIfDry(c.ix - 1, c.iz, 8u);
    }
}

bool WaterWorld::needsRebuild() const
{
    for (const WaterChunk& c : m_chunks)
    {
        if (!c.wet)
            continue;
        if (c.builtLod != c.lod || c.builtMask != c.edges.bits || c.cpu.indices.empty())
            return true;
    }
    return false;
}

bool WaterWorld::buildChunkMesh(WaterChunk& c)
{
    if (!m_heightMap)
        return false;

    const int maxLod = m_maxLod;
    const int lod    = Clamp(c.lod, 0, maxLod);
    const int step   = lodStep(lod);
    const int cells  = m_chunkCells / step;
    const int verts  = cells + 1;
    const HeightMap& hm = *m_heightMap;

    Geometry::MeshData mesh;
    mesh.positions.reserve(static_cast<size_t>(verts) * verts);
    mesh.normals.reserve(static_cast<size_t>(verts) * verts);
    mesh.uvs.reserve(static_cast<size_t>(verts) * verts);

    const int ox = c.ix * m_chunkCells;
    const int oz = c.iz * m_chunkCells;
    const float waterY = m_params.waterLevel;

    for (int z = 0; z < verts; ++z)
    {
        for (int x = 0; x < verts; ++x)
        {
            const int sx = ox + x * step;
            const int sz = oz + z * step;
            const float wx = hm.worldX(sx);
            const float wz = hm.worldZ(sz);
            const float terrainY = hm.heightAtWorld(wx, wz);

            mesh.positions.push_back(Vector3f(wx, waterY, wz));
            mesh.normals.push_back(Vector3f(0.0f, 1.0f, 0.0f));
            // uv.x unused by lighting; uv.y carries terrain height for shore fade.
            mesh.uvs.push_back(Vector2f(0.0f, terrainY));
        }
    }

    if (!buildGridIndices(cells, c.edges, mesh))
        return false;

    c.cpu       = std::move(mesh);
    c.builtLod  = c.lod;
    c.builtMask = c.edges.bits;
    c.gpu       = Geometry::Mesh{};
    return true;
}

void WaterWorld::rebuildDirtyCpuMeshes()
{
    for (WaterChunk& c : m_chunks)
    {
        if (!c.wet)
            continue;
        if (c.builtLod == c.lod && c.builtMask == c.edges.bits && !c.cpu.indices.empty())
            continue;
        if (!buildChunkMesh(c))
            DE_LOG_ERROR("WaterWorld: failed to build chunk ({},{})", c.ix, c.iz);
    }
}

bool WaterWorld::createGpu(Renderer& renderer)
{
    rebuildDirtyCpuMeshes();
    return uploadDirty(renderer);
}

bool WaterWorld::uploadDirty(Renderer& renderer)
{
    bool ok = true;
    for (WaterChunk& c : m_chunks)
    {
        if (!c.wet || c.gpu.valid() || c.cpu.indices.empty())
            continue;
        if (!Geometry::Mesh::tryCreate(renderer, c.cpu, c.gpu))
        {
            DE_LOG_ERROR("WaterWorld: GPU upload failed for chunk ({},{})", c.ix, c.iz);
            ok = false;
        }
    }
    return ok;
}

void WaterWorld::draw(
    ID3D12GraphicsCommandList* cmd,
    const WaterPipeline& pipeline,
    const Camera3D& camera,
    const Frustum3f* frustum,
    const Sky::Environment* env) const
{
    m_lastDrawCalls = 0;
    m_lastTriangles = 0;
    if (!cmd || !pipeline.isValid())
        return;

    pipeline.bind(cmd);

    const Matrix4f viewProj = camera.GetViewProj();
    WaterFrameConstants cb{};
    CopyMatrix(cb.worldViewProj, viewProj);

    const Vector3f cam = camera.GetPosition();
    const float    camPos[3] = { cam.x, cam.y, cam.z };
    const float    light[3]  = { 0.35f, 0.85f, -0.35f };
    WaterPipeline::fillConstants(cb, cb.worldViewProj, camPos, m_time, light, m_params, env);
    pipeline.setConstants(cmd, cb);

    for (const WaterChunk& c : m_chunks)
    {
        if (!c.wet || !c.gpu.valid())
            continue;
        if (frustum && !frustum->Intersects(c.bounds))
            continue;
        c.gpu.draw(cmd);
        ++m_lastDrawCalls;
        m_lastTriangles += c.gpu.indexCount() / 3u;
    }
}

float WaterWorld::heightAtWorld(float x, float z) const
{
    return waveHeight(m_params, x, z, m_time);
}

bool WaterWorld::tryHeightAtWorld(float x, float z, float& outY) const
{
    if (!m_heightMap || !m_heightMap->containsXZ(x, z))
        return false;
    const float land = m_heightMap->heightAtWorld(x, z);
    const float y    = waveHeight(m_params, x, z, m_time);
    if (land >= y)
        return false;
    outY = y;
    return true;
}

} // namespace Water
} // namespace Dark
