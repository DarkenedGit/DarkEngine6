#pragma once

#include "AI/HunterBrain.h"
#include "AI/Pathfinder.h"
#include "AI/Walkability.h"
#include "Character/Health.h"
#include "ECS/Entity.h"
#include "Geometry/Mesh.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"
#include "Render/LinePipeline.h"
#include "Render/Material.h"
#include "Render/MeshPipeline.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <vector>

namespace Dark
{
class Renderer;
class World;
class Input;
class Camera3D;
class MeshPipeline;
class ShadowSystem;

namespace Terrain
{
class TerrainWorld;
}
namespace Water
{
class WaterWorld;
}

class PathChase
{
public:
    bool init(Renderer& renderer, Terrain::TerrainWorld& terrain, Water::WaterWorld& water, World& world, Geometry::Mesh& cubeMesh, AssetRef<Material> trunkMat, AssetRef<Material> canopyMat, AssetRef<Material> aiMat);

    void tick(float dt, World& world, Input& input, Terrain::TerrainWorld& terrain, Entity hostPawn, bool playerInWater);
    void drawMeshes(ID3D12GraphicsCommandList* cmd, MeshPipeline& meshPipe, ShadowSystem& shadows, const Camera3D& camera, const MeshFrameConstants& baseCb, Geometry::Mesh& cubeMesh, DebugFill fill);
    void drawPaths(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Math::Matrix4f& viewProj);

    Entity walker() const { return m_walker; }
    const std::vector<Math::Aabb3f>& cubes() const { return m_cubes; }

    static constexpr int kHunterCount = 3;
    int  hunterCount() const { return kHunterCount; }
    bool hunterAlive(int i) const;
    const Math::Vector3f& hunterPos(int i) const;
    bool applyHunterDamage(int i, float amount);
    void tickHunterHealth(float dt);

private:
    struct Agent
    {
        Math::Vector3f     pos{};
        Math::Vector3f     forward{ 0.0f, 0.0f, 1.0f };
        Math::Vector3f     lastSeen{};
        Math::Vector3f     wanderDest{};
        AI::HunterBrain    brain;
        AI::PathResult     path;
        int                waypoint = 0;
        float              repathAt = 0.0f;
        bool               givenUp  = false;
        bool               hasLastSeen = false;
        Health             health;
        float              deadFor = 0.0f;
    };

    bool bake(Terrain::TerrainWorld& terrain, Water::WaterWorld& water);
    bool spawnWalker(World& world, Terrain::TerrainWorld& terrain);
    bool spawnAgents(Terrain::TerrainWorld& terrain);
    void follow(Agent& a, float dt, Terrain::TerrainWorld& terrain);
    void repath(Agent& a, int self, float now, float destX, float destZ);
    bool pickWanderDest(Agent& a);
    bool createLineBuffers(Renderer& renderer);

    AI::Walkability m_walk;
    AI::Pathfinder  m_finder;
    LinePipeline    m_lines;
    std::vector<Math::Aabb3f> m_cubes;
    std::vector<Math::Vector3f> m_treePos;
    std::array<Agent, kHunterCount> m_agents{};
    Entity          m_walker{};
    Math::Vector3f  m_walkerPos{};
    float           m_time = 0.0f;
    float           m_agentR = 0.8f;
    bool            m_drawWalker = true;

    Geometry::Mesh     m_trunkMesh;
    Geometry::Mesh     m_canopyMesh;
    AssetRef<Material> m_trunkMat;
    AssetRef<Material> m_canopyMat;
    AssetRef<Material> m_aiMat;

    static constexpr uint32_t kMaxLineVerts = 2048;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_lineVb[2];
    Microsoft::WRL::ComPtr<ID3D12Resource> m_lineIb[2];
    D3D12_VERTEX_BUFFER_VIEW m_lineVbv[2]{};
    D3D12_INDEX_BUFFER_VIEW  m_lineIbv[2]{};
};
} // namespace Dark
