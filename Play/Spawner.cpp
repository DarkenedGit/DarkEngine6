#include "Play/Spawner.h"

#include "Character/HealthComponent.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"

#include <cmath>
#include <string_view>

namespace Dark
{
namespace
{

    uint32_t xorshift32(uint32_t& state)
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    float unitFloat(uint32_t& state)
    {
        return static_cast<float>(xorshift32(state) >> 8) * (1.0f / 16777216.0f);
    }

    void ensureRng(SpawnerRuntime& runtime, uint32_t seed)
    {
        if (runtime.seeded)
            return;
        runtime.rng    = seed == 0u ? 1u : seed;
        runtime.seeded = true;
    }

    // Uniform disc: angle plus sqrt(u) * radius. A zero radius stays on the origin.
    void sampleDisc(SpawnerRuntime& runtime, float originX, float originZ, float radius, float& outX, float& outZ)
    {
        const float u   = unitFloat(runtime.rng);
        const float v   = unitFloat(runtime.rng);
        const float ang = u * Math::TwoPi;
        const float r   = std::sqrt(v) * radius;
        outX            = originX + std::cos(ang) * r;
        outZ            = originZ + std::sin(ang) * r;
    }

    void retireExtraSlots(SpawnerRuntime& runtime, int count, const bool alive[kSpawnerSlotCount])
    {
        for (int i = count; i < kSpawnerSlotCount; ++i)
        {
            if (runtime.slots[i].tracked && !alive[i])
            {
                runtime.slots[i].tracked  = false;
                runtime.slots[i].cooldown = 0.0f;
            }
        }
    }

    int emitSlot(SpawnerRuntime& runtime, const SpawnerDesc& desc, float originX, float originZ, int slot, SpawnerRequest* out, int written, int outCapacity)
    {
        if (written >= outCapacity)
            return written;
        SpawnerRequest& req = out[written];
        req.unit            = desc.unit;
        req.slot            = slot;
        req.seed            = desc.seed == 0u ? 1u : desc.seed;
        sampleDisc(runtime, originX, originZ, desc.radius, req.x, req.z);
        runtime.slots[slot].tracked  = true;
        runtime.slots[slot].cooldown = 0.0f;
        return written + 1;
    }

    // Fills every untracked slot in 0..count-1. A short buffer finishes on a later tick.
    int fillWave(SpawnerRuntime& runtime, const SpawnerDesc& desc, float originX, float originZ, int count, SpawnerRequest* out, int written, int outCapacity)
    {
        bool complete = true;
        for (int i = 0; i < count; ++i)
        {
            if (runtime.slots[i].tracked)
                continue;
            if (written >= outCapacity)
            {
                complete = false;
                break;
            }
            written = emitSlot(runtime, desc, originX, originZ, i, out, written, outCapacity);
        }
        if (complete)
        {
            runtime.wavesSpawned += 1;
            runtime.filling  = false;
            runtime.waveTime = 0.0f;
        }
        return written;
    }

} // namespace

    const char* toString(SpawnerMode mode)
    {
        return mode == SpawnerMode::Waves ? "waves" : "maintain";
    }

    const char* toString(SpawnerUnit unit)
    {
        if (unit == SpawnerUnit::Wolf)
            return "wolf";
        if (unit == SpawnerUnit::Crow)
            return "crow";
        return "hunter";
    }

    const char* toString(SpawnerWaveGate gate)
    {
        return gate == SpawnerWaveGate::Time ? "time" : "cleared";
    }

    bool tryParseSpawnerMode(std::string_view text, SpawnerMode& out)
    {
        if (text == "maintain")
        {
            out = SpawnerMode::Maintain;
            return true;
        }
        if (text == "waves" || text == "wave")
        {
            out = SpawnerMode::Waves;
            return true;
        }
        return false;
    }

    bool tryParseSpawnerUnit(std::string_view text, SpawnerUnit& out)
    {
        if (text == "hunter" || text == "enemy")
        {
            out = SpawnerUnit::Hunter;
            return true;
        }
        if (text == "wolf")
        {
            out = SpawnerUnit::Wolf;
            return true;
        }
        if (text == "crow")
        {
            out = SpawnerUnit::Crow;
            return true;
        }
        return false;
    }

    bool tryParseSpawnerWaveGate(std::string_view text, SpawnerWaveGate& out)
    {
        if (text == "time" || text == "timer" || text == "interval")
        {
            out = SpawnerWaveGate::Time;
            return true;
        }
        if (text == "cleared" || text == "clear" || text == "killed" || text == "dead")
        {
            out = SpawnerWaveGate::Cleared;
            return true;
        }
        return false;
    }

    void clampSpawnerDesc(SpawnerDesc& desc)
    {
        desc.count = Math::Clamp(desc.count, 1, kSpawnerSlotCount);
        if (desc.radius < 0.0f)
            desc.radius = 0.0f;
        if (desc.radius > 256.0f)
            desc.radius = 256.0f;
        if (desc.respawnDelay < 0.0f)
            desc.respawnDelay = 0.0f;
        if (desc.waveInterval < 0.0f)
            desc.waveInterval = 0.0f;
        if (desc.maxWaves < 0)
            desc.maxWaves = 0;
        if (desc.seed == 0u)
            desc.seed = 1u;
    }

    void resetSpawnerRuntime(SpawnerRuntime& runtime)
    {
        runtime = {};
    }

    bool spawnerOccupantAlive(World& world, Entity occupant)
    {
        if (!occupant.valid() || !world.alive(occupant))
            return false;
        const HealthComponent* hp = world.get<HealthComponent>(occupant);
        if (!hp)
            return true;
        return hp->health.alive();
    }

    int tickSpawner(const SpawnerDesc& desc, SpawnerRuntime& runtime, float originX, float originZ, float dt, const bool alive[kSpawnerSlotCount], SpawnerRequest* out, int outCapacity)
    {
        if (!alive || !out || outCapacity <= 0)
            return 0;
        if (dt < 0.0f)
            dt = 0.0f;

        SpawnerDesc tuned = desc;
        clampSpawnerDesc(tuned);
        const int count = tuned.count;
        retireExtraSlots(runtime, count, alive);

        if (!tuned.enabled)
        {
            const bool watchDeaths = tuned.mode == SpawnerMode::Maintain || tuned.gate == SpawnerWaveGate::Cleared;
            if (watchDeaths)
            {
                for (int i = 0; i < count; ++i)
                {
                    SpawnerSlot& slot = runtime.slots[i];
                    if (!slot.tracked || alive[i])
                        continue;
                    slot.tracked = false;
                    if (tuned.mode == SpawnerMode::Maintain)
                        slot.cooldown = tuned.respawnDelay;
                }
            }
            return 0;
        }

        ensureRng(runtime, tuned.seed);

        if (tuned.mode == SpawnerMode::Maintain)
        {
            runtime.filling = false;
            int written     = 0;
            for (int i = 0; i < count; ++i)
            {
                SpawnerSlot& slot = runtime.slots[i];
                if (slot.tracked && !alive[i])
                {
                    slot.tracked  = false;
                    slot.cooldown = tuned.respawnDelay;
                    if (slot.cooldown > 0.0f)
                        continue;
                }
                if (slot.tracked)
                    continue;
                if (slot.cooldown > 0.0f)
                {
                    slot.cooldown -= dt;
                    // 1.5 - 1.4 is a few ulps over 0.1, so the tick that spends the
                    // delay can leave a tiny positive. That residue is spent.
                    if (slot.cooldown > 1.0e-4f)
                        continue;
                    slot.cooldown = 0.0f;
                }
                written = emitSlot(runtime, tuned, originX, originZ, i, out, written, outCapacity);
            }
            return written;
        }

        if (tuned.gate == SpawnerWaveGate::Time)
        {
            if (!runtime.filling)
            {
                if (runtime.wavesSpawned > 0)
                    runtime.waveTime += dt;
                const bool underCap = tuned.maxWaves == 0 || runtime.wavesSpawned < tuned.maxWaves;
                const bool due      = runtime.wavesSpawned == 0 || (tuned.waveInterval > 0.0f && runtime.waveTime >= tuned.waveInterval);
                if (underCap && due)
                {
                    // The previous wave stays in the level. Slots now track only the new wave.
                    if (runtime.wavesSpawned > 0)
                    {
                        for (int i = 0; i < count; ++i)
                        {
                            runtime.slots[i].tracked  = false;
                            runtime.slots[i].cooldown = 0.0f;
                        }
                    }
                    runtime.filling  = true;
                    runtime.waveTime = 0.0f;
                }
            }
            if (!runtime.filling)
                return 0;
            return fillWave(runtime, tuned, originX, originZ, count, out, 0, outCapacity);
        }

        for (int i = 0; i < count; ++i)
        {
            if (runtime.slots[i].tracked && !alive[i])
                runtime.slots[i].tracked = false;
        }
        if (!runtime.filling)
        {
            bool any = false;
            for (int i = 0; i < count; ++i)
            {
                if (runtime.slots[i].tracked)
                    any = true;
            }
            const bool underCap = tuned.maxWaves == 0 || runtime.wavesSpawned < tuned.maxWaves;
            if (!any && underCap)
                runtime.filling = true;
        }
        if (!runtime.filling)
            return 0;
        return fillWave(runtime, tuned, originX, originZ, count, out, 0, outCapacity);
    }

    void tickSpawnerJobs(World& world, float dt, SpawnerSpawnFn spawn, void* user)
    {
        if (!spawn || dt < 0.0f)
            return;

        struct Job
        {
            Entity         spawner{};
            SpawnerRequest req{};
        };
        std::vector<Job> jobs;
        world.each<SpawnerComponent>([&](Entity e, SpawnerComponent& spawner) {
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            bool alive[kSpawnerSlotCount]{};
            for (int i = 0; i < kSpawnerSlotCount; ++i)
                alive[i] = spawnerOccupantAlive(world, spawner.runtime.slots[i].occupant);
            SpawnerRequest reqs[kSpawnerSlotCount];
            const int n = tickSpawner(spawner.desc, spawner.runtime, xf->position.x, xf->position.z, dt, alive, reqs, kSpawnerSlotCount);
            for (int i = 0; i < n; ++i)
                jobs.push_back(Job{ e, reqs[i] });
        });
        for (const Job& job : jobs)
        {
            const Entity      unit    = spawn(user, job.spawner, job.req);
            SpawnerComponent* spawner = world.get<SpawnerComponent>(job.spawner);
            if (!spawner || job.req.slot < 0 || job.req.slot >= kSpawnerSlotCount)
                continue;
            if (unit.valid())
                spawner->runtime.slots[job.req.slot].occupant = unit;
            else
                spawner->runtime.slots[job.req.slot].tracked = false;
        }
    }

} // namespace Dark
