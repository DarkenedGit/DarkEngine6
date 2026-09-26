#pragma once

#include "AI/Brain.h"
#include "AI/Pathfinder.h"
#include "Math/Vector3f.h"

#include <memory>

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
        static constexpr const char* kTypeName = "AiAgent";
        Math::Vector3f               forward{ 0.0f, 0.0f, 1.0f };
        Math::Vector3f               planarVelocity{};
        Math::Vector3f               lastSeen{};
        Math::Vector3f               wanderDest{};
        Math::Vector3f               helpPos{};
        float                        deadFor     = 0.0f;
        float                        assistLeft  = 0.0f;
        float                        fleeLeft    = 0.0f;
        bool                         givenUp     = false;
        bool                         hasLastSeen = false;
    };

    struct BrainComponent
    {
        static constexpr const char* kTypeName = "Brain";
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
            float sprintSpeed      = 17.0f;
            float walkSpeed        = 9.0f;
        };
    } // namespace AI

} // namespace Dark
