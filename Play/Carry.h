#pragma once

#include "ECS/Entity.h"
#include "ECS/Persist.h"
#include "Math/AABox3f.h"
#include "Math/Vector3f.h"

#include <vector>

namespace Dark
{

    class World;

    struct CarriedByComponent
    {
        static constexpr const char* kTypeName    = "CarriedBy";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        Entity carrier{};
        float  floor = 0.0f;
    };

    struct ConcealVolumeComponent
    {
        static constexpr const char* kTypeName    = "ConcealVolume";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        Math::AABox3f box{};
    };

    struct CarryView
    {
        World*                world   = nullptr;
        Entity                carrier{};
        Math::Vector3f        look{ 0.0f, 0.0f, 1.0f };
        float (*heightAt)(void* user, float x, float z) = nullptr;
        void*                 heightUser = nullptr;
        const Math::AABox3f*  conceal    = nullptr;
        int                   concealCount = 0;
        void (*onHidden)(void* user) = nullptr;
        void*                 user = nullptr;
    };

    Entity carriedBody(World& world);
    void   gatherConcealVolumes(World& world, std::vector<Math::AABox3f>& out);
    // A loaded carry mark has to set the agent flag. The save binder cannot see the agent.
    void   syncCarriedAgents(World& world);
    void   dropCarriedBody(CarryView& view);
    bool   takeCarriedBody(World& world, Entity carrier, Entity body, float (*heightAt)(void* user, float x, float z), void* heightUser);
    void   tickCarriedBody(CarryView& view);

} // namespace Dark
