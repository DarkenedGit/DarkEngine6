#pragma once

#include "Math/AABox3f.h"

#include <string>

namespace Dark
{

    // A reach volume. `inside` is runtime only and is not saved.
    struct QuestZoneComponent
    {
        static constexpr const char* kTypeName = "QuestZone";

        std::string   zone;
        Math::AABox3f box{};
        bool          inside = false;
    };

    struct GoalVolumeComponent
    {
        static constexpr const char* kTypeName = "GoalVolume";

        Math::AABox3f box{};
    };

} // namespace Dark
