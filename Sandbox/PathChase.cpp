#include "PathChase.h"

#include "AI/AiComponents.h"
#include "AI/Sight.h"
#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
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
#include "Render/GpuUpload.h"
#include "Render/MeshGen.h"
#include "Render/Renderer.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Water/Water.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
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

    const Vector3f kTreeSeeds[] = {
        { 12.0f, 0, 8.0f },  { -10.0f, 0, 10.0f }, { 14.0f, 0, -6.0f }, { -8.0f, 0, -12.0f }, { 6.0f, 0, 16.0f },
        { -16.0f, 0, 4.0f }, { 18.0f, 0, 2.0f },   { 4.0f, 0, -18.0f }, { -14.0f, 0, -8.0f }, { 10.0f, 0, -14.0f },
    };

    AssetRef<Model> makeTreeModel(AssetManager& assets)
    {
        AssetRef<Material> trunkMat  = internSolidMaterial(assets, 118, 78, 38, 255, "runtime:/pathchase/trunk-mat");
        AssetRef<Material> canopyMat = internSolidMaterial(assets, 46, 140, 62, 255, "runtime:/pathchase/canopy-mat");
        if (!trunkMat || !canopyMat)
            return {};

        MeshData trunkData;
        MeshData canopyData;
        if (!CreateCylinder(trunkData, 1.0f, 1.0f, 1.0f, 16, true, true))
            return {};
        if (!CreateCone(canopyData, 1.0f, 1.0f, 16, true))
            return {};

        Model::Part trunk;
        trunk.mesh        = std::move(trunkData);
        trunk.material    = std::move(trunkMat);
        trunk.localToRoot = Matrix4f::ScaleMatrixXYZ(kTrunkR, kTrunkH, kTrunkR) * Matrix4f::TranslationMatrix(0.0f, kTrunkH * 0.5f, 0.0f);
        trunk.name        = "Trunk";

        Model::Part canopy;
        canopy.mesh        = std::move(canopyData);
        canopy.material    = std::move(canopyMat);
        canopy.localToRoot = Matrix4f::ScaleMatrixXYZ(kCanopyR, kCanopyH, kCanopyR) * Matrix4f::TranslationMatrix(0.0f, kTrunkH + kCanopyH * 0.5f, 0.0f);
        canopy.name        = "Canopy";

        auto model = std::make_shared<Model>();
        if (!model->createFromParts({ std::move(trunk), std::move(canopy) }))
            return {};
        return model;
    }

    void copyMatrix(float dst[16], const Matrix4f& m)
    {
        std::memcpy(dst, &m, sizeof(float) * 16);
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

bool PathChase::init(Renderer& renderer, Terrain::TerrainWorld& terrain, WaterWorld& water, World& world, AssetPinTable& pins, AssetManager& assets)
{
    HitReactionSettings hunterHit{};
    hunterHit.stunSeconds       = 0.45f;
    hunterHit.knockbackDistance = 2.2f;
    hunterHit.knockbackSeconds  = 0.18f;
    hunterHit.horizontalOnly    = true;
    m_ai.setHunterHitReactionSettings(hunterHit);
    m_world = &world;
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

    m_cubes.clear();
    const Vector3f origin = { 0.0f, terrain.heightAtWorld(0.0f, 0.0f) + 0.5f, 0.0f };
    m_cubes.push_back(AABox3f::FromCenterExtents(origin, Vector3f{ 0.5f, 0.5f, 0.5f }));

    AssetRef<Model> treeModel = makeTreeModel(assets);
    if (!registerAndUploadModel(renderer, assets, treeModel, "runtime:/pathchase/tree"))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: tree model failed");
        return false;
    }
    if (!spawnTrees(world, pins, assets, terrain, treeModel))
        return false;
    if (!bake(terrain, water))
        return false;

    MeshData walkerMesh;
    if (!CreateCube(walkerMesh, 1.0f))
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: walker mesh failed");
        return false;
    }
    AssetRef<Material> walkerMat = internSolidMaterial(assets, 220, 90, 40, 255, "runtime:/pathchase/walker-mat");
    AssetRef<Model> walkerModel  = internAndUploadProceduralModel(renderer, assets, std::move(walkerMesh), walkerMat, "runtime:/pathchase/walker");
    if (!walkerModel)
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: walker model failed");
        return false;
    }
    if (!spawnWalker(world, pins, assets, terrain, walkerModel))
        return false;
    if (!spawnAgents(world, pins, assets, terrain))
        return false;
    DE_LOG_INFO(LogCategory::AI, "PathChase: ready, {} trees, 3 agents", std::size(kTreeSeeds));
    return true;
}

bool PathChase::spawnTrees(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainWorld& terrain, const AssetRef<Model>& treeModel)
{
    if (!treeModel || treeModel->id == NULL_ASSET)
    {
        DE_LOG_ERROR(LogCategory::AI, "PathChase: tree model is not registered");
        return false;
    }

    const Vector3f trunkHalf{ kTrunkR, kTrunkH * 0.5f, kTrunkR };
    for (const Vector3f& seed : kTreeSeeds)
    {
        Vector3f p = seed;
        p.y        = terrain.heightAtWorld(p.x, p.z);
        Entity e   = world.createEntity();
        world.emplace<TagComponent>(e, "Tree");
        world.emplace<TransformComponent>(e, p, Quaternion::IDENTITY, Vector3f{ 1, 1, 1 });
        ModelComponent mc{};
        mc.modelAssetID = treeModel->id;
        mc.castShadow   = treeModel->hasOpaque();
        setModelComponent(world, pins, assets, e, mc);
        m_cubes.push_back(AABox3f::FromCenterExtents(Vector3f{ p.x, p.y + kTrunkH * 0.5f, p.z }, trunkHalf));
    }
    return true;
}

bool PathChase::spawnWalker(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainWorld& terrain, const AssetRef<Model>& walkerModel)
{
    Vector3f pos{ -6.0f, 0.0f, 0.0f };
    pos.y    = terrain.heightAtWorld(pos.x, pos.z) + 0.5f;
    m_walker = world.createEntity();
    world.emplace<TagComponent>(m_walker, "ChasePawn");
    world.emplace<TransformComponent>(m_walker, pos, Quaternion::IDENTITY, Vector3f{ 2, 2, 2 });
    if (walkerModel && walkerModel->id != NULL_ASSET)
    {
        ModelComponent mc{};
        mc.modelAssetID = walkerModel->id;
        mc.castShadow   = walkerModel->hasOpaque();
        setModelComponent(world, pins, assets, m_walker, mc);
    }
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
            Entity e    = m_ai.spawnHunter(world, pins, assets, xf);
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
    if (!hostPawn.valid() && m_walker.valid())
    {
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
        }
    }

    const Entity player = hostPawn.valid() ? hostPawn : m_walker;
    m_ai.tickHunters(world, terrain, playerInWater, dt, player);
}


void PathChase::expandBounds(AABox3f& bounds) const
{
    if (!m_world || !m_walker.valid())
        return;
    if (const TransformComponent* xf = m_world->get<TransformComponent>(m_walker))
        bounds.ExpandToInclude(AABox3f::FromCenterExtents(xf->position, Vector3f{ 1.0f, 1.0f, 1.0f }));
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
