#include <gtest/gtest.h>

#include "AI/Exposure.h"
#include "Audio/AudioSystem.h"
#include "Collision/Occlusion.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/AABox3f.h"
#include "Play/AttachedLights.h"

using namespace Dark;
using namespace Dark::Math;

namespace
{
    // A 1 m thick wall across x = 4..5, 10 m wide and 4 m tall.
    const AABox3f kWall{ Vector3f{ 4.0f, 0.0f, -5.0f }, Vector3f{ 5.0f, 4.0f, 5.0f } };
}

TEST(Occlusion, WallBetweenBlocksAndOpenGroundDoesNot)
{
    EXPECT_TRUE(Collision::segmentBlocked(&kWall, 1, Vector3f{ 0.0f, 1.5f, 0.0f }, Vector3f{ 9.0f, 1.5f, 0.0f }));
    EXPECT_FALSE(Collision::segmentBlocked(&kWall, 1, Vector3f{ 0.0f, 1.5f, 0.0f }, Vector3f{ 0.0f, 1.5f, 9.0f }));
    EXPECT_FALSE(Collision::segmentBlocked(nullptr, 0, Vector3f{ 0.0f, 1.5f, 0.0f }, Vector3f{ 9.0f, 1.5f, 0.0f }));
}

TEST(Occlusion, SourceTouchingTheWallStillCounts)
{
    EXPECT_FALSE(Collision::segmentBlocked(&kWall, 1, Vector3f{ 0.0f, 1.5f, 0.0f }, Vector3f{ 4.1f, 1.5f, 0.0f }));
}

TEST(Occlusion, SoundBehindWallIsFullAndAtTheEdgeIsPartial)
{
    const Vector3f ear{ 0.0f, 1.6f, 0.0f };
    EXPECT_FLOAT_EQ(Collision::soundOcclusion(&kWall, 1, ear, Vector3f{ 9.0f, 1.6f, 0.0f }), 1.0f);
    EXPECT_FLOAT_EQ(Collision::soundOcclusion(&kWall, 1, ear, Vector3f{ 0.0f, 1.6f, 9.0f }), 0.0f);
    const float edge = Collision::soundOcclusion(&kWall, 1, ear, Vector3f{ 9.0f, 1.6f, 11.0f });
    EXPECT_GT(edge, 0.0f);
    EXPECT_LT(edge, 1.0f);
}

TEST(Occlusion, LampBehindWallDoesNotLightThePlayer)
{
    World          world;
    const Vector3f chest{ 0.0f, 0.45f, 0.0f };
    Entity         lamp = world.createEntity();
    TransformComponent xf{};
    xf.position = Vector3f{ 6.0f, 0.45f, 0.0f };
    world.emplace<TransformComponent>(lamp, xf);
    LocalLightComponent light{};
    light.type    = LocalLightType::Point;
    light.range   = 10.0f;
    light.enabled = true;
    world.emplace<LocalLightComponent>(lamp, light);

    const Vector3f up{ 0.0f, 1.0f, 0.0f };
    EXPECT_GT(AI::sampleLightLevel(world, chest, -1.0f, up, nullptr, 0, {}), 0.12f);
    EXPECT_NEAR(AI::sampleLightLevel(world, chest, -1.0f, up, &kWall, 1, {}), 0.12f, 1.0e-4f);
}

TEST(Occlusion, BlockedVoicesAreQuieterAndMuffled)
{
    EXPECT_FLOAT_EQ(Audio::occludedGain(0.0f), 1.0f);
    EXPECT_LT(Audio::occludedGain(1.0f), 0.5f);
    EXPECT_GT(Audio::occludedGain(1.0f), 0.0f);
    EXPECT_FLOAT_EQ(Audio::occludedFilterFrequency(0.0f, 22050.0f), 1.0f);
    EXPECT_LT(Audio::occludedFilterFrequency(1.0f, 22050.0f), 0.5f);
    EXPECT_LT(Audio::occludedFilterFrequency(1.0f, 22050.0f), Audio::occludedFilterFrequency(0.5f, 22050.0f));
}

TEST(Occlusion, OcclusionFadesInsteadOfJumping)
{
    const float half = Audio::smoothOcclusion(0.0f, 1.0f, 0.1f);
    EXPECT_GT(half, 0.0f);
    EXPECT_LT(half, 1.0f);
    EXPECT_FLOAT_EQ(Audio::smoothOcclusion(0.0f, 1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(Audio::smoothOcclusion(1.0f, 0.0f, 1.0f), 0.0f);
}

namespace
{
    float wallQuery(void*, const Vector3f& listener, const Vector3f& source)
    {
        return Collision::soundOcclusion(&kWall, 1, listener, source);
    }

    float overQuery(void*, const Vector3f&, const Vector3f&)
    {
        return 4.0f;
    }
}

TEST(Occlusion, NewVoiceStartsOccludedWhenAQueryIsSet)
{
    const Vector3f ear{ 0.0f, 1.6f, 0.0f };
    const Vector3f behind{ 9.0f, 1.6f, 0.0f };
    EXPECT_FLOAT_EQ(Audio::startingOcclusion(true, wallQuery, nullptr, ear, behind), 1.0f);
    EXPECT_FLOAT_EQ(Audio::startingOcclusion(true, wallQuery, nullptr, ear, Vector3f{ 0.0f, 1.6f, 9.0f }), 0.0f);
    EXPECT_FLOAT_EQ(Audio::startingOcclusion(false, wallQuery, nullptr, ear, behind), 0.0f);
    EXPECT_FLOAT_EQ(Audio::startingOcclusion(true, nullptr, nullptr, ear, behind), 0.0f);
    EXPECT_FLOAT_EQ(Audio::startingOcclusion(true, overQuery, nullptr, ear, behind), 1.0f);
}

TEST(Occlusion, GuardTorchCastsShadowsAndIsCleanedUp)
{
    World  world;
    Entity guard = world.createEntity();
    Entity torch = spawnGuardTorch(world, guard, Vector3f{ 1.0f, 0.0f, 2.0f });
    const LocalLightComponent*    beam = world.get<LocalLightComponent>(torch);
    const AttachedLightComponent* att  = world.get<AttachedLightComponent>(torch);
    ASSERT_NE(beam, nullptr);
    ASSERT_NE(att, nullptr);
    EXPECT_TRUE(beam->castShadow);
    EXPECT_EQ(beam->type, LocalLightType::Spot);
    EXPECT_EQ(att->owner, guard);
    destroyGuardTorches(world);
    EXPECT_FALSE(world.alive(torch));
    EXPECT_TRUE(world.alive(guard));
}
