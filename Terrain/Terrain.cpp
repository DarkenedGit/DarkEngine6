#include "Terrain/Terrain.h"
#include "Terrain/TerrainMaterial.h"
#include "Render/TerrainPipeline.h"
#include "Render/ShadowSystem.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Render/Renderer.h"
#include "Sky/Environment.h"
#include "Core/Log.h"
#include "Math/Matrix4f.h"

#include <cstring>

namespace Dark
{
namespace Terrain
{

using namespace Math;

namespace
{

void CopyMatrix(float dst[16], const Matrix4f& m)
{
    std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
}

} // namespace

bool TerrainWorld::create(TerrainDesc desc)
{
    m_chunks.clear();
    m_lods.clear();
    m_chunksX = 0;
    m_chunksZ = 0;

    if (!desc.heightMap.valid())
    {
        DE_LOG_ERROR("TerrainWorld: invalid height map");
        return false;
    }
    if (!isPowerOfTwo(desc.chunkCells))
    {
        DE_LOG_ERROR("TerrainWorld: chunkCells must be a power of two");
        return false;
    }

    const int samplesX = static_cast<int>(desc.heightMap.width());
    const int samplesZ = static_cast<int>(desc.heightMap.height());
    const int cellsX   = samplesX - 1;
    const int cellsZ   = samplesZ - 1;
    if (cellsX < desc.chunkCells || cellsZ < desc.chunkCells)
    {
        DE_LOG_ERROR("TerrainWorld: height map smaller than one chunk");
        return false;
    }

    m_heightMap        = std::move(desc.heightMap);
    m_chunkCells       = desc.chunkCells;
    m_maxLod           = maxLodForChunkCells(m_chunkCells);
    m_lodDistanceCount = desc.lodDistanceCount;
    if (m_lodDistanceCount < 1)
        m_lodDistanceCount = 1;
    if (m_lodDistanceCount > kMaxLodLevels)
        m_lodDistanceCount = kMaxLodLevels;
    std::memcpy(m_lodDistances, desc.lodDistances, sizeof(float) * kMaxLodLevels);

    m_chunksX = cellsX / m_chunkCells;
    m_chunksZ = cellsZ / m_chunkCells;
    m_chunks.resize(static_cast<size_t>(m_chunksX) * m_chunksZ);
    m_lods.assign(m_chunks.size(), 0);

    for (int z = 0; z < m_chunksZ; ++z)
    {
        for (int x = 0; x < m_chunksX; ++x)
        {
            TerrainChunk& c = m_chunks[static_cast<size_t>(z * m_chunksX + x)];
            c.ix        = x;
            c.iz        = z;
            c.lod       = 0;
            c.edges     = {};
            c.builtLod  = -1;
            c.builtMask = 0xFF;

            const int ox = x * m_chunkCells;
            const int oz = z * m_chunkCells;
            Aabb3f box = Aabb3f::Empty();
            // Corners + a mid sample so the AABB covers the chunk even before meshing.
            box.ExpandToInclude(m_heightMap.positionAtSample(ox, oz));
            box.ExpandToInclude(m_heightMap.positionAtSample(ox + m_chunkCells, oz));
            box.ExpandToInclude(m_heightMap.positionAtSample(ox, oz + m_chunkCells));
            box.ExpandToInclude(m_heightMap.positionAtSample(ox + m_chunkCells, oz + m_chunkCells));
            box.ExpandToInclude(m_heightMap.positionAtSample(ox + m_chunkCells / 2, oz + m_chunkCells / 2));
            c.bounds = box;
        }
    }

    m_bounds = m_heightMap.bounds();
    DE_LOG_INFO(
        "TerrainWorld: {}x{} samples, {}x{} chunks of {}, maxLod {}",
        m_heightMap.width(),
        m_heightMap.height(),
        m_chunksX,
        m_chunksZ,
        m_chunkCells,
        m_maxLod);
    return true;
}

TerrainChunk* TerrainWorld::chunkAt(int ix, int iz)
{
    if (ix < 0 || iz < 0 || ix >= m_chunksX || iz >= m_chunksZ)
        return nullptr;
    return &m_chunks[static_cast<size_t>(iz * m_chunksX + ix)];
}

const TerrainChunk* TerrainWorld::chunk(int ix, int iz) const
{
    if (ix < 0 || iz < 0 || ix >= m_chunksX || iz >= m_chunksZ)
        return nullptr;
    return &m_chunks[static_cast<size_t>(iz * m_chunksX + ix)];
}

void TerrainWorld::updateLod(const Vector3f& cameraPos)
{
    if (m_chunks.empty())
        return;

    for (TerrainChunk& c : m_chunks)
    {
        const Vector3f center = c.bounds.Center();
        const float dx = center.x - cameraPos.x;
        const float dy = center.y - cameraPos.y;
        const float dz = center.z - cameraPos.z;
        const float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        c.lod = lodFromDistance(dist, m_lodDistances, m_lodDistanceCount, m_maxLod);
        m_lods[static_cast<size_t>(c.iz * m_chunksX + c.ix)] = c.lod;
    }

    restrictNeighborLods(m_lods.data(), m_chunksX, m_chunksZ);

    for (TerrainChunk& c : m_chunks)
    {
        c.lod   = m_lods[static_cast<size_t>(c.iz * m_chunksX + c.ix)];
        c.edges = neighborCoarserMask(m_lods.data(), m_chunksX, m_chunksZ, c.ix, c.iz);
    }
}

bool TerrainWorld::needsRebuild() const
{
    for (const TerrainChunk& c : m_chunks)
    {
        if (c.builtLod != c.lod || c.builtMask != c.edges.bits || c.cpu.indices.empty())
            return true;
    }
    return false;
}

void TerrainWorld::rebuildDirtyCpuMeshes()
{
    for (TerrainChunk& c : m_chunks)
    {
        if (c.builtLod == c.lod && c.builtMask == c.edges.bits && !c.cpu.indices.empty())
            continue;

        PatchBuildDesc desc;
        desc.heightMap     = &m_heightMap;
        desc.originSampleX = c.ix * m_chunkCells;
        desc.originSampleZ = c.iz * m_chunkCells;
        desc.chunkCells    = m_chunkCells;
        desc.lod           = c.lod;
        desc.edges         = c.edges;

        if (!buildPatchMesh(desc, c.cpu))
        {
            DE_LOG_ERROR("TerrainWorld: failed to build chunk ({},{})", c.ix, c.iz);
            continue;
        }

        if (!c.cpu.positions.empty())
            c.bounds = Aabb3f::FromPoints(c.cpu.positions.data(), static_cast<int>(c.cpu.positions.size()));

        c.builtLod  = c.lod;
        c.builtMask = c.edges.bits;
        c.gpu       = Geometry::Mesh{}; // force re-upload
    }
}

bool TerrainWorld::createGpu(Renderer& renderer)
{
    rebuildDirtyCpuMeshes();
    return uploadDirty(renderer);
}

bool TerrainWorld::uploadDirty(Renderer& renderer)
{
    bool ok = true;
    for (TerrainChunk& c : m_chunks)
    {
        if (c.gpu.valid())
            continue;
        if (c.cpu.indices.empty())
            continue;
        if (!Geometry::Mesh::tryCreate(renderer, c.cpu, c.gpu))
        {
            DE_LOG_ERROR("TerrainWorld: GPU upload failed for chunk ({},{})", c.ix, c.iz);
            ok = false;
        }
    }
    return ok;
}

void TerrainWorld::draw(
    ID3D12GraphicsCommandList* cmd,
    const TerrainPipeline& pipeline,
    const TerrainMaterial& material,
    const Camera3D& camera,
    const Frustum3f* frustum,
    const Sky::Environment* env,
    const ShadowSystem* shadows) const
{
    m_lastDrawCalls = 0;
    m_lastTriangles = 0;
    if (!cmd || !pipeline.isValid() || !material.isValid())
        return;

    pipeline.bind(cmd);
    material.bind(cmd, TerrainPipeline::kRootSrvTable);
    if (shadows)
        shadows->bindReceiverCbv(cmd, TerrainPipeline::kRootShadowCbv);

    const Matrix4f world    = Matrix4f::IDENTITY;
    const Matrix4f viewProj = camera.GetViewProj();
    const Matrix4f wvp      = world * viewProj;

    TerrainFrameConstants cb{};
    CopyMatrix(cb.worldViewProj, wvp);
    CopyMatrix(cb.world, world);
    material.applySurface(cb);
    const Vector3f cam = camera.GetPosition();
    cb.cameraPosX = cam.x;
    cb.cameraPosY = cam.y;
    cb.cameraPosZ = cam.z;
    if (env)
    {
        cb.lightDirWS[0]    = env->lightDir().x;
        cb.lightDirWS[1]    = env->lightDir().y;
        cb.lightDirWS[2]    = env->lightDir().z;
        cb.lightColor[0]    = env->lightColor().x;
        cb.lightColor[1]    = env->lightColor().y;
        cb.lightColor[2]    = env->lightColor().z;
        cb.ambientColor[0]  = env->ambientColor().x;
        cb.ambientColor[1]  = env->ambientColor().y;
        cb.ambientColor[2]  = env->ambientColor().z;
        cb.fogColor[0]      = env->fogColor().x;
        cb.fogColor[1]      = env->fogColor().y;
        cb.fogColor[2]      = env->fogColor().z;
        cb.fogDensity       = env->fogDensity();
    }
    else
    {
        cb.lightDirWS[0]   = 0.35f;
        cb.lightDirWS[1]   = 0.85f;
        cb.lightDirWS[2]   = -0.35f;
        cb.lightColor[0]   = 1.0f;
        cb.lightColor[1]   = 0.96f;
        cb.lightColor[2]   = 0.88f;
        cb.ambientColor[0] = 0.18f;
        cb.ambientColor[1] = 0.20f;
        cb.ambientColor[2] = 0.22f;
        cb.fogColor[0]     = 0.55f;
        cb.fogColor[1]     = 0.62f;
        cb.fogColor[2]     = 0.72f;
        cb.fogDensity      = 0.0f;
    }
    pipeline.setConstants(cmd, cb);

    for (const TerrainChunk& c : m_chunks)
    {
        if (!c.gpu.valid())
            continue;
        if (frustum && !frustum->Intersects(c.bounds))
            continue;
        c.gpu.draw(cmd);
        ++m_lastDrawCalls;
        m_lastTriangles += c.gpu.indexCount() / 3u;
    }
}

void TerrainWorld::drawDepth(ID3D12GraphicsCommandList* cmd) const
{
    if (!cmd)
        return;
    for (const TerrainChunk& c : m_chunks)
    {
        if (!c.gpu.valid())
            continue;
        c.gpu.draw(cmd);
    }
}

float TerrainWorld::heightAtWorld(float x, float z) const
{
    return m_heightMap.heightAtWorld(x, z);
}

bool TerrainWorld::tryHeightAtWorld(float x, float z, float& outY) const
{
    return m_heightMap.tryHeightAtWorld(x, z, outY);
}

bool TerrainWorld::containsXZ(float x, float z) const
{
    return m_heightMap.containsXZ(x, z);
}

Collision::RayHit3D TerrainWorld::raycast(const Ray3f& ray, float maxDistance) const
{
    return m_heightMap.raycast(ray, maxDistance);
}

Vector3f TerrainWorld::normalAtWorld(float x, float z) const
{
    return m_heightMap.normalAtWorld(x, z);
}

} // namespace Terrain
} // namespace Dark
