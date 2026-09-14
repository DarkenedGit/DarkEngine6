#include "PathChase.h"

#include "AI/AiComponents.h"
#include "AI/Sight.h"
#include "Assets/AssetManager.h"
#include "Character/HealthComponent.h"
#include "Core/AssetPinTable.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Input/Input.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Network/NetTypes.h"
#include "Render/MeshGen.h"
#include "Render/Camera3D.h"
#include "Render/Renderer.h"
#include "Render/ShadowSystem.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Water/Water.h"

#include <cmath>
#include <cstring>
#include <random>

using namespace Dark::Math;

namespace Dark
{
namespace
{
    constexpr float kTreeHeight = 6.0f;
    constexpr float kTrunkH     = kTreeHeight * (1.0f / 3.0f);
    constexpr float kCanopyH    = kTreeHeight - kTrunkH;
    constexpr float kTrunkR     = 0.5f;
    constexpr float kCanopyR    = 2.0f;

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

bool PathChase::bake(Terrain::TerrainWorld& terrain, WaterWorld& water)
{
    AI::WalkabilityDesc d;
    d.heightMap   = &terrain.heightMap();
    d.waterLevel  = water.params().waterLevel;
    d.agentRadius = m_agentR;
    d.cubes       = m_cubes.empty() ? nullptr : m_cubes.data();
    d.cubeCount   = static_cast<int>(m_cubes.size());
    return m_ai.bake(d);
}

bool PathChase::init(Renderer& renderer, Terrain::TerrainWorld& terrain, WaterWorld& water, World& world, AssetPinTable& pins, AssetManager& assets, Mesh&,
                     AssetRef<Material> trunkMat, AssetRef<Material> canopyMat, AssetRef<Material> aiMat)
{
    HitReactionSettings hunterHit{};
    hunterHit.stunSeconds       = 0.45f;
    hunterHit.knockbackDistance = 2.2f;
    hunterHit.knockbackSeconds  = 0.18f;
    hunterHit.horizontalOnly    = true;
    m_ai.setHunterHitReactionSettings(hunterHit);
    m_world     = &world;
    m_trunkMat  = trunkMat;
    m_canopyMat = canopyMat;
    m_aiMat     = aiMat;
    if (!m_lines.create(renderer.device(), renderer.sceneColorFormat()))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: LinePipeline create failed");
        return false;
    }
    if (!createLineBuffers(renderer))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: line buffers failed");
        return false;
    }

    MeshData trunkData;
    MeshData canopyData;
    if (!CreateCylinder(trunkData, 1.0f, 1.0f, 1.0f, 16, true, true) || !Mesh::tryCreate(renderer, trunkData, m_trunkMesh))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: trunk mesh failed");
        return false;
    }
    if (!CreateCone(canopyData, 1.0f, 1.0f, 16, true) || !Mesh::tryCreate(renderer, canopyData, m_canopyMesh))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: canopy mesh failed");
        return false;
    }

    const Vector3f trees[] = {
        { 12.0f, 0, 8.0f },  { -10.0f, 0, 10.0f }, { 14.0f, 0, -6.0f }, { -8.0f, 0, -12.0f }, { 6.0f, 0, 16.0f },
        { -16.0f, 0, 4.0f }, { 18.0f, 0, 2.0f },   { 4.0f, 0, -18.0f }, { -14.0f, 0, -8.0f }, { 10.0f, 0, -14.0f },
    };
    m_treePos.clear();
    m_cubes.clear();
    const Vector3f origin = { 0.0f, terrain.heightAtWorld(0.0f, 0.0f) + 0.5f, 0.0f };
    m_cubes.push_back(AABox3f::FromCenterExtents(origin, Vector3f{ 0.5f, 0.5f, 0.5f }));
    const Vector3f trunkHalf{ kTrunkR, kTrunkH * 0.5f, kTrunkR };
    for (const Vector3f& t : trees)
    {
        Vector3f p = t;
        p.y        = terrain.heightAtWorld(p.x, p.z);
        m_treePos.push_back(p);
        Vector3f trunkCenter{ p.x, p.y + kTrunkH * 0.5f, p.z };
        m_cubes.push_back(AABox3f::FromCenterExtents(trunkCenter, trunkHalf));
    }

    if (!bake(terrain, water))
        return false;
    if (!spawnWalker(world, terrain))
        return false;
    if (!spawnAgents(world, pins, assets, terrain))
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

bool PathChase::spawnAgents(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainWorld& terrain)
{
    std::mt19937 rng{ 20260826u };
    std::uniform_real_distribution<float> ux(-22.0f, 22.0f);
    std::uniform_real_distribution<float> uz(-22.0f, 22.0f);
    const Vector3f seeds[3] = { { 16.0f, 0, -10.0f }, { -14.0f, 0, -12.0f }, { 8.0f, 0, 18.0f } };
    m_hunterCount = 0;

    for (int i = 0; i < kHunterCount; ++i)
    {
        bool ok = false;
        for (int tries = 0; tries < 256; ++tries)
        {
            const float x = (tries == 0) ? seeds[i].x : ux(rng);
            const float z = (tries == 0) ? seeds[i].z : uz(rng);
            if (!m_ai.walkability().walkableWorld(x, z))
                continue;
            bool hit = false;
            for (int j = 0; j < m_hunterCount; ++j)
            {
                const TransformComponent* ox = world.get<TransformComponent>(m_hunters[static_cast<size_t>(j)]);
                if (!ox)
                    continue;
                const float dx = x - ox->position.x;
                const float dz = z - ox->position.z;
                if (dx * dx + dz * dz < 4.0f)
                    hit = true;
            }
            if (hit)
                continue;
            TransformComponent xf{};
            xf.position = Vector3f{ x, terrain.heightAtWorld(x, z) + 0.5f, z };
            xf.scale    = Vector3f{ 1.0f, 1.0f, 1.0f };
            Entity e    = m_ai.spawnHunter(world, pins, assets, m_aiMat, xf);
            if (!e.valid())
                return false;
            if (PathAgentComponent* path = world.get<PathAgentComponent>(e))
                path->repathAt = static_cast<float>(i) * (0.5f / 3.0f);
            m_hunters[static_cast<size_t>(m_hunterCount++)] = e;
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

void PathChase::tick(float dt, World& world, Input& input, Terrain::TerrainWorld& terrain, Entity hostPawn, bool playerInWater)
{
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

    const Entity player = hostPawn.valid() ? hostPawn : m_walker;
    m_ai.tickHunters(world, terrain, playerInWater, dt, player);
}


void PathChase::drawMeshes(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, MeshPipeline& meshPipe, ShadowSystem& shadows, const Camera3D& camera, const MeshFrameConstants& baseCb, Mesh& cubeMesh, DebugFill fill)
{
    if (!cmd || !cubeMesh.valid())
        return;
    meshPipe.bind(cmd, fill);
    shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);

    MeshFrameConstants cb = baseCb;
    cb.lighting           = 0.0f;
    const Matrix4f viewProj = camera.GetViewProj();
    auto drawAt = [&](const Vector3f& p, Mesh& mesh, Material* mat, const Vector3f& scale, float r, float g, float b) {
        if (!mesh.valid())
            return;
        if (mat)
            gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
        cb.color[0] = r;
        cb.color[1] = g;
        cb.color[2] = b;
        cb.color[3] = 1.0f;
        const Matrix4f world = makeWorld(p, scale);
        copyMatrix(cb.worldViewProj, world * viewProj);
        copyMatrix(cb.world, world);
        meshPipe.setConstants(cmd, cb);
        mesh.draw(cmd, fill == DebugFill::Points);
    };

    const Vector3f trunkScale{ kTrunkR, kTrunkH, kTrunkR };
    const Vector3f canopyScale{ kCanopyR, kCanopyH, kCanopyR };
    const Vector3f aiScale{ 2.0f, 2.0f, 2.0f };
    if (m_drawWalker)
        drawAt(m_walkerPos, cubeMesh, m_aiMat.get(), aiScale, 1.0f, 0.35f, 0.12f);
    for (const Vector3f& t : m_treePos)
    {
        const Vector3f trunkPos{ t.x, t.y + kTrunkH * 0.5f, t.z };
        const Vector3f canopyPos{ t.x, t.y + kTrunkH + kCanopyH * 0.5f, t.z };
        drawAt(trunkPos, m_trunkMesh, m_trunkMat.get(), trunkScale, 0.45f, 0.28f, 0.12f);
        drawAt(canopyPos, m_canopyMesh, m_canopyMat.get(), canopyScale, 0.15f, 0.75f, 0.18f);
    }
}

void PathChase::drawDepth(ID3D12GraphicsCommandList* cmd, const ShadowSystem& shadows, int cascade, Mesh& cubeMesh) const
{
    if (!cmd || !cubeMesh.valid() || cascade < 0 || cascade >= shadows.cascadeCount())
        return;

    const Matrix4f lightVP = shadows.cascade(cascade).viewProj;
    auto drawAt = [&](const Vector3f& p, const Mesh& mesh, const Vector3f& scale) {
        if (!mesh.valid())
            return;
        const Matrix4f wvp = makeWorld(p, scale) * lightVP;
        shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
        mesh.draw(cmd);
    };

    const Vector3f trunkScale{ kTrunkR, kTrunkH, kTrunkR };
    const Vector3f canopyScale{ kCanopyR, kCanopyH, kCanopyR };
    const Vector3f aiScale{ 2.0f, 2.0f, 2.0f };
    if (m_drawWalker)
        drawAt(m_walkerPos, cubeMesh, aiScale);
    for (const Vector3f& t : m_treePos)
    {
        const Vector3f trunkPos{ t.x, t.y + kTrunkH * 0.5f, t.z };
        const Vector3f canopyPos{ t.x, t.y + kTrunkH + kCanopyH * 0.5f, t.z };
        drawAt(trunkPos, m_trunkMesh, trunkScale);
        drawAt(canopyPos, m_canopyMesh, canopyScale);
    }
}

void PathChase::expandBounds(AABox3f& bounds) const
{
    auto include = [&](const Vector3f& p, const Vector3f& half) { bounds.ExpandToInclude(AABox3f::FromCenterExtents(p, half)); };
    if (m_drawWalker)
        include(m_walkerPos, Vector3f{ 1.0f, 1.0f, 1.0f });
    for (const Vector3f& t : m_treePos)
    {
        include(Vector3f{ t.x, t.y + kTrunkH * 0.5f, t.z }, Vector3f{ kTrunkR, kTrunkH * 0.5f, kTrunkR });
        include(Vector3f{ t.x, t.y + kTrunkH + kCanopyH * 0.5f, t.z }, Vector3f{ kCanopyR, kCanopyH * 0.5f, kCanopyR });
    }
}

void PathChase::drawMeshesGBuffer(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, MeshPipeline& meshPipe, const Camera3D& camera, const Matrix4f& prevViewProj, Mesh& cubeMesh, DebugFill fill)
{
    if (!cmd || !cubeMesh.valid())
        return;
    meshPipe.bind(cmd, fill);

    MeshGBufferConstants cb{};
    const Matrix4f viewProj = camera.GetViewProj();
    auto drawAt = [&](const Vector3f& p, const Vector3f& prevP, Mesh& mesh, Material* mat, const Vector3f& scale, float r, float g, float b) {
        if (!mesh.valid())
            return;
        if (mat)
            gpu.bindMaterial(cmd, *mat, MeshPipeline::kRootAlbedoSrv);
        cb.color[0] = r;
        cb.color[1] = g;
        cb.color[2] = b;
        cb.color[3] = 0.0f;
        const Matrix4f world     = makeWorld(p, scale);
        const Matrix4f prevWorld = makeWorld(prevP, scale);
        copyMatrix(cb.worldViewProj, world * viewProj);
        copyMatrix(cb.world, world);
        copyMatrix(cb.prevWorldViewProj, prevWorld * prevViewProj);
        meshPipe.setGBufferConstants(cmd, cb);
        mesh.draw(cmd, fill == DebugFill::Points);
    };

    const Vector3f trunkScale{ kTrunkR, kTrunkH, kTrunkR };
    const Vector3f canopyScale{ kCanopyR, kCanopyH, kCanopyR };
    const Vector3f aiScale{ 2.0f, 2.0f, 2.0f };
    const Vector3f walkerPrev = m_havePrevXforms ? m_prevWalkerPos : m_walkerPos;
    if (m_drawWalker)
        drawAt(m_walkerPos, walkerPrev, cubeMesh, m_aiMat.get(), aiScale, 1.0f, 0.35f, 0.12f);
    for (const Vector3f& t : m_treePos)
    {
        const Vector3f trunkPos{ t.x, t.y + kTrunkH * 0.5f, t.z };
        const Vector3f canopyPos{ t.x, t.y + kTrunkH + kCanopyH * 0.5f, t.z };
        drawAt(trunkPos, trunkPos, m_trunkMesh, m_trunkMat.get(), trunkScale, 0.45f, 0.28f, 0.12f);
        drawAt(canopyPos, canopyPos, m_canopyMesh, m_canopyMat.get(), canopyScale, 0.15f, 0.75f, 0.18f);
    }
    m_prevWalkerPos  = m_walkerPos;
    m_havePrevXforms = true;
}

void PathChase::drawPaths(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Matrix4f& viewProj)
{
    if (!m_lines.isValid() || !cmd)
        return;
    const uint32_t fi = renderer.frameIndex() % 2;
    std::vector<Vector3f> verts;
    std::vector<uint32_t> idx;
    verts.reserve(512);
    idx.reserve(512);
    auto addSeg = [&](const Vector3f& a, const Vector3f& b) {
        const uint32_t i0 = static_cast<uint32_t>(verts.size());
        verts.push_back(a);
        verts.push_back(b);
        idx.push_back(i0);
        idx.push_back(i0 + 1);
    };
    if (m_world)
    {
        for (int hi = 0; hi < m_hunterCount; ++hi)
        {
            const Entity e = m_hunters[static_cast<size_t>(hi)];
            const HealthComponent* hp = m_world->get<HealthComponent>(e);
            const TransformComponent* xf = m_world->get<TransformComponent>(e);
            const AiAgentComponent* ai = m_world->get<AiAgentComponent>(e);
            const PathAgentComponent* path = m_world->get<PathAgentComponent>(e);
            const BrainComponent* brain = m_world->get<BrainComponent>(e);
            if (!hp || !xf || !ai || !path || !brain || !brain->brain || !hp->health.alive())
                continue;
            if (path->path.points.size() >= 2 && brain->brain->leaf() != AI::Leaf::Wander)
            {
                for (size_t i = 0; i + 1 < path->path.points.size(); ++i)
                {
                    Vector3f p0 = path->path.points[i];
                    Vector3f p1 = path->path.points[i + 1];
                    p0.y += 0.4f;
                    p1.y += 0.4f;
                    addSeg(p0, p1);
                }
            }
            Vector3f eye{ xf->position.x, xf->position.y + 0.5f, xf->position.z };
            Vector3f fwd = ai->forward;
            fwd.y = 0.0f;
            if (fwd.MagnitudeSqrd() < 1.0e-6f)
                fwd = Vector3f{ 0.0f, 0.0f, 1.0f };
            fwd.Normalize();
            Vector3f right{ -fwd.z, 0.0f, fwd.x };
            const float half = 35.0f * 3.14159265f / 180.0f;
            const float range = 12.0f;
            Vector3f leftRay  = fwd * std::cos(half) + right * std::sin(half);
            Vector3f rightRay = fwd * std::cos(half) - right * std::sin(half);
            leftRay.Normalize();
            rightRay.Normalize();
            addSeg(eye, eye + leftRay * range);
            addSeg(eye, eye + rightRay * range);
            addSeg(eye + leftRay * range, eye + rightRay * range);
            if (ai->hasLastSeen && brain->brain->leaf() == AI::Leaf::Memory)
            {
                Vector3f p = ai->lastSeen;
                p.y += 1.2f;
                addSeg(Vector3f{ p.x - 0.6f, p.y, p.z }, Vector3f{ p.x + 0.6f, p.y, p.z });
                addSeg(Vector3f{ p.x, p.y, p.z - 0.6f }, Vector3f{ p.x, p.y, p.z + 0.6f });
            }
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


Entity PathChase::hunterEntity(int i) const
{
    if (i < 0 || i >= m_hunterCount)
        return {};
    return m_hunters[static_cast<size_t>(i)];
}

void PathChase::setHunterHitReaction(const HitReactionSettings& settings)
{
    if (m_world)
        m_ai.setHunterHitReaction(*m_world, settings);
    else
        m_ai.setHunterHitReactionSettings(settings);
}

} // namespace Dark
