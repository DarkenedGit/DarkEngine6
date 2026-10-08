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

	void applyChargeWindup(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, const ChargeWindup& wind)
	{
		if (!localR || count == 0)
			return;
		const float attack = Math::Clamp(wind.attack, 0.0f, 1.0f);
		const float block  = Math::Clamp(wind.block, 0.0f, 1.0f);
		const float strike = Math::Clamp(wind.blockStrike, 0.0f, 1.0f);
		if (attack < 1.0e-4f && block < 1.0e-4f && strike < 1.0e-4f)
			return;

		const int32_t chest = findJoint(skeleton, "Chest");
		const int32_t upper = findJoint(skeleton, "R_UpperArm");
		const int32_t lower = findJoint(skeleton, "R_LowerArm");
		auto add = [&](int32_t joint, const Math::Vector3f& axis, float radians) {
			if (joint < 0 || static_cast<uint32_t>(joint) >= count || fabsf(radians) < 1.0e-4f)
				return;
			const Math::Quaternion q = Math::Quaternion::FromAxisAngle(axis, radians);
			localR[joint] = q * localR[joint];
		};

		// Positive X on a hanging arm swings it toward -Z, which is behind the character.
		// SwingSword uses the opposite pitch, so dropping this pose is the swing forward.
		const float cock = Math::Clamp(attack + 0.85f * block, 0.0f, 1.0f) * (1.0f - 0.75f * strike);
		add(upper, Math::Vector3f::X_AXIS, 0.95f * cock);
		add(upper, Math::Vector3f::Z_AXIS, -0.50f * cock);
		add(lower, Math::Vector3f::X_AXIS, 0.40f * cock);
		add(chest, Math::Vector3f::Y_AXIS, -0.22f * cock);

		add(upper, Math::Vector3f::X_AXIS, -0.85f * strike);
		add(upper, Math::Vector3f::Z_AXIS, -0.35f * strike);
		add(chest, Math::Vector3f::Y_AXIS, 0.28f * strike);
	}

	void applyHitFlinch(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, float forward, float side)
	{
		if (!localR || count == 0 || (fabsf(forward) < 1.0e-4f && fabsf(side) < 1.0e-4f))
			return;

		auto add = [&](int32_t joint, const Math::Vector3f& axis, float radians) {
			if (joint < 0 || static_cast<uint32_t>(joint) >= count)
				return;
			localR[joint] = Math::Quaternion::FromAxisAngle(axis, radians) * localR[joint];
		};

		const int32_t spine = findJoint(skeleton, "Spine");
		const int32_t chest = findJoint(skeleton, "Chest");
		const int32_t neck  = findJoint(skeleton, "Neck");
		add(spine, Math::Vector3f::X_AXIS, 0.30f * forward);
		add(chest, Math::Vector3f::X_AXIS, 0.25f * forward);
		add(neck, Math::Vector3f::X_AXIS, 0.20f * forward);
		add(spine, Math::Vector3f::Z_AXIS, -0.30f * side);
		add(chest, Math::Vector3f::Z_AXIS, -0.25f * side);
		add(neck, Math::Vector3f::Z_AXIS, -0.20f * side);
	}
}
