#pragma once

#include "Animation/Pose.h"
#include "Animation/Skeleton.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

#include <vector>

namespace Dark
{
	// Interleaved line-list pairs (a, b, a, b, ...).
	struct SkeletonDebugLines
	{
		std::vector<Math::Vector3f> bones;
		std::vector<Math::Vector3f> axisX;
		std::vector<Math::Vector3f> axisY;
		std::vector<Math::Vector3f> axisZ;
	};

	// World-space overlay from pose.jointWorld * entityWorld.
	// axisLength is world units after entity scale.
	void collectSkeletonDebugLines(
		const Skeleton& skeleton,
		const AnimPose& pose,
		const Math::Matrix4f& entityWorld,
		float axisLength,
		SkeletonDebugLines& out);
}
