#pragma once

#include "Math/Matrix4f.h"
#include <cstdint>

namespace Dark
{
	struct AnimPose
	{
		static constexpr uint32_t kMaxBones = 64;

		Math::Matrix4f palette[kMaxBones];
		Math::Matrix4f prevPalette[kMaxBones];
		Math::Matrix4f jointWorld[kMaxBones]; // armature/model space; debug draw + root motion
		uint32_t       boneCount = 0;
	};
}
