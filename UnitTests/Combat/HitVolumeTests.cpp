#include <gtest/gtest.h>

#include "AI/AiSystem.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/AnimGraphTick.h"
#include "Animation/AnimSampler.h"
#include "Animation/AnimationClip.h"
#include "Animation/AnimationSet.h"
#include "Animation/Skeleton.h"
#include "Assets/AssetManager.h"
#include "Assets/Model.h"
#include "Audio/AudioSystem.h"
#include "Combat/HitVolumes.h"
#include "Core/AssetPinTable.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Play/CharacterAttach.h"
#include "Terrain/TerrainGround.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Dark;
using namespace Dark::Combat;
using namespace Dark::Math;

namespace
{

    struct JointPlace
    {
        const char* name;
        Vector3f    at;
    };

    const JointPlace kHumanJoints[] = {
        { "Hips", { 0.0f, 1.00f, 0.0f } },
        { "Spine", { 0.0f, 1.20f, 0.0f } },
        { "Chest", { 0.0f, 1.40f, 0.0f } },
        { "Neck", { 0.0f, 1.60f, 0.0f } },
        { "Head", { 0.0f, 1.75f, 0.10f } },
        { "L_UpperArm", { -0.20f, 1.40f, 0.0f } },
        { "L_LowerArm", { -0.45f, 1.15f, 0.0f } },
        { "L_Hand", { -0.70f, 0.95f, 0.05f } },
        { "R_UpperArm", { 0.20f, 1.40f, 0.0f } },
        { "R_LowerArm", { 0.45f, 1.15f, 0.0f } },
        { "R_Hand", { 0.70f, 0.95f, 0.05f } },
        { "L_UpperLeg", { -0.12f, 0.95f, 0.0f } },
        { "L_LowerLeg", { -0.12f, 0.50f, 0.0f } },
        { "L_Foot", { -0.12f, 0.05f, 0.08f } },
        { "R_UpperLeg", { 0.12f, 0.95f, 0.0f } },
        { "R_LowerLeg", { 0.12f, 0.50f, 0.0f } },
        { "R_Foot", { 0.12f, 0.05f, 0.08f } },
    };

    void addJoint(Skeleton& skeleton, const char* name, const Vector3f& at)
    {
        Joint joint;
        joint.name   = name;
        joint.parent = -1;
        joint.restT  = at;
        joint.restR  = Quaternion::IDENTITY;
        joint.restS  = Vector3f{ 1.0f, 1.0f, 1.0f };
        const uint32_t index = static_cast<uint32_t>(skeleton.joints.size());
        skeleton.joints.push_back(std::move(joint));
        skeleton.fkOrder.push_back(index);
    }

    Skeleton humanoidSkeleton(const char* skip)
    {
        Skeleton skeleton;
        for (const JointPlace& place : kHumanJoints)
        {
            if (skip && std::strcmp(place.name, skip) == 0)
                continue;
            addJoint(skeleton, place.name, place.at);
        }
        return skeleton;
    }

    int jointIndex(const Skeleton& skeleton, const char* name)
    {
        for (size_t i = 0; i < skeleton.joints.size(); ++i)
        {
            if (skeleton.joints[i].name == name)
                return static_cast<int>(i);
        }
        return -1;
    }

    AnimPose poseAtRest(const Skeleton& skeleton)
    {
        Vector3f   T[AnimPose::kMaxBones];
        Quaternion R[AnimPose::kMaxBones];
        Vector3f   S[AnimPose::kMaxBones];
        const uint32_t n = static_cast<uint32_t>(skeleton.joints.size());
        for (uint32_t i = 0; i < n && i < AnimPose::kMaxBones; ++i)
        {
            T[i] = skeleton.joints[i].restT;
            R[i] = skeleton.joints[i].restR;
            S[i] = skeleton.joints[i].restS;
        }
        AnimPose pose;
        localToPalette(skeleton, T, R, S, pose);
        return pose;
    }

    TransformComponent transformAt(const Vector3f& position, const Vector3f& scale)
    {
        TransformComponent xf;
        xf.position = position;
        xf.rotation = Quaternion::IDENTITY;
        xf.scale    = scale;
        return xf;
    }

    void expectVec(const Vector3f& got, const Vector3f& want)
    {
        EXPECT_NEAR(got.x, want.x, 1.0e-4f);
        EXPECT_NEAR(got.y, want.y, 1.0e-4f);
        EXPECT_NEAR(got.z, want.z, 1.0e-4f);
    }

    Vector3f modelPoint(const AnimPose& pose, int joint)
    {
        return pose.jointWorld[joint].GetTranslation();
    }

    Renderer& unboundRenderer()
    {
        return *reinterpret_cast<Renderer*>(reinterpret_cast<void*>(static_cast<uintptr_t>(1)));
    }

} // namespace

TEST(HitVolume, HumanoidResolvesSevenParts)
{
    const Skeleton skeleton = humanoidSkeleton(nullptr);
    const AnimPose pose     = poseAtRest(skeleton);
    const TransformComponent xf = transformAt(Vector3f::ZERO, Vector3f{ 1.0f, 1.0f, 1.0f });

    HitVolumesComponent volumes;
    volumes.disableTimer[hitPartIndex(HitPart::Head)]      = 3.0f;
    volumes.impactWeight[hitPartIndex(HitPart::RightArm)] = 0.4f;
    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);

    EXPECT_TRUE(volumes.jointsResolved);
    EXPECT_EQ(volumes.activeCount(), 7);

    const HitPart parts[kHitPartCount] = {
        HitPart::Head, HitPart::RightArm, HitPart::LeftArm, HitPart::RightLeg, HitPart::LeftLeg, HitPart::Chest, HitPart::Waist,
    };
    const char* proximal[kHitPartCount] = { "Neck", "R_UpperArm", "L_UpperArm", "R_UpperLeg", "L_UpperLeg", "Spine", "Hips" };
    const char* distal[kHitPartCount]   = { "Head", "R_Hand", "L_Hand", "R_Foot", "L_Foot", "Chest", "Spine" };
    const float radius[kHitPartCount]   = { 0.10f, 0.06f, 0.06f, 0.06f, 0.06f, 0.14f, 0.12f };
    for (int i = 0; i < kHitPartCount; ++i)
    {
        const HitSegment* segment = volumes.find(parts[i]);
        ASSERT_NE(segment, nullptr);
        EXPECT_TRUE(segment->active);
        EXPECT_EQ(segment->part, parts[i]);
        EXPECT_NEAR(segment->radius, radius[i], 1.0e-4f);
        expectVec(segment->a, modelPoint(pose, jointIndex(skeleton, proximal[i])));
        expectVec(segment->b, modelPoint(pose, jointIndex(skeleton, distal[i])));
    }

    EXPECT_FLOAT_EQ(volumes.disableTimer[hitPartIndex(HitPart::Head)], 3.0f);
    EXPECT_FLOAT_EQ(volumes.impactWeight[hitPartIndex(HitPart::RightArm)], 0.4f);
    EXPECT_FLOAT_EQ(volumes.disableTimer[hitPartIndex(HitPart::Waist)], 0.0f);
    EXPECT_FLOAT_EQ(volumes.impactWeight[hitPartIndex(HitPart::Head)], 0.0f);
}

TEST(HitVolume, HeadEndpointIncludesEntityTranslation)
{
    const Skeleton skeleton = humanoidSkeleton(nullptr);
    AnimPose       pose     = poseAtRest(skeleton);
    const int      head     = jointIndex(skeleton, "Head");
    const int      neck     = jointIndex(skeleton, "Neck");
    ASSERT_GE(head, 0);
    ASSERT_GE(neck, 0);

    pose.jointWorld[head] = Matrix4f::TranslationMatrix(0.20f, 1.90f, -0.30f);
    const Vector3f movedHead = modelPoint(pose, head);
    const Vector3f neckModel = modelPoint(pose, neck);
    const Vector3f translation{ 4.0f, 5.0f, 6.0f };
    const TransformComponent xf = transformAt(translation, Vector3f{ 1.0f, 1.0f, 1.0f });

    HitVolumesComponent volumes;
    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);

    const HitSegment* segment = volumes.find(HitPart::Head);
    ASSERT_NE(segment, nullptr);
    ASSERT_TRUE(segment->active);
    expectVec(segment->a, neckModel + translation);
    expectVec(segment->b, movedHead + translation);
}

TEST(HitVolume, MissingHandEndsArmOnLowerArm)
{
    const Skeleton skeleton = humanoidSkeleton("L_Hand");
    const AnimPose pose     = poseAtRest(skeleton);
    const TransformComponent xf = transformAt(Vector3f::ZERO, Vector3f{ 1.0f, 1.0f, 1.0f });
    ASSERT_LT(jointIndex(skeleton, "L_Hand"), 0);
    const int lower = jointIndex(skeleton, "L_LowerArm");
    const int upper = jointIndex(skeleton, "L_UpperArm");
    ASSERT_GE(lower, 0);
    ASSERT_GE(upper, 0);

    HitVolumesComponent volumes;
    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);

    const HitSegment* left = volumes.find(HitPart::LeftArm);
    ASSERT_NE(left, nullptr);
    EXPECT_TRUE(left->active);
    expectVec(left->a, modelPoint(pose, upper));
    expectVec(left->b, modelPoint(pose, lower));
    EXPECT_EQ(volumes.distal[hitPartIndex(HitPart::LeftArm)], lower);

    const HitSegment* right = volumes.find(HitPart::RightArm);
    ASSERT_NE(right, nullptr);
    EXPECT_TRUE(right->active);
    expectVec(right->b, modelPoint(pose, jointIndex(skeleton, "R_Hand")));
    EXPECT_EQ(volumes.activeCount(), 7);
}

TEST(HitVolume, MissingHeadDeactivatesHeadOnly)
{
    const Skeleton skeleton = humanoidSkeleton("Head");
    const AnimPose pose     = poseAtRest(skeleton);
    const TransformComponent xf = transformAt(Vector3f::ZERO, Vector3f{ 1.0f, 1.0f, 1.0f });

    HitVolumesComponent volumes;
    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);

    const HitSegment* head = volumes.find(HitPart::Head);
    ASSERT_NE(head, nullptr);
    EXPECT_FALSE(head->active);

    const HitSegment* chest = volumes.find(HitPart::Chest);
    ASSERT_NE(chest, nullptr);
    EXPECT_TRUE(chest->active);
    expectVec(chest->a, modelPoint(pose, jointIndex(skeleton, "Spine")));
    expectVec(chest->b, modelPoint(pose, jointIndex(skeleton, "Chest")));
    EXPECT_EQ(volumes.activeCount(), 6);
}

TEST(HitVolume, MissingLimbActivatesNothing)
{
    const Skeleton skeleton = humanoidSkeleton("L_UpperArm");
    const AnimPose pose     = poseAtRest(skeleton);
    const TransformComponent xf = transformAt(Vector3f::ZERO, Vector3f{ 1.0f, 1.0f, 1.0f });
    ASSERT_GE(jointIndex(skeleton, "Head"), 0);
    ASSERT_GE(jointIndex(skeleton, "Hips"), 0);
    ASSERT_LT(jointIndex(skeleton, "L_UpperArm"), 0);

    HitVolumesComponent volumes;
    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);

    EXPECT_EQ(volumes.activeCount(), 0);
    EXPECT_TRUE(volumes.jointsResolved);
    for (int i = 0; i < kHitPartCount; ++i)
        EXPECT_FALSE(volumes.segment[i].active);
}

TEST(HitVolume, RadiusScalesByLargestAxis)
{
    const Skeleton skeleton = humanoidSkeleton(nullptr);
    const AnimPose pose     = poseAtRest(skeleton);
    const TransformComponent xf = transformAt(Vector3f::ZERO, Vector3f{ 2.0f, 0.5f, 0.25f });

    HitVolumesComponent volumes;
    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);

    const HitSegment* arm = volumes.find(HitPart::LeftArm);
    ASSERT_NE(arm, nullptr);
    ASSERT_TRUE(arm->active);
    EXPECT_NEAR(arm->radius, 0.12f, 1.0e-4f);

    const HitSegment* head = volumes.find(HitPart::Head);
    ASSERT_NE(head, nullptr);
    EXPECT_NEAR(head->radius, 0.20f, 1.0e-4f);
}

TEST(HitVolume, NullSkeletonDoesNotStick)
{
    const Skeleton skeleton = humanoidSkeleton(nullptr);
    const AnimPose pose     = poseAtRest(skeleton);
    const TransformComponent xf = transformAt(Vector3f{ 1.0f, 0.0f, 0.0f }, Vector3f{ 1.0f, 1.0f, 1.0f });

    HitVolumesComponent volumes;
    rebuildHitVolumes(volumes, nullptr, nullptr, &xf);
    EXPECT_FALSE(volumes.jointsResolved);
    EXPECT_EQ(volumes.activeCount(), 0);

    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);
    EXPECT_TRUE(volumes.jointsResolved);
    EXPECT_EQ(volumes.activeCount(), 7);
}

TEST(HitVolume, ZeroActiveCapsulesStayInactive)
{
    const Skeleton skeleton = humanoidSkeleton(nullptr);
    const TransformComponent xf = transformAt(Vector3f::ZERO, Vector3f{ 1.0f, 1.0f, 1.0f });
    AnimPose empty;
    empty.boneCount = 0;

    HitVolumesComponent volumes;
    EXPECT_EQ(volumes.activeCount(), 0);
    volumes.disableTimer[hitPartIndex(HitPart::LeftLeg)] = 1.5f;
    rebuildHitVolumes(volumes, &skeleton, &empty, &xf);

    EXPECT_TRUE(volumes.jointsResolved);
    EXPECT_EQ(volumes.activeCount(), 0);
    EXPECT_FLOAT_EQ(volumes.disableTimer[hitPartIndex(HitPart::LeftLeg)], 1.5f);

    const AnimPose pose = poseAtRest(skeleton);
    rebuildHitVolumes(volumes, &skeleton, &pose, &xf);
    EXPECT_EQ(volumes.activeCount(), 7);
    EXPECT_FLOAT_EQ(volumes.disableTimer[hitPartIndex(HitPart::LeftLeg)], 1.5f);
}

TEST(HitVolume, PlayerSetupEmplaces)
{
    World  world;
    Entity e = world.createEntity();
    ensureHumanoidBody(world, e, 200.0f, Vector3f{ 0.35f, 0.7f, 0.35f });

    HitVolumesComponent* volumes = world.get<HitVolumesComponent>(e);
    ASSERT_NE(volumes, nullptr);
    EXPECT_EQ(volumes->activeCount(), 0);
    for (int i = 0; i < kHitPartCount; ++i)
    {
        EXPECT_FLOAT_EQ(volumes->disableTimer[i], 0.0f);
        EXPECT_FLOAT_EQ(volumes->impactWeight[i], 0.0f);
    }

    volumes->disableTimer[hitPartIndex(HitPart::Chest)] = 2.0f;
    ensureHumanoidBody(world, e, 50.0f, Vector3f{ 9.0f, 9.0f, 9.0f });
    EXPECT_FLOAT_EQ(world.get<HitVolumesComponent>(e)->disableTimer[hitPartIndex(HitPart::Chest)], 2.0f);
}

TEST(HitVolume, HunterAttachEmplaces)
{
    World         world;
    AssetManager  assets;
    AssetPinTable pins;
    AiSystem      ai;

    Entity e = world.createEntity();
    world.emplace<TagComponent>(e, "hunter");
    TransformComponent xf{};
    xf.position = Vector3f{ 4.0f, 1.0f, -2.0f };
    world.emplace<TransformComponent>(e, xf);

    ASSERT_TRUE(ai.attachHunter(world, e, pins, assets));
    HitVolumesComponent* volumes = world.get<HitVolumesComponent>(e);
    ASSERT_NE(volumes, nullptr);
    EXPECT_EQ(volumes->activeCount(), 0);
}

TEST(HitVolume, WolfConfigureDoesNotEmplace)
{
    World                   world;
    AssetManager            assets;
    AssetPinTable           pins;
    Audio::AudioSystem      audio;
    Terrain::TerrainGround  ground;
    Entity                  e = world.createEntity();
    world.emplace<TagComponent>(e, "Wolf");
    world.emplace<TransformComponent>(e, TransformComponent{});

    // configureNpc does not read the renderer. A live Renderer would open a window.
    const CharacterHost host{ world, unboundRenderer(), assets, pins, audio, ground };
    configureNpc(host, e, NpcKind::Wolf);
    EXPECT_FALSE(world.has<HitVolumesComponent>(e));
}

TEST(HitVolume, TickRebuildsHeadFromLatestPose)
{
    World        world;
    AssetManager assets;
    Entity       e = world.createEntity();

    const Skeleton skeleton = humanoidSkeleton(nullptr);
    AnimationClip  clip;
    clip.name     = "Idle";
    clip.duration = 1.0f;
    auto set      = std::make_shared<AnimationSet>();
    set->setClips({ clip });
    auto model = std::make_shared<Model>();
    model->setSkeleton(skeleton);
    model->setAnimationSet(set);

    AnimGraphComponent ag;
    ag.model   = model;
    ag.animSet = set;
    ag.graph.player().bind(model->skeleton(), set.get());
    ASSERT_TRUE(ag.graph.player().play("Idle", 0.0f));
    world.emplace<AnimGraphComponent>(e, std::move(ag));

    const Vector3f translation{ 4.0f, 5.0f, 6.0f };
    world.emplace<TransformComponent>(e, transformAt(translation, Vector3f{ 1.0f, 1.0f, 1.0f }));
    world.emplace<HitVolumesComponent>(e);

    EXPECT_EQ(world.get<HitVolumesComponent>(e)->activeCount(), 0);
    tickAnimGraphs(world, assets, 0.1f);

    const Skeleton* posed = model->skeleton();
    ASSERT_NE(posed, nullptr);
    const AnimPose rest = poseAtRest(*posed);
    const int      head = jointIndex(*posed, "Head");
    const int      neck = jointIndex(*posed, "Neck");
    const HitSegment* segment = world.get<HitVolumesComponent>(e)->find(HitPart::Head);
    ASSERT_NE(segment, nullptr);
    ASSERT_TRUE(segment->active);
    expectVec(segment->a, modelPoint(rest, neck) + translation);
    expectVec(segment->b, modelPoint(rest, head) + translation);
    EXPECT_EQ(world.get<HitVolumesComponent>(e)->activeCount(), 7);
}
