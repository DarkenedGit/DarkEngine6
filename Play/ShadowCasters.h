#pragma once

#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Matrix4f.h"
#include "Render/Frustum3f.h"
#include "Render/ShadowCasterSource.h"
#include "Render/ShadowFeature.h"
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
    class LocalShadowSystem;
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

    // Non-owning pointers. Both apps fill one host per frame for the shadow passes.
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
        // `cmd` is null while the worker captures casters (capturingShadowCasters()).
        void (*drawPrimitives)(void* user, ID3D12GraphicsCommandList* cmd, const Math::Matrix4f& lightViewProj, const Frustum3f& frustum, bool recordThisView) = nullptr;
        void* user = nullptr;
    };

    // The Gameplay casters of the shadow passes: models, the host's primitives, terrain or the ground
    // plane, and foliage. Lives for one frame, until the frame graph executed.
    class GameplayShadowCasters final : public IShadowCasterSource
    {
    public:
        explicit GameplayShadowCasters(const ShadowCasterHost& host) :
            m_host(host)
        {
        }

        void prepareDirectional() override;
        void drawGround(ID3D12GraphicsCommandList* cmd, const Frustum3f& frustum) override;
        void drawCasters(ID3D12GraphicsCommandList* cmd, const ShadowCasterView& view) override;
        bool workerReady() const override;
        void beginWorkerCapture() override;
        bool groundOnWorker() const override;
        void captureGround(const Frustum3f& frustum) override;
        void captureCasters(const ShadowCasterView& view) override;
        uint32_t          retainFoliage(const Frustum3f& frustum) override;
        ShadowWorkerDraws workerDraws() override;

    private:
        const ShadowCasterHost& m_host;
    };

    // The render half of `host`, with `casters` as its caster source.
    ShadowFrame shadowFrameOf(const ShadowCasterHost& host, LocalShadowSystem* localShadows, IShadowCasterSource* casters);

} // namespace Dark
