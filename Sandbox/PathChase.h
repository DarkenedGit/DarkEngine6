#pragma once

#include "AI/AiSystem.h"
#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/LinePipeline.h"

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <vector>

namespace Dark
{
    class Renderer;
    class World;
    class Input;
    class WaterWorld;
    class AssetManager;
    class AssetPinTable;
    class Model;

    namespace Terrain
    {
        class TerrainWorld;
    }

    class PathChase
    {
    public:
        using PackSettings = AI::PackSettings;

        bool init(Renderer& renderer, Terrain::TerrainWorld& terrain, WaterWorld& water, World& world, AssetPinTable& pins, AssetManager& assets);

        void tick(float dt, World& world, Input& input, Terrain::TerrainWorld& terrain, Entity hostPawn, bool playerInWater);
        void expandBounds(Math::AABox3f& bounds) const;
        void drawPaths(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Math::Matrix4f& viewProj);

        Entity walker() const { return m_walker; }
        const std::vector<Math::AABox3f>& cubes() const { return m_cubes; }

        static constexpr int kHunterCount = 3;
        int    hunterCount() const { return m_hunterCount; }
        Entity hunterEntity(int i) const;
        AiSystem&       ai() { return m_ai; }
        const AiSystem& ai() const { return m_ai; }

        void setPackSettings(const PackSettings& settings) { m_ai.setPackSettings(settings); }
        const PackSettings& packSettings() const { return m_ai.packSettings(); }
        void setHunterHitReaction(const HitReactionSettings& settings);
        const HitReactionSettings& hunterHitReaction() const { return m_ai.hunterHitReaction(); }

    private:
        bool bake(Terrain::TerrainWorld& terrain, WaterWorld& water);
        bool spawnWalker(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainWorld& terrain, const AssetRef<Model>& walkerModel);
        bool spawnAgents(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainWorld& terrain);
        bool spawnTrees(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainWorld& terrain, const AssetRef<Model>& treeModel);
        bool createLineBuffers(Renderer& renderer);

        AiSystem        m_ai;
        World*          m_world = nullptr;
        LinePipeline    m_lines;
        std::vector<Math::AABox3f>   m_cubes;
        std::array<Entity, kHunterCount> m_hunters{};
        int             m_hunterCount = 0;
        Entity          m_walker{};
        float           m_agentR = 0.8f;

        static constexpr uint32_t kMaxLineVerts = 2048;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_lineVb[2];
        Microsoft::WRL::ComPtr<ID3D12Resource> m_lineIb[2];
        D3D12_VERTEX_BUFFER_VIEW m_lineVbv[2]{};
        D3D12_INDEX_BUFFER_VIEW  m_lineIbv[2]{};
    };
} // namespace Dark
