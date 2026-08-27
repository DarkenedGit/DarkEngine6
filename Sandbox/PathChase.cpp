#include "PathChase.h"

#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Input/Input.h"
#include "Math/Matrix4f.h"
#include "Network/NetTypes.h"
#include "Render/Camera3D.h"
#include "Render/Renderer.h"
#include "Render/ShadowSystem.h"
#include "Terrain/Terrain.h"
#include "Water/Water.h"

#include <cmath>
#include <cstring>
#include <random>

using namespace Dark::Math;
using namespace Dark::Geometry;

namespace Dark
{
namespace
{
    void copyMatrix(float dst[16], const Matrix4f& m)
    {
        std::memcpy(dst, &m, sizeof(float) * 16);
    }

    Matrix4f makeWorld(const Vector3f& p, const Vector3f& scale)
    {
        return Matrix4f::ScaleMatrixXYZ(scale.x, scale.y, scale.z) * Matrix4f::TranslationMatrix(p.x, p.y, p.z);
    }

    ComPtr<ID3D12Resource> createUpload(ID3D12Device* device, uint64_t bytes)
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width            = bytes;
        desc.Height           = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels        = 1;
        desc.SampleDesc       = { 1, 0 };
        desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> res;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&res))))
            return {};
        return res;
    }
} // namespace

bool PathChase::createLineBuffers(Renderer& renderer)
{
    ID3D12Device* device = renderer.device();
    const uint64_t vbBytes = sizeof(Vector3f) * kMaxLineVerts;
    const uint64_t ibBytes = sizeof(uint32_t) * kMaxLineVerts * 2;
    for (int i = 0; i < 2; ++i)
    {
        m_lineVb[i] = createUpload(device, vbBytes);
        m_lineIb[i] = createUpload(device, ibBytes);
        if (!m_lineVb[i] || !m_lineIb[i])
            return false;
        m_lineVbv[i].BufferLocation = m_lineVb[i]->GetGPUVirtualAddress();
        m_lineVbv[i].StrideInBytes  = sizeof(Vector3f);
        m_lineVbv[i].SizeInBytes    = static_cast<UINT>(vbBytes);
        m_lineIbv[i].BufferLocation = m_lineIb[i]->GetGPUVirtualAddress();
        m_lineIbv[i].Format         = DXGI_FORMAT_R32_UINT;
        m_lineIbv[i].SizeInBytes    = static_cast<UINT>(ibBytes);
    }
    return true;
}

bool PathChase::bake(Terrain::TerrainWorld& terrain, Water::WaterWorld& water)
{
    AI::WalkabilityDesc d;
    d.heightMap   = &terrain.heightMap();
    d.waterLevel  = water.params().waterLevel;
    d.agentRadius = m_agentR;
    d.cubes       = m_cubes.empty() ? nullptr : m_cubes.data();
    d.cubeCount   = static_cast<int>(m_cubes.size());
    if (!m_walk.bake(d))
        return false;
    return m_finder.bind(&m_walk);
}

bool PathChase::init(Renderer& renderer, Terrain::TerrainWorld& terrain, Water::WaterWorld& water, World& world, Geometry::Mesh&, AssetRef<Material> treeMat, AssetRef<Material> aiMat)
{
    m_treeMat = treeMat;
    m_aiMat   = aiMat;
    if (!m_lines.create(renderer.device()))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: LinePipeline create failed");
        return false;
    }
    if (!createLineBuffers(renderer))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: line buffers failed");
        return false;
    }

    const Vector3f trees[] = {
        { 18.0f, 0, 12.0f },  { -22.0f, 0, 8.0f }, { 30.0f, 0, -16.0f }, { -14.0f, 0, -28.0f }, { 8.0f, 0, 36.0f },
        { -36.0f, 0, 20.0f }, { 42.0f, 0, 6.0f },  { -8.0f, 0, 48.0f },  { 24.0f, 0, -40.0f }, { -28.0f, 0, -12.0f },
    };
    m_treePos.clear();
    m_cubes.clear();
    const Vector3f origin = { 0.0f, terrain.heightAtWorld(0.0f, 0.0f) + 0.5f, 0.0f };
    m_cubes.push_back(Aabb3f::FromCenterExtents(origin, Vector3f{ 0.5f, 0.5f, 0.5f }));
    for (const Vector3f& t : trees)
    {
        Vector3f p = t;
        p.y        = terrain.heightAtWorld(p.x, p.z) + 0.5f;
        m_treePos.push_back(p);
        m_cubes.push_back(Aabb3f::FromCenterExtents(p, Vector3f{ 0.5f, 0.5f, 0.5f }));
    }

    if (!bake(terrain, water))
        return false;
    if (!spawnWalker(world, terrain))
        return false;
    if (!spawnAgents(terrain))
        return false;
    DE_LOG_INFO(LogCategory::AI, "PathChase: ready, {} trees, 3 agents", m_treePos.size());
    return true;
}

bool PathChase::spawnWalker(World& world, Terrain::TerrainWorld& terrain)
{
    Vector3f pos{ -6.0f, 0.0f, 0.0f };
    pos.y     = terrain.heightAtWorld(pos.x, pos.z) + 0.5f;
    m_walker  = world.createEntity();
    world.emplace<TagComponent>(m_walker, "ChasePawn");
    world.emplace<TransformComponent>(m_walker, pos, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
    m_walkerPos = pos;
    return true;
}

bool PathChase::spawnAgents(Terrain::TerrainWorld& terrain)
{
    std::mt19937 rng{ 20260826u };
    const auto&  hm = terrain.heightMap();
    std::uniform_real_distribution<float> ux(hm.origin().x + 4.0f, hm.origin().x + hm.cellSize() * static_cast<float>(hm.width() - 2) - 4.0f);
    std::uniform_real_distribution<float> uz(hm.origin().z + 4.0f, hm.origin().z + hm.cellSize() * static_cast<float>(hm.height() - 2) - 4.0f);

    for (int i = 0; i < 3; ++i)
    {
        bool ok = false;
        for (int tries = 0; tries < 256; ++tries)
        {
            const float x = ux(rng);
            const float z = uz(rng);
            if (!m_walk.walkableWorld(x, z))
                continue;
            bool hit = false;
            for (int j = 0; j < i; ++j)
            {
                const float dx = x - m_agents[static_cast<size_t>(j)].pos.x;
                const float dz = z - m_agents[static_cast<size_t>(j)].pos.z;
                if (dx * dx + dz * dz < 4.0f)
                    hit = true;
            }
            if (hit)
                continue;
            m_agents[static_cast<size_t>(i)].pos      = Vector3f{ x, terrain.heightAtWorld(x, z) + 0.5f, z };
            m_agents[static_cast<size_t>(i)].repathAt = static_cast<float>(i) * (0.5f / 3.0f);
            m_agents[static_cast<size_t>(i)].givenUp  = false;
            ok = true;
            break;
        }
        if (!ok)
        {
            DE_LOG_ERROR(LogCategory::AI, "PathChase: failed to spawn agent {}", i);
            return false;
        }
    }
    return true;
}

void PathChase::repath(Agent& a, int self, float now)
{
    AI::AgentStamp stamps[3];
    int        n = 0;
    for (int i = 0; i < 3; ++i)
    {
        if (i == self)
            continue;
        stamps[n].x      = m_agents[static_cast<size_t>(i)].pos.x;
        stamps[n].z      = m_agents[static_cast<size_t>(i)].pos.z;
        stamps[n].radius = m_agentR;
        ++n;
    }
    AI::PathRequest req;
    req.startX     = a.pos.x;
    req.startZ     = a.pos.z;
    req.destX      = m_walkerPos.x;
    req.destZ      = m_walkerPos.z;
    req.others     = stamps;
    req.otherCount = n;
    AI::PathResult next;
    if (!m_finder.find(req, next) || next.points.empty())
    {
        a.givenUp = true;
        a.path.points.clear();
        a.waypoint = 0;
        return;
    }
    a.givenUp  = false;
    a.path     = std::move(next);
    a.waypoint = 0;
    a.repathAt = now + 0.5f;
}

void PathChase::follow(Agent& a, float dt, Terrain::TerrainWorld& terrain)
{
    if (a.givenUp || a.path.points.empty())
        return;
    const float speed = 10.0f;
    float       remain = speed * dt;
    while (remain > 0.0f && a.waypoint < static_cast<int>(a.path.points.size()))
    {
        const Vector3f& wp = a.path.points[static_cast<size_t>(a.waypoint)];
        Vector3f        d{ wp.x - a.pos.x, 0.0f, wp.z - a.pos.z };
        const float     dist = d.Magnitude();
        if (dist < 1.0f)
        {
            ++a.waypoint;
            continue;
        }
        const float step = remain < dist ? remain : dist;
        d *= (1.0f / dist);
        const Vector3f next{ a.pos.x + d.x * step, 0.0f, a.pos.z + d.z * step };
        if (!m_walk.walkableWorld(next.x, next.z))
            break;
        a.pos.x = next.x;
        a.pos.z = next.z;
        remain -= step;
    }
    a.pos.y = terrain.heightAtWorld(a.pos.x, a.pos.z) + 0.5f;
}

void PathChase::tick(float dt, World& world, Input& input, Terrain::TerrainWorld& terrain, Entity hostPawn)
{
    m_time += dt;
    if (hostPawn.valid())
    {
        m_drawWalker = false;
        if (const TransformComponent* xf = world.get<TransformComponent>(hostPawn))
            m_walkerPos = xf->position;
    }
    else if (m_walker.valid())
    {
        m_drawWalker = true;
        if (TransformComponent* xf = world.get<TransformComponent>(m_walker))
        {
            const float ax = input.actionAxis("pawn_x");
            const float az = input.actionAxis("pawn_z");
            if (ax != 0.0f || az != 0.0f)
            {
                Vector3f delta{ ax, 0.0f, az };
                const float mag = delta.Magnitude();
                if (mag > 1.0f)
                    delta *= (1.0f / mag);
                xf->position += delta * (kNetPawnMaxSpeed * dt);
            }
            xf->position.y = terrain.heightAtWorld(xf->position.x, xf->position.z) + 0.5f;
            m_walkerPos    = xf->position;
        }
    }

    const float touch = 1.0f;
    for (int i = 0; i < 3; ++i)
    {
        Agent& a = m_agents[static_cast<size_t>(i)];
        const float dx = a.pos.x - m_walkerPos.x;
        const float dz = a.pos.z - m_walkerPos.z;
        const bool  arrived = (dx * dx + dz * dz) <= touch * touch;
        const bool  destWalk = m_walk.walkableWorld(m_walkerPos.x, m_walkerPos.z);
        if (arrived)
        {
            a.path.points.clear();
            continue;
        }
        if (a.givenUp)
        {
            if (destWalk)
                repath(a, i, m_time);
            continue;
        }
        bool need = m_time >= a.repathAt;
        if (a.path.points.empty())
            need = true;
        if (need)
            repath(a, i, m_time);
        follow(a, dt, terrain);
    }
}

void PathChase::drawMeshes(ID3D12GraphicsCommandList* cmd, MeshPipeline& meshPipe, ShadowSystem& shadows, const Camera3D& camera, const MeshFrameConstants& baseCb, Geometry::Mesh& cubeMesh, DebugFill fill)
{
    if (!cmd)
        return;
    meshPipe.bind(cmd, fill);
    shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);

    MeshFrameConstants cb = baseCb;
    const Matrix4f viewProj = camera.GetViewProj();
    auto drawAt = [&](const Vector3f& p, Material* mat) {
        if (mat && mat->isValid())
            mat->bind(cmd, MeshPipeline::kRootAlbedoSrv);
        const Matrix4f world = makeWorld(p, Vector3f{ 1, 1, 1 });
        copyMatrix(cb.worldViewProj, world * viewProj);
        copyMatrix(cb.world, world);
        meshPipe.setConstants(cmd, cb);
        cubeMesh.draw(cmd, fill == DebugFill::Points);
    };

    if (m_drawWalker)
        drawAt(m_walkerPos, m_aiMat.get());
    for (const Vector3f& t : m_treePos)
        drawAt(t, m_treeMat.get());
    for (const Agent& a : m_agents)
        drawAt(a.pos, m_aiMat.get());
}

void PathChase::drawPaths(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Matrix4f& viewProj)
{
    if (!m_lines.isValid() || !cmd)
        return;
    const uint32_t fi = renderer.frameIndex() % 2;
    std::vector<Vector3f> verts;
    std::vector<uint32_t> idx;
    verts.reserve(256);
    idx.reserve(256);
    for (const Agent& a : m_agents)
    {
        if (a.givenUp || a.path.points.size() < 2)
            continue;
        const uint32_t base = static_cast<uint32_t>(verts.size());
        for (const Vector3f& p : a.path.points)
        {
            Vector3f q = p;
            q.y += 0.4f;
            verts.push_back(q);
        }
        for (uint32_t i = 0; i + 1 < static_cast<uint32_t>(a.path.points.size()); ++i)
        {
            idx.push_back(base + i);
            idx.push_back(base + i + 1);
        }
    }
    if (verts.empty() || idx.empty() || verts.size() > kMaxLineVerts)
        return;

    void* vp = nullptr;
    void* ip = nullptr;
    if (FAILED(m_lineVb[fi]->Map(0, nullptr, &vp)) || FAILED(m_lineIb[fi]->Map(0, nullptr, &ip)))
        return;
    std::memcpy(vp, verts.data(), verts.size() * sizeof(Vector3f));
    std::memcpy(ip, idx.data(), idx.size() * sizeof(uint32_t));
    m_lineVb[fi]->Unmap(0, nullptr);
    m_lineIb[fi]->Unmap(0, nullptr);

    m_lineVbv[fi].SizeInBytes = static_cast<UINT>(verts.size() * sizeof(Vector3f));
    m_lineIbv[fi].SizeInBytes = static_cast<UINT>(idx.size() * sizeof(uint32_t));

    m_lines.bind(cmd);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    LineFrameConstants lc{};
    copyMatrix(lc.worldViewProj, viewProj);
    lc.color[0] = 1.0f;
    lc.color[1] = 0.85f;
    lc.color[2] = 0.15f;
    lc.color[3] = 1.0f;
    m_lines.setConstants(cmd, lc);
    cmd->IASetVertexBuffers(0, 1, &m_lineVbv[fi]);
    cmd->IASetIndexBuffer(&m_lineIbv[fi]);
    cmd->DrawIndexedInstanced(static_cast<UINT>(idx.size()), 1, 0, 0, 0);
}

} // namespace Dark
