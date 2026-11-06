#pragma once

#include "ECS/Entity.h"
#include "Media/MovieStatus.h"

#include <string>

namespace Dark
{

    class World;
    class MoviePlayer;
    struct SceneObjectData;

    // Live cue. The player is the one MoviePlayer on the exe. fired is not saved.
    struct MovieCueComponent
    {
        static constexpr const char* kTypeName = "MovieCue";

        std::string path;
        float       radius = 2.0f;
        bool        loop   = false;
        bool        fired  = false;
    };

    void applyMovieCue(World& world, Entity e, const SceneObjectData& data);
    void tickMovieCues(World& world, MoviePlayer& player, Entity possessed);
    void resetMovieCues(World& world);

    // Tests pass a stand-in so a hard status or Busy does not need a device.
    void setMovieCueOpenForTest(MovieStatus (*fn)(MoviePlayer& player, const char* path));

} // namespace Dark
