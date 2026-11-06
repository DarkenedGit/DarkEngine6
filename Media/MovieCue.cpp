#include "Media/MovieCue.h"

#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Media/MoviePlayer.h"
#include "Scene/SceneTypes.h"

#include <utility>

namespace Dark
{

    namespace
    {

        MovieStatus (*g_movieCueOpenForTest)(MoviePlayer& player, const char* path) = nullptr;

        const char* movieCueStatusText(MovieStatus status)
        {
            switch (status)
            {
            case MovieStatus::Ok:                    return "ok";
            case MovieStatus::FileMissing:           return "file missing";
            case MovieStatus::UnsupportedContainer:  return "unsupported container";
            case MovieStatus::UnsupportedCodec:      return "unsupported codec";
            case MovieStatus::ResolutionUnsupported: return "resolution unsupported";
            case MovieStatus::DecodeError:           return "decode error";
            case MovieStatus::Busy:                  return "busy";
            case MovieStatus::DeviceLost:            return "device lost";
            default:                                 return "unknown";
            }
        }

        bool movieCueOverlapsXZ(const Math::Vector3f& cue, const Math::Vector3f& pawn, float radius)
        {
            const float dx = cue.x - pawn.x;
            const float dz = cue.z - pawn.z;
            return dx * dx + dz * dz <= radius * radius;
        }

        MovieStatus openMovieCue(MoviePlayer& player, const char* path)
        {
            if (g_movieCueOpenForTest)
                return g_movieCueOpenForTest(player, path);
            return player.open(path);
        }

    } // namespace

    void setMovieCueOpenForTest(MovieStatus (*fn)(MoviePlayer& player, const char* path))
    {
        g_movieCueOpenForTest = fn;
    }

    void applyMovieCue(World& world, Entity e, const SceneObjectData& data)
    {
        MovieCueComponent cue;
        cue.path = data.movie;
        cue.radius = data.movieRadius;
        if (!(cue.radius >= 0.0f))
            cue.radius = 0.0f;
        cue.loop  = data.movieLoop;
        cue.fired = false;
        world.emplace<MovieCueComponent>(e, std::move(cue));
        if (data.movie.empty())
            DE_LOG_WARN(LogCategory::Media, "Movie cue: empty path");
    }

    void resetMovieCues(World& world)
    {
        world.each<MovieCueComponent>([](Entity, MovieCueComponent& cue) { cue.fired = false; });
    }

    void tickMovieCues(World& world, MoviePlayer& player, Entity possessed)
    {
        const TransformComponent* pawn = possessed.valid() ? world.get<TransformComponent>(possessed) : nullptr;
        if (!pawn)
            return;

        const Math::Vector3f pawnPos = pawn->position;
        world.each<MovieCueComponent>([&](Entity e, MovieCueComponent& cue) {
            if (cue.fired || cue.path.empty() || !(cue.radius > 0.0f))
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf || !movieCueOverlapsXZ(xf->position, pawnPos, cue.radius))
                return;

            // Opening and a fade still hold the one player. Leave fired clear so a later frame can retry.
            const MoviePlayState state = player.state();
            if (player.blocksWorld() || state == MoviePlayState::Opening)
                return;
            if (state == MoviePlayState::Ready || state == MoviePlayState::Failed || state == MoviePlayState::Ended)
                player.close();
            else if (state != MoviePlayState::Closed)
                return;

            const MovieStatus status = openMovieCue(player, cue.path.c_str());
            if (status == MovieStatus::Busy)
                return;
            if (status != MovieStatus::Ok)
            {
                cue.fired = true;
                DE_LOG_WARN(LogCategory::Media, "Movie cue: open failed ({}) '{}'", movieCueStatusText(status), cue.path);
                return;
            }
            player.setLoop(cue.loop);
            player.play();
            cue.fired = true;
        });
    }

} // namespace Dark
