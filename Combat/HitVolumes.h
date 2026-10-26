#pragma once

#include "Combat/HitPart.h"
#include "ECS/Persist.h"
#include "Math/Vector3f.h"

#include <cstdint>

namespace Dark
{

    struct AnimPose;
    struct Skeleton;
    struct TransformComponent;

    namespace Combat
    {

        // World-space segment. a is the proximal joint, b the distal joint.
        struct HitSegment
        {
            HitPart        part   = HitPart::None;
            Math::Vector3f a{};
            Math::Vector3f b{};
            float          radius = 0.0f;
            bool           active = false;
        };

        // Pose cache plus seven disable timers and seven impact weights.
        // Save persists the timers and weights. The pose cache is rebuilt.
        inline constexpr float kPartDisableSeconds      = 3.0f;
        inline constexpr float kImpactWeightDecayPerSec = 3.5f;
        inline constexpr float kDisabledLegSpeedScale   = 0.65f;
        inline constexpr float kDisabledWaistSpeedScale = 0.85f;

        struct HitVolumesComponent
        {
            static constexpr const char* kTypeName    = "HitVolumes";
            static constexpr uint16_t    kSaveVersion = 1;
            static constexpr int         kSlotCount   = kHitPartCount;
            static const PersistFns      kPersist;

            bool       jointsResolved = false;
            int32_t    proximal[kSlotCount]{};
            int32_t    distal[kSlotCount]{};
            float      modelRadius[kSlotCount]{};
            HitSegment segment[kSlotCount]{};
            float      disableTimer[kSlotCount]{};
            float      impactWeight[kSlotCount]{};

            HitVolumesComponent()
            {
                for (int i = 0; i < kSlotCount; ++i)
                {
                    proximal[i]     = -1;
                    distal[i]       = -1;
                    segment[i].part = static_cast<HitPart>(static_cast<int>(HitPart::Head) + i);
                }
            }

            int activeCount() const
            {
                int count = 0;
                for (int i = 0; i < kSlotCount; ++i)
                {
                    if (segment[i].active)
                        ++count;
                }
                return count;
            }

            HitSegment* find(HitPart part)
            {
                const int index = hitPartIndex(part);
                return index >= 0 ? &segment[index] : nullptr;
            }

            const HitSegment* find(HitPart part) const
            {
                const int index = hitPartIndex(part);
                return index >= 0 ? &segment[index] : nullptr;
            }
        };

        // Resolve joint indices on the first skeleton. A null skeleton does not stick.
        // No pose, or a skeleton without the four limb names, leaves every segment inactive.
        void rebuildHitVolumes(HitVolumesComponent& volumes, const Skeleton* skeleton, const AnimPose* pose, const TransformComponent* transform);

        // Sets this part's disable timer to 3 seconds and its impact weight to 1. None is ignored.
        void noteLandedPartHit(HitVolumesComponent& volumes, HitPart part);
        void tickHitVolumes(HitVolumesComponent& volumes, float dt);
        void clearHitPartState(HitVolumesComponent& volumes);

        // Null volumes yield 1. Otherwise the existing scale times each disabled leg and a disabled waist, clamped to 0..1.
        float hitVolumesSpeedScale(const HitVolumesComponent* volumes, float existingScale);
        bool hitVolumesBlockFire(const HitVolumesComponent* volumes);
        bool hitVolumesBlockHunterRelease(const HitVolumesComponent* volumes);
        bool hitVolumesBlockShield(const HitVolumesComponent* volumes);
        bool hitVolumesBlockHunterBlock(const HitVolumesComponent* volumes);
        bool hitVolumesBlockJump(const HitVolumesComponent* volumes);

        // Horizontal unit from the attacker to the surface point.
        // Part None, a missing attacker, or no horizontal separation keeps the attack direction.
        Math::Vector3f partKnockbackDirection(HitPart part, const TransformComponent* attacker, const Math::Vector3f& surfacePoint,
                                              const Math::Vector3f& attackDirection);

    } // namespace Combat

} // namespace Dark
