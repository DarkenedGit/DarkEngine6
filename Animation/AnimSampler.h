#pragma once

#include "Animation/AnimationClip.h"
#include "Animation/Pose.h"
#include "Animation/Skeleton.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

namespace Dark
{
	Math::Matrix4f composeLocal(const Math::Vector3f& t, const Math::Quaternion& r, const Math::Vector3f& s);

	// Seed outT/R/S from node-local rest, then replace with sampled channels.
	// Caller provides arrays of skeleton.joints.size(). timeSec is clamped to the clip.
	bool sampleClipLocal(
		const AnimationClip& clip,
		const Skeleton& skeleton,
		float timeSec,
		Math::Vector3f* outT,
		Math::Quaternion* outR,
		Math::Vector3f* outS);

	void localToJointWorlds(
		const Skeleton& skeleton,
		const Math::Vector3f* T,
		const Math::Quaternion* R,
		const Math::Vector3f* S,
		Math::Matrix4f* outWorlds);

	void localToPalette(
		const Skeleton& skeleton,
		const Math::Vector3f* T,
		const Math::Quaternion* R,
		const Math::Vector3f* S,
		AnimPose& pose);
}
