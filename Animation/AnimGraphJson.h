#pragma once

#include "Animation/AnimGraph.h"

namespace Dark
{
	// CPU parse. Does not assign out.animSet (caller attaches the strong ref).
	// Unknown state/clip names, discarded JSON, or >64 sidecar notifies → false.
	bool parseAnimGraphJson(const char* jsonText, const AnimationSet& animSet, AnimGraphDef& out);

	bool peekAnimGraphModelPath(const char* jsonText, std::string& outModelPath);
}
