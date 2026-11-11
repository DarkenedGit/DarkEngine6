#pragma once

#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Render/Frustum3f.h"
#include "Terrain/FoliageFile.h"

#include "ECS/FrameEntitySet.h"

#include <cstdint>
#include <vector>

struct ID3D12GraphicsCommandList;

namespace Dark
{

    class AssetManager;
    class Camera3D;
    class DrawList;
    class JobSystem;
    class FoliagePipeline;
    class FoliagePrototypes;
    class GpuResourceCache;
    class Mesh;
    class Renderer;
    class ShadowSystem;
    class SkinningUploadRing;
    class SkinnedInstanceBuffer;
    class SkinnedMeshPipeline;
    class World;

    namespace Terrain
    {
        class TerrainGrid;
    }

    // Non-owning pointers. Both apps fill one host and call the same record function.
    struct ShadowCasterHost
    {
        Renderer*             renderer          = nullptr;
        JobSystem*            jobs              = nullptr;
        World*                world             = nullptr;
        AssetManager*         assets            = nullptr;
        GpuResourceCache*     gpu               = nullptr;
        ShadowSystem*         shadows           = nullptr;
        DrawList*             drawList          = nullptr;
        SkinnedInstanceBuffer* skinned          = nullptr;
        const SkinnedMeshPipeline* skinnedShadow = nullptr;
        SkinningUploadRing*   skinRing          = nullptr;
        const Terrain::TerrainGrid* terrain     = nullptr;
        FoliagePipeline*      foliage           = nullptr;
        FoliagePrototypes*    foliagePrototypes = nullptr;
        const std::vector<Terrain::FoliageRecord>* foliageRecords = nullptr;
        const Terrain::FoliageDensity* foliageDensity = nullptr;
        const Camera3D*       camera            = nullptr;
        const Mesh*           ground            = nullptr;
        Math::AABox3f         groundBounds{};
        bool                  drawTerrain       = false;
        bool                  allowGround       = false;
        bool                  instanceStatic    = false;
        bool                  instanceSkinned   = false;
        bool                  rejectTinyScale   = false;
        bool                  foliageActive     = false;
        const FrameEntitySet* acceptedModel   = nullptr;
        const FrameEntitySet* acceptedSkinned = nullptr;
        bool (*skipModel)(void* user, Entity e) = nullptr;
        void (*drawPrimitives)(void* user, ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj, const Frustum3f& frustum, bool recordThisView) = nullptr;
        void* user = nullptr;
    };

    // CreateCommittedResource for casters. Call on main before recordDirectionalShadows.
    void ensureDirectionalShadowModels(ShadowCasterHost& host);

    // True while main is filling the caster array for the shadow worker.
    // Primitive callbacks append mesh and world instead of drawing.
    bool capturingShadowCasters();
    void pushShadowCaster(const Mesh* mesh, const Math::Matrix4f& world);

    // One cascade or one local-light face. The caller picks the list.
    void recordShadowCasterView(
        ShadowCasterHost& host,
        ID3D12GraphicsCommandList* cmd,
        const Math::Matrix4f& lightViewProj,
        const Frustum3f& frustum,
        const Math::Sphere3f* foliageSphere,
        bool beginFoliage,
        bool profileFoliageCull,
        uint8_t& shadowViewIndex);

    void recordDirectionalShadows(ShadowCasterHost& host, ID3D12GraphicsCommandList* mainCmd, uint8_t& shadowViewIndex);

} // namespace Dark
