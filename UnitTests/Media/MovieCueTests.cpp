#include <gtest/gtest.h>

#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Media/MovieCue.h"
#include "Media/MoviePlayer.h"
#include "Scene/SceneTypes.h"

#include <string>
#include <vector>

using namespace Dark;

namespace
{

    MovieStatus g_status = MovieStatus::FileMissing;
    MovieStatus g_queue[4]{};
    int         g_queueCount = 0;
    int         g_queueIndex = 0;
    int         g_opens      = 0;

    std::vector<std::string> g_logs;
    std::vector<LogCategory> g_cats;
    std::vector<LogLevel>    g_levels;

    void captureLog(LogLevel level, LogCategory category, const char* message)
    {
        g_levels.push_back(level);
        g_cats.push_back(category);
        g_logs.emplace_back(message ? message : "");
    }

    MovieStatus testOpen(MoviePlayer&, const char*)
    {
        ++g_opens;
        if (g_queueCount > 0)
        {
            const int i = g_queueIndex < g_queueCount ? g_queueIndex : g_queueCount - 1;
            ++g_queueIndex;
            return g_queue[i];
        }
        return g_status;
    }

    struct OpenHook
    {
        OpenHook()
        {
            g_opens      = 0;
            g_queueCount = 0;
            g_queueIndex = 0;
            g_status     = MovieStatus::FileMissing;
            setMovieCueOpenForTest(&testOpen);
        }

        ~OpenHook()
        {
            setMovieCueOpenForTest(nullptr);
        }
    };

    struct LogCapture
    {
        LogCapture()
        {
            g_logs.clear();
            g_cats.clear();
            g_levels.clear();
            Log::resetFilters();
            Log::setCapture(&captureLog);
        }

        ~LogCapture()
        {
            Log::setCapture(nullptr);
        }
    };

    Entity makePawn(World& world, Math::Vector3f pos)
    {
        const Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = pos;
        world.emplace<TransformComponent>(e, xf);
        return e;
    }

    Entity makeCue(World& world, Math::Vector3f pos, const char* path, float radius, bool loop = false)
    {
        const Entity e = world.createEntity();
        TransformComponent xf{};
        xf.position = pos;
        world.emplace<TransformComponent>(e, xf);
        SceneObjectData data;
        data.type        = SceneObjectType::Movie;
        data.movie       = path ? path : "";
        data.movieRadius = radius;
        data.movieLoop   = loop;
        applyMovieCue(world, e, data);
        return e;
    }

    bool loggedText(const char* needle)
    {
        for (const std::string& line : g_logs)
        {
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

} // namespace

TEST(MovieCue, XZOverlapIgnoresY)
{
    OpenHook hook;
    g_status = MovieStatus::FileMissing;
    World world;
    MoviePlayer player;
    const Entity cue  = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "movies/a.mp4", 2.0f);
    const Entity pawn = makePawn(world, Math::Vector3f{ 2.0f, 80.0f, 0.0f });

    tickMovieCues(world, player, pawn);
    EXPECT_TRUE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 1);

    resetMovieCues(world);
    EXPECT_FALSE(world.get<MovieCueComponent>(cue)->fired);
    world.get<TransformComponent>(pawn)->position = Math::Vector3f{ 2.01f, 0.0f, 0.0f };
    g_opens = 0;
    tickMovieCues(world, player, pawn);
    EXPECT_FALSE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 0);

    world.get<TransformComponent>(pawn)->position = Math::Vector3f{ 0.0f, -40.0f, 0.0f };
    tickMovieCues(world, player, pawn);
    EXPECT_TRUE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 1);
}

TEST(MovieCue, RadiusZeroAndEmptyPathDoNotFire)
{
    OpenHook hook;
    World world;
    MoviePlayer player;
    const Entity zero  = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "movies/a.mp4", 0.0f);
    const Entity empty = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "", 2.0f);
    const Entity pawn  = makePawn(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    g_opens = 0;
    tickMovieCues(world, player, pawn);
    EXPECT_FALSE(world.get<MovieCueComponent>(zero)->fired);
    EXPECT_FALSE(world.get<MovieCueComponent>(empty)->fired);
    EXPECT_FLOAT_EQ(world.get<MovieCueComponent>(empty)->radius, 2.0f);
    EXPECT_EQ(g_opens, 0);
}

TEST(MovieCue, BusyLeavesFiredFalse)
{
    OpenHook hook;
    LogCapture logs;
    g_status = MovieStatus::Busy;
    World world;
    MoviePlayer player;
    const Entity cue  = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "movies/a.mp4", 2.0f);
    const Entity pawn = makePawn(world, Math::Vector3f{ 0.0f, 9.0f, 0.0f });
    g_logs.clear();
    tickMovieCues(world, player, pawn);
    EXPECT_FALSE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 1);
    EXPECT_FALSE(loggedText("open failed"));
    EXPECT_FALSE(loggedText("busy"));

    tickMovieCues(world, player, pawn);
    EXPECT_FALSE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 2);
}

TEST(MovieCue, FileMissingSetsFired)
{
    OpenHook hook;
    LogCapture logs;
    g_status = MovieStatus::FileMissing;
    World world;
    MoviePlayer player;
    const Entity cue  = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "movies/missing.mp4", 2.0f);
    const Entity pawn = makePawn(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    g_logs.clear();
    tickMovieCues(world, player, pawn);
    EXPECT_TRUE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 1);
    ASSERT_FALSE(g_logs.empty());
    EXPECT_EQ(g_cats.back(), LogCategory::Media);
    EXPECT_EQ(g_levels.back(), LogLevel::Warn);
    EXPECT_TRUE(loggedText("file missing"));
    EXPECT_TRUE(loggedText("movies/missing.mp4"));

    g_opens = 0;
    g_logs.clear();
    tickMovieCues(world, player, pawn);
    EXPECT_EQ(g_opens, 0);
    EXPECT_TRUE(g_logs.empty());
}

TEST(MovieCue, SecondCueStaysUnfiredWhenBusy)
{
    OpenHook hook;
    g_queue[0]   = MovieStatus::Ok;
    g_queue[1]   = MovieStatus::Busy;
    g_queueCount = 2;
    World world;
    MoviePlayer player;
    const Entity first  = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "movies/first.mp4", 2.0f, true);
    const Entity second = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "movies/second.mp4", 2.0f);
    const Entity pawn   = makePawn(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    tickMovieCues(world, player, pawn);
    EXPECT_TRUE(world.get<MovieCueComponent>(first)->fired);
    EXPECT_TRUE(world.get<MovieCueComponent>(first)->loop);
    EXPECT_FALSE(world.get<MovieCueComponent>(second)->fired);
    EXPECT_EQ(g_opens, 2);

    g_opens      = 0;
    g_queueIndex = 0;
    g_status     = MovieStatus::Ok;
    g_queueCount = 0;
    tickMovieCues(world, player, pawn);
    EXPECT_EQ(g_opens, 1);
    EXPECT_TRUE(world.get<MovieCueComponent>(second)->fired);
}

TEST(MovieCue, NoPossessedOrTransformSkips)
{
    OpenHook hook;
    g_status = MovieStatus::FileMissing;
    World world;
    MoviePlayer player;
    const Entity cue = makeCue(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f }, "movies/a.mp4", 2.0f);
    tickMovieCues(world, player, Entity{});
    EXPECT_FALSE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 0);

    const Entity bare = world.createEntity();
    SceneObjectData data;
    data.movie       = "movies/a.mp4";
    data.movieRadius = 2.0f;
    applyMovieCue(world, bare, data);
    const Entity pawn = makePawn(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    tickMovieCues(world, player, pawn);
    EXPECT_FALSE(world.get<MovieCueComponent>(bare)->fired);
    EXPECT_TRUE(world.get<MovieCueComponent>(cue)->fired);
    EXPECT_EQ(g_opens, 1);
}

TEST(MovieCue, NegativeRadiusClampsAndDoesNotFire)
{
    OpenHook hook;
    World world;
    MoviePlayer player;
    SceneObjectData data;
    data.movie       = "movies/a.mp4";
    data.movieRadius = -4.0f;
    data.movieLoop   = true;
    const Entity e = world.createEntity();
    TransformComponent xf{};
    world.emplace<TransformComponent>(e, xf);
    applyMovieCue(world, e, data);
    const MovieCueComponent* cue = world.get<MovieCueComponent>(e);
    ASSERT_NE(cue, nullptr);
    EXPECT_FLOAT_EQ(cue->radius, 0.0f);
    EXPECT_TRUE(cue->loop);
    EXPECT_FALSE(cue->fired);
    const Entity pawn = makePawn(world, Math::Vector3f{ 0.0f, 0.0f, 0.0f });
    tickMovieCues(world, player, pawn);
    EXPECT_FALSE(world.get<MovieCueComponent>(e)->fired);
    EXPECT_EQ(g_opens, 0);
}

TEST(MovieCue, EmptyPathWarnsOnce)
{
    LogCapture logs;
    World world;
    const Entity e = world.createEntity();
    SceneObjectData data;
    data.movieRadius = 2.0f;
    applyMovieCue(world, e, data);
    applyMovieCue(world, e, data);
    int warns = 0;
    for (size_t i = 0; i < g_logs.size(); ++i)
    {
        if (g_levels[i] == LogLevel::Warn && g_cats[i] == LogCategory::Media && g_logs[i].find("empty path") != std::string::npos)
            ++warns;
    }
    EXPECT_EQ(warns, 2);
    EXPECT_TRUE(world.get<MovieCueComponent>(e)->path.empty());
}

TEST(MovieCue, HardStatusWithoutDeviceSetsFired)
{
    setMovieCueOpenForTest(nullptr);
    World world;
    MoviePlayer player;
    const Entity cue  = makeCue(world, Math::Vector3f{ 0.0f, 3.0f, 0.0f }, "movies/missing.mp4", 2.0f);
    const Entity pawn = makePawn(world, Math::Vector3f{ 2.0f, -12.0f, 0.0f });
    tickMovieCues(world, player, pawn);
    EXPECT_TRUE(world.get<MovieCueComponent>(cue)->fired);

    resetMovieCues(world);
    world.get<TransformComponent>(pawn)->position = Math::Vector3f{ 2.01f, -12.0f, 0.0f };
    tickMovieCues(world, player, pawn);
    EXPECT_FALSE(world.get<MovieCueComponent>(cue)->fired);
}
