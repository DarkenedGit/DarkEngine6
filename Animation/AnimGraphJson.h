#pragma once

#include "Animation/AnimGraph.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark
{
	// CPU parse. Does not assign out.animSet (caller attaches the strong ref).
	// Unknown state/clip names, discarded JSON, or >64 sidecar notifies → false.
	bool parseAnimGraphJson(const char* jsonText, const AnimationSet& animSet, AnimGraphDef& out);

	bool peekAnimGraphModelPath(const char* jsonText, std::string& outModelPath);

	// One looping/non-looping state per clip. Does not assign out.animSet.
	bool initAnimGraphFromSet(AnimGraphDef& out, const AnimationSet& animSet, std::string_view modelPath);

	bool writeAnimGraphJson(const AnimGraphDef& def, std::string& outText);
	bool saveAnimGraphJsonFile(const AnimGraphDef& def, const std::filesystem::path& path);
}
