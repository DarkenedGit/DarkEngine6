#pragma once

#include "Combat/HitPart.h"
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

        // Pose cache plus room for seven disable timers and seven impact weights.
        // Save does not own this cache.
        struct HitVolumesComponent
        {
            static constexpr const char* kTypeName = "HitVolumes";
            static constexpr int         kSlotCount = kHitPartCount;

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

    } // namespace Combat

} // namespace Dark
