#include "Animation/Locomotion.h"

#include <cmath>
#include <string_view>

namespace Dark
{
	namespace
	{
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
	}

	void applyLocomotionYawSplit(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, float yawRadians)
	{
		if (!localR || count == 0 || fabsf(yawRadians) < 1.0e-4f)
			return;
		const int32_t hip = findJoint(skeleton, "Hips");
		const int32_t spine = findJoint(skeleton, "Spine");
		if (hip < 0 || spine < 0)
			return;
		if (static_cast<uint32_t>(hip) >= count || static_cast<uint32_t>(spine) >= count)
			return;

		const Math::Quaternion yaw = Math::Quaternion::FromAxisAngle(Math::Vector3f::Y_AXIS, yawRadians);
		const Math::Quaternion undo = yaw.Conjugate();
		localR[hip] = yaw * localR[hip];
		localR[spine] = undo * localR[spine];
	}
}
