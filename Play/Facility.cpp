#include "Play/Facility.h"

#include "AI/AiComponents.h"
#include "AI/AiSystem.h"
#include "AI/Awareness.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Audio/AudioSystem.h"
#include "Audio/SoundComponents.h"
#include "Character/Health.h"
#include "Character/HealthComponent.h"
#include "Character/PlayerMotorComponent.h"
#include "Character/PlayerStealth.h"
#include "Collision/StaticCollision.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Gameplay/PowerComponents.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Render/GpuUpload.h"

#include <cmath>
#include <vector>

namespace Dark
{
    namespace
    {
        using Math::Quaternion;
        using Math::Vector3f;

        constexpr float kDoorOpenRadius   = 2.2f;
        constexpr float kDoorHoldRadius   = 2.8f;
        constexpr float kDoorPlayerRadius = 2.2f;
        constexpr float kAlarmSeconds     = 15.0f;
        constexpr float kAlarmRadius      = 50.0f;

        bool agentIsWolf(const FacilityHost& host, Entity e)
        {
            return host.ai && host.world && host.ai->entityIsWolf(*host.world, e);
        }

        float horizSq(const Vector3f& a, const Vector3f& b)
        {
            const float dx = a.x - b.x;
            const float dz = a.z - b.z;
            return dx * dx + dz * dz;
        }

        float groundY(const FacilityHost& host, float x, float z, float fallback)
        {
            if (host.heightAt)
                return host.heightAt(host.heightUser, x, z) + 0.5f;
            return fallback;
        }

        void playClick(const FacilityHost& host)
        {
            if (!host.world || !host.audio || !host.assets)
                return;
            playSoundCue(*host.world, *host.audio, *host.assets, host.listener, "click");
        }

        void showBody(World& world, Entity body, bool visible)
        {
            if (ModelComponent* mc = body.valid() ? world.get<ModelComponent>(body) : nullptr)
                mc->visible = visible;
        }

        void restoreUpgradeBody(FacilityState& state, World* world)
        {
            if (!world || !state.upgradeBody.valid() || !world->alive(state.upgradeBody))
                return;
            if (PlayerMotorComponent* pmc = world->get<PlayerMotorComponent>(state.upgradeBody))
            {
                pmc->motor.settings().breathSeconds = state.baseBreath;
                pmc->motor.settings().walkSpeed     = state.baseWalk;
                pmc->motor.settings().sprintSpeed   = state.baseSprint;
            }
            if (HealthComponent* hp = world->get<HealthComponent>(state.upgradeBody))
            {
                const float want = state.baseMaxHp;
                if (want > 0.0f && std::fabs(hp->health.maxHp() - want) > 0.01f)
                {
                    HealthSettings settings = hp->health.settings();
                    const float    now      = hp->health.hp();
                    const float    since    = hp->health.timeSinceDamage();
                    settings.maxHp          = want;
                    hp->health              = Health(settings);
                    hp->health.restore(Math::Clamp(now, 0.0f, want), since);
                }
            }
        }

        void writeScaledVitals(FacilityState& state, PlayerMotor& motor, HealthComponent* hp)
        {
            motor.settings().breathSeconds = state.baseBreath * state.fx.breath;
            motor.settings().walkSpeed     = state.baseWalk * state.fx.speed;
            motor.settings().sprintSpeed   = state.baseSprint * state.fx.speed;
            const float wantMax            = state.baseMaxHp + state.fx.health;
            if (!hp || std::fabs(hp->health.maxHp() - wantMax) <= 0.01f)
                return;
            HealthSettings settings = hp->health.settings();
            const float    gained   = wantMax - settings.maxHp;
            const float    now      = hp->health.hp();
            const float    since    = hp->health.timeSinceDamage();
            settings.maxHp          = wantMax;
            hp->health              = Health(settings);
            hp->health.restore(Math::Clamp(now + Math::Max(0.0f, gained), 0.0f, wantMax), since);
        }

        void applyFacilityUpgrades(FacilityState& state, const FacilityHost& host)
        {
            World* world = host.world;
            if (!world || !host.player.valid())
                return;
            PlayerMotorComponent* pmc = world->get<PlayerMotorComponent>(host.player);
            HealthComponent*      hp  = world->get<HealthComponent>(host.player);
            if (pmc && host.player != state.upgradeBody)
            {
                if (state.upgradeBody.valid())
                    restoreUpgradeBody(state, world);
                state.upgradeBody = host.player;
                state.baseBreath  = pmc->motor.settings().breathSeconds;
                state.baseWalk    = pmc->motor.settings().walkSpeed;
                state.baseSprint  = pmc->motor.settings().sprintSpeed;
                state.baseMaxHp   = hp ? hp->health.maxHp() : 100.0f;
                state.upgradesDirty = true;
            }
            if (state.upgradesDirty)
            {
                state.fx = combineUpgrades(state.defs, state.owned);
                if (pmc)
                    writeScaledVitals(state, pmc->motor, hp);
                state.upgradesDirty = false;
            }
            if (PlayerStealthComponent* stealth = world->get<PlayerStealthComponent>(host.player))
            {
                stealth->exposureScale = state.fx.exposure;
                stealth->noiseScale    = state.fx.quiet;
                stealth->hidden        = state.hiddenIn.valid();
            }
        }

        void publishBlocks(FacilityState& state, const FacilityHost& host, const std::vector<EntityID>& shutIds, const std::vector<Math::AABox3f>& shutBoxes)
        {
            const bool same = shutIds == state.shutIds && host.cubes == state.solidsSource && host.cubeCount == state.solidsSourceCount;
            if (!same)
            {
                state.shutIds            = shutIds;
                state.solidsSource       = host.cubes;
                state.solidsSourceCount  = host.cubeCount;
                state.solids.clear();
                if (!shutIds.empty())
                {
                    if (host.cubes && host.cubeCount > 0)
                        state.solids.assign(host.cubes, host.cubes + host.cubeCount);
                    state.solids.insert(state.solids.end(), shutBoxes.begin(), shutBoxes.end());
                }
            }
            if (state.shutIds.empty())
            {
                state.blocks     = host.cubes;
                state.blockCount = host.cubeCount;
            }
            else
            {
                state.blocks     = state.solids.data();
                state.blockCount = static_cast<int>(state.solids.size());
            }
        }

        void tickDoors(FacilityState& state, const FacilityHost& host, float dt)
        {
            World* world = host.world;
            if (!world)
                return;
            if (state.doorMsgTime > 0.0f)
                state.doorMsgTime -= dt;

            Vector3f playerPos{};
            bool     playerAlive = false;
            if (host.player.valid())
            {
                if (const TransformComponent* pxf = world->get<TransformComponent>(host.player))
                    playerPos = pxf->position;
                const HealthComponent* php = world->get<HealthComponent>(host.player);
                playerAlive                = php && php->health.alive();
            }

            std::vector<Vector3f> guards;
            world->each<AiAgentComponent>([&](Entity e, AiAgentComponent&) {
                const TransformComponent* xf = world->get<TransformComponent>(e);
                const HealthComponent*    hp = world->get<HealthComponent>(e);
                if (!xf || !hp || !hp->health.alive() || agentIsWolf(host, e))
                    return;
                guards.push_back(xf->position);
            });

            std::vector<EntityID>    shutIds;
            std::vector<Math::AABox3f> shutBoxes;
            world->each<DoorComponent>([&](Entity e, DoorComponent& door) {
                bool guardCanOpen = false;
                bool guardHolds   = false;
                if (door.lock.empty())
                {
                    for (const Vector3f& pos : guards)
                    {
                        const float d2 = horizSq(pos, door.closedPos);
                        if (d2 < kDoorOpenRadius * kDoorOpenRadius)
                            guardCanOpen = true;
                        if (d2 < kDoorHoldRadius * kDoorHoldRadius)
                            guardHolds = true;
                    }
                }
                const bool playerHolds = playerAlive && horizSq(playerPos, door.closedPos) < kDoorPlayerRadius * kDoorPlayerRadius;
                const bool wasOpen     = door.wantOpen;
                if (!door.wantOpen && guardCanOpen)
                    door.wantOpen = true;
                else if (door.wantOpen && !guardHolds && !playerHolds)
                    door.wantOpen = false;
                if (door.wantOpen != wasOpen)
                    DE_LOG_INFO(door.wantOpen ? "Door: a guard opened a door" : "Door: closed");

                const float target = door.wantOpen ? 1.0f : 0.0f;
                door.open          = door.open < target ? Math::Min(target, door.open + dt * 2.0f) : Math::Max(target, door.open - dt * 2.0f);
                const bool  alongX = door.size.x > door.size.z;
                const float slide  = (alongX ? door.size.x : door.size.z) * 0.95f * door.open;
                if (TransformComponent* xf = world->get<TransformComponent>(e))
                    xf->position = door.closedPos + (alongX ? Vector3f{ slide, 0.0f, 0.0f } : Vector3f{ 0.0f, 0.0f, slide });
                if (door.open < 0.8f)
                {
                    shutIds.push_back(e.id());
                    shutBoxes.push_back(Math::AABox3f::FromCenterExtents(door.closedPos, door.size * 0.5f));
                }
            });
            publishBlocks(state, host, shutIds, shutBoxes);
        }

        void leaveHide(FacilityState& state, const FacilityHost& host)
        {
            World* world = host.world;
            if (!world)
                return;
            const Entity        spot = state.hiddenIn;
            state.hiddenIn           = {};
            state.hideWitness        = {};
            state.hideFound          = 0.0f;
            const Entity        body = host.player;
            TransformComponent* pxf  = body.valid() ? world->get<TransformComponent>(body) : nullptr;
            const TransformComponent* sxf = world->alive(spot) ? world->get<TransformComponent>(spot) : nullptr;
            const HideSpotComponent*  hs  = sxf ? world->get<HideSpotComponent>(spot) : nullptr;
            if (pxf && sxf && hs)
            {
                const Vector3f out = sxf->position + hs->front;
                pxf->position      = Vector3f{ out.x, groundY(host, out.x, out.z, out.y), out.z };
            }
            showBody(*world, body, true);
            playClick(host);
            DE_LOG_INFO("Player: left hiding spot");
        }

        void enterHide(FacilityState& state, const FacilityHost& host, Entity spot)
        {
            World* world = host.world;
            if (!world)
                return;
            const Entity              body = host.player;
            TransformComponent*       pxf  = body.valid() ? world->get<TransformComponent>(body) : nullptr;
            const TransformComponent* sxf  = world->get<TransformComponent>(spot);
            const HideSpotComponent*  hs   = world->get<HideSpotComponent>(spot);
            if (!pxf || !sxf || !hs)
                return;
            state.hiddenIn    = spot;
            state.hideFound   = 0.0f;
            state.hideWitness = {};
            float closest     = 1.0e9f;
            world->each<AiAgentComponent>([&](Entity g, AiAgentComponent& ai) {
                const TransformComponent* gxf = world->get<TransformComponent>(g);
                const HealthComponent*    hp  = world->get<HealthComponent>(g);
                if (!gxf || !hp || !hp->health.alive() || agentIsWolf(host, g))
                    return;
                const float d          = (gxf->position - pxf->position).MagnitudeSqrd();
                const bool  justLost   = ai.aware.alertness == AI::Alertness::Alert && ai.lostFor < 1.5f && d < 30.0f * 30.0f;
                const bool  pointBlank = d < 4.0f * 4.0f && ai.aware.alertness != AI::Alertness::Calm;
                if ((ai.aware.seesNow || justLost || pointBlank || d < 2.5f * 2.5f) && d < closest)
                {
                    closest            = d;
                    state.hideWitness  = g;
                }
            });
            if (state.hideWitness.valid())
                DE_LOG_INFO("Guard {}: saw the player hide", state.hideWitness.id());
            pxf->position = Vector3f{ sxf->position.x, groundY(host, sxf->position.x, sxf->position.z, sxf->position.y), sxf->position.z };
            showBody(*world, body, false);
            playClick(host);
            DE_LOG_INFO("Player: hiding in {}", hs->label);
        }

        void tickHide(FacilityState& state, const FacilityHost& host, float dt)
        {
            if (!state.hiddenIn.valid())
                return;
            World* world = host.world;
            if (!world)
                return;
            const Entity              body = host.player;
            TransformComponent*       pxf  = body.valid() ? world->get<TransformComponent>(body) : nullptr;
            const TransformComponent* sxf  = world->alive(state.hiddenIn) ? world->get<TransformComponent>(state.hiddenIn) : nullptr;
            if (!pxf || !sxf)
            {
                state.hiddenIn = {};
                showBody(*world, body, true);
                return;
            }
            pxf->position = Vector3f{ sxf->position.x, groundY(host, sxf->position.x, sxf->position.z, sxf->position.y), sxf->position.z };
            if (PlayerMotorComponent* pmc = body.valid() ? world->get<PlayerMotorComponent>(body) : nullptr)
                pmc->motor.setHorizontalVelocity(0.0f, 0.0f);

            Entity finder{};
            world->each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
                const TransformComponent* gxf = world->get<TransformComponent>(e);
                const HealthComponent*    hp  = world->get<HealthComponent>(e);
                if (finder.valid() || !gxf || !hp || !hp->health.alive() || agentIsWolf(host, e))
                    return;
                const bool hunting = ai.aware.alertness == AI::Alertness::Alert || ai.aware.alertness == AI::Alertness::Searching
                                  || ai.aware.alertness == AI::Alertness::Investigating;
                if (hunting && horizSq(gxf->position, sxf->position) < 1.8f * 1.8f)
                    finder = e;
            });
            float                 catchAfter = 1.2f;
            AiAgentComponent*     wai = state.hideWitness.valid() && world->alive(state.hideWitness) ? world->get<AiAgentComponent>(state.hideWitness) : nullptr;
            const HealthComponent* whp = wai ? world->get<HealthComponent>(state.hideWitness) : nullptr;
            const TransformComponent* wxf = wai ? world->get<TransformComponent>(state.hideWitness) : nullptr;
            const HideSpotComponent*  hs  = world->get<HideSpotComponent>(state.hiddenIn);
            if (wai && whp && whp->health.alive() && wxf && hs)
            {
                wai->aware.alertness = AI::Alertness::Alert;
                wai->aware.noticePos = sxf->position + hs->front;
                wai->aware.heardOnly = false;
                wai->aware.stateTime = 0.0f;
                if (horizSq(wxf->position, sxf->position) < 2.2f * 2.2f)
                    finder = state.hideWitness;
                catchAfter = 0.5f;
            }
            else
                state.hideWitness = {};
            state.hideFound = finder.valid() ? state.hideFound + dt : 0.0f;
            if (state.hideFound < catchAfter)
                return;
            const Vector3f where = pxf->position;
            const Entity   who   = finder;
            leaveHide(state, host);
            if (host.ai && who.valid())
                host.ai->forceAlert(*world, who, where);
            DE_LOG_INFO("Guard: found the player hiding");
        }

        void raiseAlarm(FacilityState& state, const FacilityHost& host, const Vector3f& at, const Vector3f& playerPos)
        {
            World* world = host.world;
            if (!world)
                return;
            const bool fresh = state.alarmTime <= 0.0f;
            state.alarmTime  = kAlarmSeconds;
            int called       = 0;
            world->each<AiAgentComponent>([&](Entity g, AiAgentComponent&) {
                const TransformComponent* gxf = world->get<TransformComponent>(g);
                const HealthComponent*    hp  = world->get<HealthComponent>(g);
                if (!gxf || !hp || !hp->health.alive() || agentIsWolf(host, g) || horizSq(gxf->position, at) > kAlarmRadius * kAlarmRadius)
                    return;
                if (host.ai)
                    host.ai->forceAlert(*world, g, playerPos);
                ++called;
            });
            if (fresh)
                DE_LOG_INFO("Alarm: camera spotted the player, {} guard(s) alerted", called);
        }

        void tickCameras(FacilityState& state, const FacilityHost& host, float dt)
        {
            World* world = host.world;
            if (!world)
                return;
            if (state.alarmTime > 0.0f)
            {
                state.alarmTime -= dt;
                state.alarmBeep -= dt;
                if (state.alarmBeep <= 0.0f)
                {
                    state.alarmBeep = 0.6f;
                    if (host.audio && host.assets)
                    {
                        if (!state.alarmTone)
                            state.alarmTone = host.audio->createTone(*host.assets, 920.0f, 0.22f, 0.3f);
                        if (state.alarmTone)
                            host.audio->play2D(state.alarmTone, 0.6f);
                    }
                }
            }
            const Entity              body = host.player;
            const TransformComponent* pxf  = body.valid() ? world->get<TransformComponent>(body) : nullptr;
            const HealthComponent*    php  = body.valid() ? world->get<HealthComponent>(body) : nullptr;
            const bool                visible = pxf && php && php->health.alive() && !state.hiddenIn.valid() && !host.submerged;
            const PlayerStealthComponent* stealth = body.valid() ? world->get<PlayerStealthComponent>(body) : nullptr;
            const float               exposure = stealth ? stealth->exposure : 1.0f;
            const Vector3f            chest    = pxf ? pxf->position + Vector3f{ 0.0f, 0.6f, 0.0f } : Vector3f{};
            const Math::AABox3f*      blocks   = state.blocks;
            const int                 blockCount = state.blockCount;
            world->each<SecurityCameraComponent>([&](Entity e, SecurityCameraComponent& cam) {
                bool powered = true;
                world->each<PowerBoxComponent>([&](Entity, PowerBoxComponent& box) {
                    if (!cam.def.circuit.empty() && box.circuit == cam.def.circuit && !box.on)
                        powered = false;
                });
                LocalLightComponent* beam = world->alive(cam.light) ? world->get<LocalLightComponent>(cam.light) : nullptr;
                if (beam)
                    beam->enabled = powered;
                if (!powered)
                {
                    cam.seen = 0.0f;
                    return;
                }
                bool sees = false;
                if (visible)
                {
                    Vector3f    to   = chest - cam.def.pos;
                    const float dist = to.Magnitude();
                    if (dist > 0.5f && dist < cam.def.range)
                    {
                        to *= 1.0f / dist;
                        const float    rad = cam.aimYaw * 0.0174533f;
                        const Vector3f fwd{ std::sin(rad), -0.35f, std::cos(rad) };
                        const Vector3f flatFwd{ fwd.x, 0.0f, fwd.z };
                        const Vector3f flatTo{ to.x, 0.0f, to.z };
                        const float    cosA = flatFwd.Dot(flatTo) / Math::Max(1.0e-4f, flatFwd.Magnitude() * flatTo.Magnitude());
                        if (cosA > std::cos(28.0f * 0.0174533f))
                        {
                            sees = true;
                            const Math::Ray3f ray{ cam.def.pos, to };
                            for (int i = 0; i < blockCount; ++i)
                            {
                                const Collision::RayHit3D hit = Collision::Intersect(ray, blocks[i]);
                                if (hit.hit && hit.t > 0.3f && hit.t < dist - 0.3f)
                                {
                                    sees = false;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (sees && exposure > 0.08f)
                    cam.seen += dt * (0.7f + 1.3f * exposure);
                else
                    cam.seen = Math::Max(0.0f, cam.seen - dt * 0.5f);
                if (cam.seen >= 1.0f && pxf)
                {
                    cam.seen = 0.0f;
                    raiseAlarm(state, host, cam.def.pos, pxf->position);
                }
                if (cam.seen <= 0.0f)
                    cam.phase += dt * 0.45f;
                cam.aimYaw = cam.def.yaw + std::sin(cam.phase) * cam.def.sweep * 0.5f;
                const float rad = cam.aimYaw * 0.0174533f;
                Vector3f    dir{ std::sin(rad), -0.35f, std::cos(rad) };
                dir.Normalize();
                const Quaternion look = Quaternion::FromLookRotation(dir, Vector3f::Y_AXIS);
                if (TransformComponent* xf = world->get<TransformComponent>(e))
                    xf->rotation = look;
                if (TransformComponent* lxf = world->alive(cam.light) ? world->get<TransformComponent>(cam.light) : nullptr)
                {
                    lxf->position = cam.def.pos + dir * 0.3f;
                    lxf->rotation = look;
                }
                if (beam)
                    beam->intensity = cam.seen > 0.0f ? 900.0f + 2500.0f * cam.seen : 900.0f;
            });
        }

        void useDoor(FacilityState& state, const FacilityHost& host, Entity e)
        {
            World* world = host.world;
            if (!world)
                return;
            DoorComponent* door = world->get<DoorComponent>(e);
            if (!door)
                return;
            if (door->wantOpen)
            {
                door->wantOpen = false;
                playClick(host);
                return;
            }
            if (!door->lock.empty())
            {
                const InventoryComponent* inv = host.player.valid() ? world->get<InventoryComponent>(host.player) : nullptr;
                const ItemDef*            key = findItemDef(door->lock);
                if (!inv || !hasItem(*inv, door->lock))
                {
                    state.doorMsg     = std::string("Locked. Needs a ") + (key ? key->name : door->lock.c_str());
                    state.doorMsgTime = 2.5f;
                    playClick(host);
                    return;
                }
                state.doorMsg     = std::string("Unlocked with the ") + (key ? key->name : door->lock.c_str());
                state.doorMsgTime = 2.5f;
                DE_LOG_INFO("Door: unlocked with {}", door->lock);
                door->lock.clear();
            }
            door->wantOpen = true;
            playClick(host);
        }

    } // namespace

    void tickFacility(FacilityState& state, const FacilityHost& host, float dt)
    {
        tickDoors(state, host, dt);
        tickHide(state, host, dt);
        tickCameras(state, host, dt);
        applyFacilityUpgrades(state, host);
    }

    bool handleFacilityUse(FacilityState& state, const FacilityHost& host, const PlayInteractPick& pick)
    {
        if (!host.world || !host.player.valid())
            return false;
        if (state.hiddenIn.valid())
        {
            leaveHide(state, host);
            return true;
        }
        if (pick.kind == PlayInteract::Hide)
        {
            enterHide(state, host, pick.e);
            return true;
        }
        if (pick.kind == PlayInteract::Door)
        {
            useDoor(state, host, pick.e);
            return true;
        }
        return false;
    }

    void clearFacilitySession(FacilityState& state, World* world, Entity player)
    {
        restoreUpgradeBody(state, world);
        if (world && player.valid())
            showBody(*world, player, true);
        state.owned.clear();
        state.upgradeBody = {};
        state.fx            = {};
        state.baseBreath    = 0.0f;
        state.baseWalk      = 0.0f;
        state.baseSprint    = 0.0f;
        state.baseMaxHp     = 0.0f;
        state.upgradesDirty = true;
        state.hiddenIn      = {};
        state.hideWitness   = {};
        state.hideFound     = 0.0f;
        state.doorMsg.clear();
        state.doorMsgTime = 0.0f;
        state.alarmTime   = 0.0f;
        state.alarmBeep   = 0.0f;
        state.shutIds.clear();
        state.solids.clear();
        state.solidsSource      = nullptr;
        state.solidsSourceCount = -1;
        state.blocks            = nullptr;
        state.blockCount        = 0;
    }

    void refreshFacilityUpgrades(FacilityState& state, const FacilityHost& host)
    {
        state.upgradesDirty = true;
        applyFacilityUpgrades(state, host);
    }

    void spawnBuildingFixtures(const BuildingFixtureSpawn& spawn, const std::vector<BuildingContainer>& containers,
                               const std::vector<BuildingHideSpot>& hides, const std::vector<BuildingDoor>& doors,
                               const std::vector<BuildingCamera>& cameras)
    {
        World* world = spawn.world;
        if (!world)
            return;
        if (spawn.ai)
        {
            std::vector<Vector3f> checkFrom;
            checkFrom.reserve(hides.size());
            for (const BuildingHideSpot& h : hides)
                checkFrom.push_back(h.pos + h.front * 0.85f);
            spawn.ai->setHideSpots(std::move(checkFrom));
        }
        std::deque<std::string>* labels = spawn.labels;
        for (const BuildingContainer& c : containers)
        {
            if (!labels)
                break;
            LootComponent loot;
            labels->push_back(c.label);
            loot.label = labels->back().c_str();
            for (const BuildingLoot& l : c.loot)
            {
                if (const ItemDef* def = findItemDef(l.item))
                    addItem(loot.contents, *def, Math::Max(1, l.count));
                else
                    DE_LOG_WARN("Building: unknown loot item '{}'", l.item);
            }
            if (itemCount(loot.contents) == 0)
                continue;
            const Entity e = world->createEntity();
            world->emplace<TransformComponent>(e, c.pos, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
            world->emplace<LootComponent>(e, loot);
            setInteractable(*world, e, InteractKind::Loot, kInteractLootRange);
        }

        const bool canDraw = spawn.renderer && spawn.assets && spawn.pins;
        AssetRef<Model> doorModel = (!canDraw || doors.empty()) ? AssetRef<Model>{} : loadAndUploadModel(*spawn.renderer, *spawn.assets, "models/unit_cube.gltf");
        AssetRef<Model> camModel = (!canDraw || cameras.empty()) ? AssetRef<Model>{} : loadAndUploadModel(*spawn.renderer, *spawn.assets, "models/unit_cube.gltf");
        for (const BuildingCamera& c : cameras)
        {
            const Entity e = world->createEntity();
            world->emplace<TransformComponent>(e, c.pos, Quaternion::IDENTITY, Vector3f{ 0.3f, 0.25f, 0.45f });
            if (camModel)
            {
                ModelComponent mc{};
                mc.modelAssetID = camModel->id;
                setModelComponent(*world, *spawn.pins, *spawn.assets, e, mc);
            }
            const Entity lamp = world->createEntity();
            world->emplace<TransformComponent>(lamp, c.pos, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
            LocalLightComponent& beam = world->emplace<LocalLightComponent>(lamp);
            beam.type                 = LocalLightType::Spot;
            beam.color                = Vector3f{ 1.0f, 0.15f, 0.1f };
            beam.intensity            = 900.0f;
            beam.range                = c.range + 2.0f;
            beam.innerConeDeg         = 18.0f;
            beam.outerConeDeg         = 28.0f;
            SecurityCameraComponent cam;
            cam.def    = c;
            cam.aimYaw = c.yaw;
            cam.light  = lamp;
            world->emplace<SecurityCameraComponent>(e, cam);
        }
        for (const BuildingDoor& d : doors)
        {
            const Entity e = world->createEntity();
            world->emplace<TransformComponent>(e, d.center, Quaternion::IDENTITY, d.size);
            if (doorModel)
            {
                ModelComponent mc{};
                mc.modelAssetID = doorModel->id;
                mc.castShadow   = true;
                setModelComponent(*world, *spawn.pins, *spawn.assets, e, mc);
            }
            DoorComponent door;
            door.closedPos = d.center;
            door.size      = d.size;
            door.lock      = d.lock;
            world->emplace<DoorComponent>(e, door);
            setInteractable(*world, e, InteractKind::Door, kInteractDoorRange);
        }
        for (const BuildingHideSpot& h : hides)
        {
            const Entity e = world->createEntity();
            world->emplace<TransformComponent>(e, h.pos, Quaternion::IDENTITY, Vector3f{ 1.0f, 1.0f, 1.0f });
            world->emplace<HideSpotComponent>(e, HideSpotComponent{ h.front, h.label });
            setInteractable(*world, e, InteractKind::Hide, kInteractHideRange);
        }
    }

} // namespace Dark
