#pragma once

#include "ECS/Entity.h"

#include <cstdint>
#include <string_view>

namespace Dark
{

    class World;

    // Authored spawner. Saved on the level object. Runtime slots are not saved.
    enum class SpawnerMode : uint8_t
    {
        Maintain = 0,
        Waves
    };

    enum class SpawnerUnit : uint8_t
    {
        Hunter = 0,
        Wolf,
        Crow
    };

    // Waves only. Time starts the next wave on a timer and leaves earlier units in the level.
    // Cleared starts the next wave once every unit from the current wave is dead.
    enum class SpawnerWaveGate : uint8_t
    {
        Time = 0,
        Cleared
    };

    inline constexpr int kSpawnerSlotCount = 16;

    struct SpawnerDesc
    {
        SpawnerMode     mode         = SpawnerMode::Maintain;
        SpawnerUnit     unit         = SpawnerUnit::Hunter;
        SpawnerWaveGate gate         = SpawnerWaveGate::Cleared;
        int             count        = 3;
        float           radius       = 6.0f;
        float           respawnDelay = 1.5f;
        float           waveInterval = 8.0f;
        int             maxWaves     = 0; // 0 = no limit
        bool            enabled      = true;
        uint32_t        seed         = 1;
    };

    struct SpawnerSlot
    {
        Entity occupant{};
        float  cooldown = 0.0f;
        bool   tracked  = false;
    };

    // Session state. Reset when play stops. Occupant ids are not written to the level.
    struct SpawnerRuntime
    {
        SpawnerSlot slots[kSpawnerSlotCount]{};
        uint32_t    rng           = 1;
        bool        seeded        = false;
        bool        filling       = false;
        int         wavesSpawned  = 0;
        float       waveTime      = 0.0f;
    };

    struct SpawnerRequest
    {
        SpawnerUnit unit = SpawnerUnit::Hunter;
        int         slot = 0;
        float       x    = 0.0f;
        float       z    = 0.0f;
        uint32_t    seed = 1;
    };

    // Marker entity. The spawned units are separate and must not be editor objects.
    struct SpawnerComponent
    {
        static constexpr const char* kTypeName = "Spawner";

        SpawnerDesc    desc{};
        SpawnerRuntime runtime{};
    };

    // Stamped on a unit this spawner created. Stop Play destroys these and does not save them.
    struct SpawnedUnitComponent
    {
        static constexpr const char* kTypeName = "SpawnedUnit";

        Entity spawner{};
        int    slot = -1;
    };

    const char* toString(SpawnerMode mode);
    const char* toString(SpawnerUnit unit);
    const char* toString(SpawnerWaveGate gate);
    bool        tryParseSpawnerMode(std::string_view text, SpawnerMode& out);
    bool        tryParseSpawnerUnit(std::string_view text, SpawnerUnit& out);
    bool        tryParseSpawnerWaveGate(std::string_view text, SpawnerWaveGate& out);

    void clampSpawnerDesc(SpawnerDesc& desc);
    void resetSpawnerRuntime(SpawnerRuntime& runtime);

    // True when the slot's unit is still in the world and its health is above zero.
    // A missing body is dead. A body with no health component is treated as alive.
    bool spawnerOccupantAlive(World& world, Entity occupant);

    // Writes spawn requests for this tick. The host creates the unit, then stores it in
    // slots[request.slot].occupant before the next tick. alive[i] reports that occupant.
    // A maintain death waits respawnDelay on later ticks; the tick that notices the death
    // does not consume the delay. Radius is an XZ disc around originX/originZ.
    // Returns the number of requests written.
    int tickSpawner(const SpawnerDesc& desc, SpawnerRuntime& runtime, float originX, float originZ, float dt, const bool alive[kSpawnerSlotCount], SpawnerRequest* out, int outCapacity);

    // Host creates the unit. A null entity clears that slot's tracked flag so the next tick can try again.
    using SpawnerSpawnFn = Entity (*)(void* user, Entity spawner, const SpawnerRequest& req);
    void tickSpawnerJobs(World& world, float dt, SpawnerSpawnFn spawn, void* user);

} // namespace Dark
