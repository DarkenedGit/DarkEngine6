#pragma once

#include "Animation/Pose.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Dark
{
	struct Joint
	{
		std::string      name;
		int32_t          parent = -1; // nearest joint ancestor in joints[], or -1
		Math::Vector3f   restT{ 0.0f, 0.0f, 0.0f };
		Math::Quaternion restR = Math::Quaternion::IDENTITY;
		Math::Vector3f   restS{ 1.0f, 1.0f, 1.0f };
		Math::Matrix4f   ancestorBindWorld; // identity if no non-joint gap
		Math::Matrix4f   inverseBind;       // mesh-space IBM
	};

	struct Skeleton
	{
		std::vector<Joint>    joints; // skin->joints order; JOINTS_0 indexes this
		std::vector<uint32_t> fkOrder;
		AnimPose              restPose;
		Math::Matrix4f        meshWorld;
		int32_t               skeletonRoot = -1;
		int32_t               rootMotionJoint = 0;
	};
}
