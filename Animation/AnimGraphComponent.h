#pragma once

#include "Animation/AnimGraph.h"
#include "ECS/Persist.h"
#include "Assets/Model.h"
#include "Math/Matrix4f.h"

namespace Dark
{
	struct AnimGraphComponent
	{
		static constexpr const char* kTypeName = "AnimGraph";
		static constexpr uint16_t kSaveVersion = 1;
		static const PersistFns kPersist;

		AssetRef<Model>        model;
		AssetRef<AnimationSet> animSet;
		AssetRef<AnimGraphDef> graphDef;

		AnimGraphInstance graph;
		Math::Matrix4f    prevWorld;
		bool              prevWorldValid = false;
		bool              warnedMissing = false;
	};
}
