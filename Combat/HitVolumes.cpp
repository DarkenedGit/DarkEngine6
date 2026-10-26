#include "Combat/HitVolumes.h"

#include "Animation/Skeleton.h"
#include "ECS/Components.h"

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

} // namespace Dark::Combat
