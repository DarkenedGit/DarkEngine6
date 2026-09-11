#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Dark
{
	enum class AnimPath : uint8_t
	{
		Translation,
		Rotation,
		Scale
	};

	enum class AnimInterp : uint8_t
	{
		Linear,
		Step,
		CubicSpline
	};

	struct AnimMarker
	{
		std::string name;
		float       time = 0.0f;
		int32_t     intPayload = 0;
		float       floatPayload = 0.0f;
	};

	struct AnimChannel
	{
		uint32_t           joint = 0;
		AnimPath           path = AnimPath::Translation;
		AnimInterp         interp = AnimInterp::Linear;
		std::vector<float> times;
		std::vector<float> values;
	};

	struct AnimationClip
	{
		std::string              name;
		float                    duration = 0.0f;
		bool                     loopDefault = true;
		std::vector<AnimChannel> channels;
		std::vector<AnimMarker>  markers;
	};
}
