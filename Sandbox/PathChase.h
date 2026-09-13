#pragma once

#include "AI/AiSystem.h"
#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"
#include "Render/GpuResourceCache.h"
#include "Render/Mesh.h"
#include "Render/LinePipeline.h"
#include "Assets/Material.h"
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
    class WaterWorld;
    class AssetManager;
    class AssetPinTable;

    namespace Terrain
    {
        class TerrainWorld;
    }

    class PathChase
    {
    public:
        using PackSettings = AI::PackSettings;

        bool init(Renderer& renderer, Terrain::TerrainWorld& terrain, WaterWorld& water, World& world, AssetPinTable& pins, AssetManager& assets, Mesh& cubeMesh,
                  AssetRef<Material> trunkMat, AssetRef<Material> canopyMat, AssetRef<Material> aiMat);

        void tick(float dt, World& world, Input& input, Terrain::TerrainWorld& terrain, Entity hostPawn, bool playerInWater);
        void drawMeshes(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, MeshPipeline& meshPipe, ShadowSystem& shadows, const Camera3D& camera,
                        const MeshFrameConstants& baseCb, Mesh& cubeMesh, DebugFill fill);
        void drawMeshesGBuffer(ID3D12GraphicsCommandList* cmd, GpuResourceCache& gpu, MeshPipeline& meshPipe, const Camera3D& camera, const Math::Matrix4f& prevViewProj,
                               Mesh& cubeMesh, DebugFill fill);
        void drawDepth(ID3D12GraphicsCommandList* cmd, const ShadowSystem& shadows, int cascade, Mesh& cubeMesh) const;
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
        bool spawnWalker(World& world, Terrain::TerrainWorld& terrain);
        bool spawnAgents(World& world, AssetPinTable& pins, AssetManager& assets, Terrain::TerrainWorld& terrain);
        bool createLineBuffers(Renderer& renderer);

        AiSystem        m_ai;
        World*          m_world = nullptr;
        LinePipeline    m_lines;
        std::vector<Math::AABox3f>   m_cubes;
        std::vector<Math::Vector3f>  m_treePos;
        std::array<Entity, kHunterCount> m_hunters{};
        int             m_hunterCount = 0;
        Entity          m_walker{};
        Math::Vector3f  m_walkerPos{};
        Math::Vector3f  m_prevWalkerPos{};
        bool            m_havePrevXforms = false;
        float           m_agentR = 0.8f;
        bool            m_drawWalker = true;

        Mesh               m_trunkMesh;
        Mesh               m_canopyMesh;
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
