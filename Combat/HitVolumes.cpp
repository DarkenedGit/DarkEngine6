#include "Combat/HitVolumes.h"

#include "Animation/Skeleton.h"
#include "ECS/Components.h"
#include "Math/MathHelper.h"

#include <cmath>
#include <string_view>

namespace Dark::Combat
{

    namespace
    {

        struct PartSpec
        {
            HitPart     part;
            const char* proximal;
            const char* distal;
            const char* distalFallback;
            float       radius;
        };

        // Model-space radii. Hand and Foot fall back to the lower joint; head, chest, and waist do not.
        constexpr PartSpec kParts[kHitPartCount] = {
            { HitPart::Head, "Neck", "Head", nullptr, 0.10f },
            { HitPart::RightArm, "R_UpperArm", "R_Hand", "R_LowerArm", 0.06f },
            { HitPart::LeftArm, "L_UpperArm", "L_Hand", "L_LowerArm", 0.06f },
            { HitPart::RightLeg, "R_UpperLeg", "R_Foot", "R_LowerLeg", 0.06f },
            { HitPart::LeftLeg, "L_UpperLeg", "L_Foot", "L_LowerLeg", 0.06f },
            { HitPart::Chest, "Spine", "Chest", nullptr, 0.14f },
            { HitPart::Waist, "Hips", "Spine", nullptr, 0.12f },
        };

        static_assert(kParts[0].part == HitPart::Head);
        static_assert(kParts[1].part == HitPart::RightArm);
        static_assert(kParts[2].part == HitPart::LeftArm);
        static_assert(kParts[3].part == HitPart::RightLeg);
        static_assert(kParts[4].part == HitPart::LeftLeg);
        static_assert(kParts[5].part == HitPart::Chest);
        static_assert(kParts[6].part == HitPart::Waist);

        int32_t findJoint(const Skeleton& skeleton, std::string_view name)
        {
            const uint32_t n = static_cast<uint32_t>(skeleton.joints.size());
            for (uint32_t i = 0; i < n; ++i)
            {
                if (skeleton.joints[i].name == name)
                    return static_cast<int32_t>(i);
            }
            return -1;
        }

        bool isHumanoid(const Skeleton& skeleton)
        {
            return findJoint(skeleton, "L_UpperArm") >= 0 && findJoint(skeleton, "R_UpperArm") >= 0 && findJoint(skeleton, "L_UpperLeg") >= 0
                && findJoint(skeleton, "R_UpperLeg") >= 0;
        }

        void clearSegments(HitVolumesComponent& volumes)
        {
            for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
            {
                HitSegment& segment = volumes.segment[i];
                segment.a           = Math::Vector3f::ZERO;
                segment.b           = Math::Vector3f::ZERO;
                segment.radius      = 0.0f;
                segment.active      = false;
            }
        }

        void resolveJoints(HitVolumesComponent& volumes, const Skeleton& skeleton)
        {
            volumes.jointsResolved = true;
            const bool humanoid    = isHumanoid(skeleton);
            for (int i = 0; i < kHitPartCount; ++i)
            {
                volumes.proximal[i]    = -1;
                volumes.distal[i]      = -1;
                volumes.modelRadius[i] = 0.0f;
                volumes.segment[i].part = kParts[i].part;
                if (!humanoid)
                    continue;

                const int32_t proximal = findJoint(skeleton, kParts[i].proximal);
                int32_t       distal   = findJoint(skeleton, kParts[i].distal);
                if (distal < 0 && kParts[i].distalFallback)
                    distal = findJoint(skeleton, kParts[i].distalFallback);
                if (proximal < 0 || distal < 0)
                    continue;

                volumes.proximal[i]    = proximal;
                volumes.distal[i]      = distal;
                volumes.modelRadius[i] = kParts[i].radius;
            }
            clearSegments(volumes);
        }

        bool jointInPose(int32_t index, const AnimPose& pose)
        {
            return index >= 0 && static_cast<uint32_t>(index) < pose.boneCount && static_cast<uint32_t>(index) < AnimPose::kMaxBones;
        }

        float largestScaleAxis(const Math::Vector3f& scale)
        {
            float axis = fabsf(scale.x);
            const float y = fabsf(scale.y);
            const float z = fabsf(scale.z);
            if (y > axis)
                axis = y;
            if (z > axis)
                axis = z;
            return axis;
        }

        bool timerAboveZero(const HitVolumesComponent* volumes, HitPart part)
        {
            if (!volumes)
                return false;
            const int index = hitPartIndex(part);
            return index >= 0 && volumes->disableTimer[index] > 0.0f;
        }

        void writeSegments(HitVolumesComponent& volumes, const AnimPose& pose, const TransformComponent& transform)
        {
            // jointWorld is model space. Same entity matrix as SwordView: scale, then rotation, then translation.
            const Math::Matrix4f entityWorld = worldMatrix(transform);
            const float           scale      = largestScaleAxis(transform.scale);
            for (int i = 0; i < kHitPartCount; ++i)
            {
                HitSegment& segment = volumes.segment[i];
                segment.part        = kParts[i].part;
                const int32_t a     = volumes.proximal[i];
                const int32_t b     = volumes.distal[i];
                if (!jointInPose(a, pose) || !jointInPose(b, pose))
                {
                    segment.a      = Math::Vector3f::ZERO;
                    segment.b      = Math::Vector3f::ZERO;
                    segment.radius = 0.0f;
                    segment.active = false;
                    continue;
                }
                segment.a      = (pose.jointWorld[a] * entityWorld).GetTranslation();
                segment.b      = (pose.jointWorld[b] * entityWorld).GetTranslation();
                segment.radius = volumes.modelRadius[i] * scale;
                segment.active = true;
            }
        }

    } // namespace

    void rebuildHitVolumes(HitVolumesComponent& volumes, const Skeleton* skeleton, const AnimPose* pose, const TransformComponent* transform)
    {
        if (!volumes.jointsResolved)
        {
            if (!skeleton || skeleton->joints.empty())
            {
                clearSegments(volumes);
                return;
            }
            resolveJoints(volumes, *skeleton);
        }

        if (!pose || pose->boneCount == 0 || !transform)
        {
            clearSegments(volumes);
            return;
        }

        writeSegments(volumes, *pose, *transform);
    }

    void noteLandedPartHit(HitVolumesComponent& volumes, HitPart part)
    {
        const int index = hitPartIndex(part);
        if (index < 0)
            return;
        volumes.disableTimer[index]  = kPartDisableSeconds;
        volumes.impactWeight[index]  = 1.0f;
    }

    void tickHitVolumes(HitVolumesComponent& volumes, float dt)
    {
        if (dt < 0.0f)
            return;
        for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
        {
            volumes.disableTimer[i] -= dt;
            if (volumes.disableTimer[i] < 0.0f)
                volumes.disableTimer[i] = 0.0f;
            volumes.impactWeight[i] -= kImpactWeightDecayPerSec * dt;
            if (volumes.impactWeight[i] < 0.0f)
                volumes.impactWeight[i] = 0.0f;
        }
    }

    void clearHitPartState(HitVolumesComponent& volumes)
    {
        for (int i = 0; i < HitVolumesComponent::kSlotCount; ++i)
        {
            volumes.disableTimer[i]  = 0.0f;
            volumes.impactWeight[i]  = 0.0f;
        }
    }

    float hitVolumesSpeedScale(const HitVolumesComponent* volumes, float existingScale)
    {
        if (!volumes)
            return 1.0f;
        float product = existingScale;
        if (timerAboveZero(volumes, HitPart::RightLeg))
            product *= kDisabledLegSpeedScale;
        if (timerAboveZero(volumes, HitPart::LeftLeg))
            product *= kDisabledLegSpeedScale;
        if (timerAboveZero(volumes, HitPart::Waist))
            product *= kDisabledWaistSpeedScale;
        return Math::Clamp(product, 0.0f, 1.0f);
    }

    bool hitVolumesBlockFire(const HitVolumesComponent* volumes)
    {
        return timerAboveZero(volumes, HitPart::RightArm);
    }

    bool hitVolumesBlockHunterRelease(const HitVolumesComponent* volumes)
    {
        return timerAboveZero(volumes, HitPart::RightArm);
    }

    bool hitVolumesBlockShield(const HitVolumesComponent* volumes)
    {
        return timerAboveZero(volumes, HitPart::LeftArm);
    }

    bool hitVolumesBlockHunterBlock(const HitVolumesComponent* volumes)
    {
        return timerAboveZero(volumes, HitPart::LeftArm);
    }

    bool hitVolumesBlockJump(const HitVolumesComponent*)
    {
        return false;
    }

    Math::Vector3f partKnockbackDirection(HitPart part, const TransformComponent* attacker, const Math::Vector3f& surfacePoint,
                                          const Math::Vector3f& attackDirection)
    {
        if (part == HitPart::None || !attacker)
            return attackDirection;
        Math::Vector3f flat{ surfacePoint.x - attacker->position.x, 0.0f, surfacePoint.z - attacker->position.z };
        if (flat.MagnitudeSqrd() < 1.0e-8f)
            return attackDirection;
        flat.Normalize();
        return flat;
    }

} // namespace Dark::Combat
