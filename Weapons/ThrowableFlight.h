#pragma once

#include "Collision/HitResult.h"
#include "ECS/Entity.h"
#include "Math/AABox3f.h"
#include "Math/Ray3f.h"
#include "Math/Sphere3f.h"
#include "Math/Vector3f.h"

#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    class ThrowableBelt;
    class WeaponLoadout;
    class World;

    // Shared base gravity. Charged speed is solved from this and the authored distance.
    // Weight multiplies gravity after that solve, so it is not part of the speed.
    inline constexpr float kThrowableBaseGravity   = 18.0f;
    inline constexpr float kThrowableTapSpeedScale = 0.55f;
    inline constexpr float kThrowableRadius        = 0.12f;
    inline constexpr float kThrowableMaxLife       = 12.0f;

    float throwableChargedSpeed(float throwDistance);
    float throwableGravity(float weight);
    // Pitch above the horizon that lands on flat ground at throwDistance. False when weight keeps it short of that distance.
    bool throwableChargedPitch(float throwDistance, float weight, bool higherAim, float& outPitchRad);

    struct ThrowableBody
    {
        Entity         entity{};
        Math::Vector3f center{};
        Math::Vector3f halfExtents{ 0.5f, 0.5f, 0.5f };
        bool           alive = true;
    };

    // Terrain, character bodies, and the solids that already stop the player.
    // Sandbox passes path cubes and no spheres. Editor passes play cubes and play spheres.
    struct ThrowableWorld
    {
        Collision::RayHit3D (*raycastTerrain)(void* user, const Math::Ray3f& ray, float maxDistance) = nullptr;
        float (*heightAt)(void* user, float x, float z)                                              = nullptr;
        void* terrainUser                                                                            = nullptr;
        bool  hasTerrain                                                                             = false;

        const ThrowableBody* bodies    = nullptr;
        int                  bodyCount = 0;

        const Math::AABox3f*  cubes      = nullptr;
        int                   cubeCount  = 0;
        const Math::Sphere3f* spheres    = nullptr;
        int                   sphereCount = 0;
    };

    struct ThrowableLaunch
    {
        std::string    itemId;
        std::string    impactClip;
        float          weight        = 1.0f;
        float          throwDistance = 1.0f;
        Math::Vector3f origin{};
        Math::Vector3f direction{ 0.0f, 0.0f, 1.0f };
        bool           charged = true;
        Entity         thrower{};
        float          maxLife = 0.0f; // <= 0 uses kThrowableMaxLife
    };

    struct ThrowableLanding
    {
        std::string    itemId;
        std::string    impactClip;
        Math::Vector3f position{};
        Math::Vector3f normal{ 0.0f, 1.0f, 0.0f };
        Entity         hitEntity{};
        bool           hitBody           = false;
        bool           hitSolid          = false;
        bool           hitTerrain        = false;
        bool           expired           = false;
        float          damage            = 0.0f;
        bool           emittedWeaponHit  = false;
    };

    class ThrowableFlights
    {
    public:
        bool launch(const ThrowableLaunch& launch);
        void tick(float dt, const ThrowableWorld& world);

        int  liveCount() const;
        bool livePosition(int index, Math::Vector3f& out) const;

        const std::vector<ThrowableLanding>& landings() const;
        void                                 clearLandings();
        void                                 clear();

    private:
        struct Live
        {
            std::string    itemId;
            std::string    impactClip;
            Math::Vector3f position{};
            Math::Vector3f velocity{};
            float          gravity = 0.0f;
            float          age     = 0.0f;
            float          maxLife = 0.0f;
            Entity         thrower{};
            bool           clearOfThrower = false;
        };

        bool stepLive(Live& flight, float dt, const ThrowableWorld& world);

        std::vector<Live>              m_live;
        std::vector<ThrowableLanding>  m_landings;
    };

    // Spends one, then starts the sweep. False leaves the belt and the flights unchanged.
    bool beginThrowableThrow(ThrowableFlights& flights, WeaponLoadout& loadout, ThrowableBelt& belt, const ThrowableLaunch& launch);

    // Pickup at a fixed world position. No parent and no physics body. Hosts that can draw call spawnItemPickup instead.
    Entity placeThrowablePickup(World& world, std::string_view id, const Math::Vector3f& position);

} // namespace Dark
