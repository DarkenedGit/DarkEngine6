#pragma once

#include "AI/AiSystem.h"
#include "Terrain/TerrainGround.h"
#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/LinePipeline.h"

#include <d3d12.h>
#include <wrl/client.h>
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
    struct TransformComponent;

    namespace Terrain
    {
        class TerrainGrid;
    }

    class PathChase
    {
    public:
        using PackSettings = AI::PackSettings;

        bool init(Renderer& renderer, Terrain::TerrainGrid& terrain, WaterWorld& water, World& world, AssetPinTable& pins, AssetManager& assets);

        void tick(float dt, World& world, Input& input, Terrain::TerrainGrid& terrain, Entity hostPawn, bool playerInWater);
        void setGround(const Terrain::TerrainGround* ground) { m_ground = ground; }
        void expandBounds(Math::AABox3f& bounds) const;
        void drawPaths(ID3D12GraphicsCommandList* cmd, Renderer& renderer, const Math::Matrix4f& viewProj);
        LinePipeline& linePipeline() { return m_lines; }

        Entity walker() const { return m_walker; }
        const std::vector<Math::AABox3f>& cubes() const { return m_cubes; }

        int    hunterCount() const { return static_cast<int>(m_hunters.size()); }
        Entity hunterEntity(int i) const;
        // Scene pawns. Ground Y is the caller's. Staggers repath so the pack does not query together.
        Entity spawnListedHunter(World& world, AssetPinTable& pins, AssetManager& assets, const TransformComponent& xf);
        void   addHunter(Entity e);
        AiSystem&       ai() { return m_ai; }
        const AiSystem& ai() const { return m_ai; }

        void setPackSettings(const PackSettings& settings) { m_ai.setPackSettings(settings); }
        const PackSettings& packSettings() const { return m_ai.packSettings(); }
        void setHunterHitReaction(const HitReactionSettings& settings);
        const HitReactionSettings& hunterHitReaction() const { return m_ai.hunterHitReaction(); }

    private:
        bool bake(Terrain::TerrainGrid& terrain, WaterWorld& water);
        bool spawnWalker(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainGrid& terrain, const AssetRef<Model>& walkerModel);
        bool createLineBuffers(Renderer& renderer);

        AiSystem        m_ai;
        const Terrain::TerrainGround* m_ground = nullptr;
        World*          m_world = nullptr;
        LinePipeline    m_lines;
        std::vector<Math::AABox3f> m_cubes;
        std::vector<Entity>        m_hunters;
        Entity          m_walker{};
        float           m_agentR = 0.8f;

        static constexpr uint32_t kMaxLineVerts = 2048;
        Microsoft::WRL::ComPtr<ID3D12Resource> m_lineVb[2];
        Microsoft::WRL::ComPtr<ID3D12Resource> m_lineIb[2];
        D3D12_VERTEX_BUFFER_VIEW m_lineVbv[2]{};
        D3D12_INDEX_BUFFER_VIEW  m_lineIbv[2]{};
    };
} // namespace Dark
