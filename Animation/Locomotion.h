#pragma once

#include "Animation/Skeleton.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cmath>
#include <string_view>

namespace Dark
{
	// Horizontal motion for the locomotion graph.
	// speed is the planar magnitude. strafe is +speed when moving to the character's
	// right and -speed when moving left, and stays 0 when forward or back dominates
	// so diagonals keep the forward walk / run.
	struct LocomotionSample
	{
		float speed  = 0.0f;
		float strafe = 0.0f;
	};

	inline LocomotionSample locomotionSample(const Math::Vector3f& worldVelocity, const Math::Quaternion& facing)
	{
		const Math::Vector3f vel{ worldVelocity.x, 0.0f, worldVelocity.z };
		LocomotionSample     out;
		out.speed = vel.Magnitude();
		if (out.speed < 0.05f)
			return out;

		Math::Vector3f fwd = facing.Rotate(Math::Vector3f::Z_AXIS);
		fwd.y              = 0.0f;
		Math::Vector3f right = facing.Rotate(Math::Vector3f::X_AXIS);
		right.y              = 0.0f;
		if (fwd.MagnitudeSqrd() < 1.0e-8f || right.MagnitudeSqrd() < 1.0e-8f)
			return out;
		fwd.Normalize();
		right.Normalize();

		const float forwardSpeed = vel.Dot(fwd);
		const float lateralSpeed = vel.Dot(right);
		if (fabsf(lateralSpeed) > fabsf(forwardSpeed))
			out.strafe = std::copysign(out.speed, lateralSpeed);
		return out;
	}

	// Signed yaw from the entity's horizontal facing to its horizontal velocity.
	// Positive turns +Z toward +X. Zero when nearly stopped, so idle faces the look direction.
	inline float locomotionYawOffset(const Math::Vector3f& worldVelocity, const Math::Quaternion& facing)
	{
		const Math::Vector3f vel{ worldVelocity.x, 0.0f, worldVelocity.z };
		if (vel.MagnitudeSqrd() < 0.25f)
			return 0.0f;
		Math::Vector3f fwd = facing.Rotate(Math::Vector3f::Z_AXIS);
		fwd.y              = 0.0f;
		if (fwd.MagnitudeSqrd() < 1.0e-8f)
			return 0.0f;
		fwd.Normalize();
		Math::Vector3f dir = vel;
		dir.Normalize();
		const float dot     = fwd.Dot(dir);
		const float crossY  = fwd.z * dir.x - fwd.x * dir.z;
		return atan2f(crossY, dot);
	}

	// Clip rate that keeps the feet moving at the body's planar speed. Other clips play at 1.
	inline float locomotionPlaybackScale(std::string_view clip, float speed)
	{
		float native = 0.0f;
		if (clip == "Walk" || clip == "WalkBack")
			native = 1.45f;
		else if (clip == "Run" || clip == "RunBack")
			native = 3.4f;
		else if (clip == "StrafeLeft" || clip == "StrafeRight")
			native = 1.3f;
		else if (clip == "StrafeRunLeft" || clip == "StrafeRunRight")
			native = 2.7f;
		else if (clip == "CrouchWalk")
			native = 0.55f;
		if (native <= 0.0f || speed < 0.05f)
			return 1.0f;
		return Math::Clamp(speed / native, 0.6f, 3.0f);
	}

	inline float approachAngle(float current, float target, float speedRadPerSec, float dt)
	{
		const float delta = Math::WrapPi(target - current);
		const float maxStep = speedRadPerSec * ((dt > 0.0f) ? dt : 0.0f);
		if (fabsf(delta) <= maxStep)
			return Math::WrapPi(target);
		return Math::WrapPi(current + std::copysign(maxStep, delta));
	}

	// Yaw Hips (and the legs under them) by `yawRadians` and counter-yaw Spine so the
	// chest, head, and arms stay in the entity's facing frame.
	void applyLocomotionYawSplit(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, float yawRadians);

	// Additive wind-up on the weapon/shield arm (R_UpperArm) and chest.
	// attack/block pull that arm and shoulder back. blockStrike drives them forward.
	struct ChargeWindup
	{
		float attack      = 0.0f;
		float block       = 0.0f;
		float blockStrike = 0.0f;
	};

	void applyChargeWindup(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, const ChargeWindup& wind);

	void applyHitFlinch(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, float forward, float side);

	// Slot order matches the seven hit parts: head, right arm, left arm, right leg, left leg, chest, waist.
	struct PartImpactWeights
	{
		static constexpr int kCount = 7;
		float weight[kCount]{};
	};

	// Local twist on the struck joints. Weight 0 leaves the pose alone. Missing joint names are skipped.
	void applyPartImpact(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, const PartImpactWeights& impact);

	// 0..1. Lifts the off-hand (L_*) arm so the forearm is across the chest, shield out front.
	void applyShieldGuard(const Skeleton& skeleton, Math::Quaternion* localR, uint32_t count, float alpha);

	// Smooth the pose toward the live hold. Release eases the arm forward into the swing.
	inline void stepChargeWindup(ChargeWindup& pose, float dt, bool attackHeld, float attackHeldSec, float attackWindow,
		bool blockHeld, float blockHeldSec, float blockWindow, bool blockStriking)
	{
		if (dt < 0.0f)
			dt = 0.0f;
		auto frac = [](float held, float window) {
			if (window <= 1.0e-4f)
				return held > 0.0f ? 1.0f : 0.0f;
			return Math::Clamp(held / window, 0.0f, 1.0f);
		};
		const float attackTarget = attackHeld ? frac(attackHeldSec, attackWindow) : 0.0f;
		const float blockTarget  = (blockHeld && !blockStriking) ? frac(blockHeldSec, blockWindow) : 0.0f;
		const float strikeTarget = blockStriking ? 1.0f : 0.0f;
		auto approach = [&](float current, float target, float rate) {
			const float delta = target - current;
			const float step  = rate * dt;
			if (fabsf(delta) <= step)
				return target;
			return current + std::copysign(step, delta);
		};
		pose.attack      = approach(pose.attack, attackTarget, 6.0f);
		pose.block       = approach(pose.block, blockTarget, 6.0f);
		pose.blockStrike = approach(pose.blockStrike, strikeTarget, 10.0f);
	}

	// Aim-relative player locomotion. Hips track travel but only within ±75° of look.
	// Past that the side-step clips carry the motion, and past ±110° the backward cycle does.
	struct AimLocomotion
	{
		float speed     = 0.0f;
		float strafe    = 0.0f;
		bool  backward  = false;
		float lowerYaw  = 0.0f;
	};

	inline AimLocomotion aimLocomotion(const Math::Vector3f& worldVelocity, const Math::Quaternion& facing)
	{
		AimLocomotion out;
		const Math::Vector3f vel{ worldVelocity.x, 0.0f, worldVelocity.z };
		out.speed = vel.Magnitude();
		if (out.speed < 0.05f)
			return out;

		const float yaw = locomotionYawOffset(worldVelocity, facing);
		constexpr float kLegYaw  = 75.0f * Math::DegToRad;
		constexpr float kBackYaw = 110.0f * Math::DegToRad;
		const float absYaw = fabsf(yaw);
		if (absYaw > kBackYaw)
		{
			out.backward = true;
			out.lowerYaw = 0.0f;
		}
		else if (absYaw > kLegYaw)
		{
			out.strafe   = std::copysign(out.speed, yaw);
			out.lowerYaw = std::copysign(kLegYaw, yaw);
		}
		else
			out.lowerYaw = yaw;
		return out;
	}
}
