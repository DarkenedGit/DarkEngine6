#pragma once

#include "AI/Awareness.h"
#include "AI/Brain.h"
#include "ECS/Persist.h"
#include "AI/Pathfinder.h"
#include "AI/WolfApproach.h"
#include "Math/Vector3f.h"

#include <memory>
#include <vector>

namespace Dark
{

    struct SightComponent
    {
        static constexpr const char* kTypeName = "Sight";
        float                        coneDeg = 70.0f;
        float                        range   = 25.0f;
    };

    struct PathAgentComponent
    {
        static constexpr const char* kTypeName = "PathAgent";
        AI::PathResult               path;
        int                          waypoint = 0;
        float                        repathAt = 0.0f;
        float                        radius   = 0.8f;
    };

    struct AiAgentComponent
    {
        static constexpr const char* kTypeName    = "AiAgent";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;
        Math::Vector3f               forward{ Math::Vect3f::Z_AXIS };
        Math::Vector3f               planarVelocity{};
        Math::Vector3f               lastSeen{};
        Math::Vector3f               wanderDest{};
        Math::Vector3f               helpPos{};
        float                        deadFor     = 0.0f;
        float                        assistLeft  = 0.0f;
        float                        fleeLeft    = 0.0f;
        float                        footstepAcc = 0.0f;
        float                        attackGap   = 0.0f;
        float                        attackPause = 0.0f;
        float                        repositionLeft = 0.0f;
        float                        meleeWindup = 0.0f;
        int                          attackChain = 0;
        int                          lastAttack  = -1;
        int                          meleeAttack = -1;
        bool                         preferOtherAttack = false;
        bool                         givenUp     = false;
        Math::Vector3f               repositionDest{};
        bool                         hasLastSeen = false;
        // Set on a wolf's notice and kept through Memory so a brief lose does not re-roll the side.
        AI::WolfStalk                stalk       = AI::WolfStalk::Inactive;
        AI::WolfApproachSide         stalkSide   = AI::WolfApproachSide::Behind;
        Math::Vector3f               stalkDest{};
        float                        stalkLeft   = 0.0f;
        float                        circleLeft  = 0.0f;
        float                        circleDir   = 1.0f;
        bool                         inDuel      = false;
        float                        guardLeft   = 0.0f;
        float                        hitWindow   = 0.0f;
        int                          hitsTaken   = 0;
        AI::AwarenessState           aware{};
        float                        lookLeft    = 0.0f;
        Math::Vector3f               searchDest{};
        float                        stuckFor    = 0.0f;
        float                        reactLeft   = 0.0f;
        float                        boostLeft   = 0.0f;
        float                        holdYaw     = 0.0f;
        float                        glanceLeft  = 0.0f;
        bool                         reacting    = false;
        bool                         bodyFound   = false;
        bool                         carried     = false;
        bool                         passive     = false;
        float                        lostFor     = 0.0f;
        uint32_t                     squad       = 0;
        Math::Vector3f               lastAlivePos{};
        bool                         missingReported = false;
        bool                         reportMissing   = false;
        uint32_t                     checkedHides    = 0; // 0 = no squad. Squad mates share alerts at any distance.
        // This alert already radioed its first sighting. Cleared when the guard leaves Alert. Not saved.
        bool                         spotCalled  = false;
        // False until a movement file or the dev-tools speed slider writes walk and run.
        // Tick keeps using PackSettings while this is false.
        bool                         authoredMove = false;
        float                        walkSpeed     = 0.0f;
        float                        runSpeed      = 0.0f;
        float                        acceleration  = 0.0f; // m/s^2 toward walk and run. 0 snaps.
        // Throwable search. Not saved. lurePoint is a copied world position.
        bool                         lureSearch  = false;
        bool                         lureReached = false;
        float                        lureLeft    = 0.0f;
        uint32_t                     lureSalt    = 1;
        Math::Vector3f               lurePoint{};
        Math::Vector3f               lureDest{};
    };

    struct PatrolComponent
    {
        static constexpr const char* kTypeName = "Patrol";
        std::vector<Math::Vector3f>  points;
        int                          next     = 0;
        float                        waitLeft = 0.0f;
    };

    struct BrainComponent
    {
        static constexpr const char* kTypeName    = "Brain";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;
        std::unique_ptr<AI::Brain>   brain;
    };

    namespace AI
    {
        struct PackSettings
        {
            float assistAllyRadius = 6.0f;
            float assistSeconds    = 5.0f;
            float fleeSeconds      = 4.0f;
            float alertRange       = 18.0f;
            float sprintSpeed      = 6.8f;
            float walkSpeed        = 3.4f;
            float acceleration     = 0.0f; // m/s^2. 0 snaps. Authored agents use the movement file instead.
        };
    } // namespace AI

} // namespace Dark
