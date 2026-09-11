#pragma once

#include "Animation/AnimGraph.h"
#include "Assets/Model.h"
#include "Math/Matrix4f.h"

namespace Dark
{
	struct AnimGraphComponent
	{
		static constexpr const char* kTypeName = "AnimGraph";

		AssetRef<Model>        model;
		AssetRef<AnimationSet> animSet;
		AssetRef<AnimGraphDef> graphDef;

		AnimGraphInstance graph;
		Math::Matrix4f    prevWorld;
		bool              prevWorldValid = false;
		bool              warnedMissing = false;
	};
}
