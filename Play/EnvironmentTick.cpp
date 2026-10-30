#include "Play/EnvironmentTick.h"

#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Audio/AudioSystem.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/SkillCatalog.h"
#include "Character/SkillComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Particles/LeafRenderer.h"
#include "Render/Camera3D.h"
#include "Render/Frustum3f.h"
#include "Terrain/LeafRustle.h"
#include "Terrain/TerrainGrid.h"

namespace Dark
{

    void stepFallingLeaves(const FallingLeafTick& tick, float dt)
    {
        if (!tick.rustle || !tick.audio)
            return;
        if (!tick.terrain || !tick.terrain->valid() || !(dt > 0.0f) || !tick.field || !tick.assets || !tick.camera || !tick.simSeconds)
        {
            tick.rustle->stopAll(*tick.audio);
            return;
        }

        *tick.simSeconds += static_cast<double>(dt);
        float treeTop = 12.0f;
        if (tick.treeModel && tick.treeModel->bounds().Max.y > 0.05f)
            treeTop = tick.treeModel->bounds().Max.y;

        const Math::Vector3f cam = tick.camera->GetPosition();
        Frustum3f            frustum(tick.camera->GetCullViewProj());
        Terrain::LeafField::Crown crowns[Terrain::LeafField::kMaxCrowns];
        const int crownCount = gatherLeafCrowns(*tick.terrain, tick.foliage, cam, nullptr, treeTop, crowns, Terrain::LeafField::kMaxCrowns);

        Terrain::LeafField::Step step;
        step.dt         = dt;
        step.simTime    = *tick.simSeconds;
        step.listenerX  = cam.x;
        step.listenerY  = cam.y;
        step.listenerZ  = cam.z;
        step.cameraX    = cam.x;
        step.cameraZ    = cam.z;
        step.crowns     = crowns;
        step.crownCount = crownCount;
        step.tryHeight  = [](float x, float z, float& y, void* user) {
            return static_cast<const Terrain::TerrainGrid*>(user)->tryHeightAtWorld(x, z, y);
        };
        step.heightUser   = tick.terrain;
        step.sampleNormal = [](float x, float z, float& nx, float& ny, float& nz, void* user) {
            const Math::Vector3f n = static_cast<const Terrain::TerrainGrid*>(user)->normalAtWorld(x, z);
            nx                     = n.x;
            ny                     = n.y;
            nz                     = n.z;
        };
        step.normalUser  = tick.terrain;
        step.lake        = tick.lake;
        step.lakeLevel   = tick.lakeLevel;
        step.ponds       = tick.ponds;
        step.pondCount   = tick.pondCount;
        step.cellMetres  = tick.terrain->cellSize();
        step.inFrustum   = [](float x, float y, float z, void* user) {
            return static_cast<const Frustum3f*>(user)->Contains(Math::Vector3f(x, y, z));
        };
        step.frustumUser = &frustum;
        tick.field->setShedScale(tick.shedScale);
        tick.field->step(step);
        tick.rustle->tick(*tick.audio, *tick.assets, crowns, crownCount, cam.x, cam.z, *tick.simSeconds, dt);
    }

    bool fillGrassPlayerSample(World& world, Entity body, float& x, float& z, Terrain::GrassPlayerSample& sample)
    {
        const TransformComponent* xf = body.valid() ? world.get<TransformComponent>(body) : nullptr;
        if (!xf)
            return false;
        x        = xf->position.x;
        z        = xf->position.z;
        sample   = {};
        sample.x = x;
        sample.z = z;
        if (const PlayerMotorComponent* pmc = world.get<PlayerMotorComponent>(body))
        {
            sample.velX                = pmc->motor.velocity().x;
            sample.velZ                = pmc->motor.velocity().z;
            const PlayerMoveState move = pmc->motor.state();
            sample.shoving             = move == PlayerMoveState::Grounded || move == PlayerMoveState::Crouch || move == PlayerMoveState::Dodge;
        }
        return true;
    }

    void setViewAudioListener(Audio::AudioSystem& audio, const Camera3D& camera, World* world, Entity body, bool applyHearSkill)
    {
        Audio::AudioListener lis{};
        lis.position      = camera.GetPosition();
        lis.forward       = camera.GetLook();
        lis.up            = camera.GetUp();
        lis.distanceScale = 1.0f;
        if (applyHearSkill && world && body.valid() && world->alive(body))
        {
            if (const SkillComponent* sk = world->get<SkillComponent>(body))
                lis.distanceScale = skillScalar(SkillId::Hear, SkillScalar::HearScale, sk->level(SkillId::Hear));
        }
        audio.setListener(lis);
    }

} // namespace Dark
