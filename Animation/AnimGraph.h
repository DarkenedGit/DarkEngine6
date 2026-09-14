#pragma once

#include "Animation/AnimPlayer.h"
#include "Animation/AnimationClip.h"
#include "Animation/AnimationSet.h"
#include "Animation/Skeleton.h"
#include "Assets/AssetHandle.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{
	constexpr uint32_t kAnyState = 0xFFFFFFFFu;

	enum class AnimParamType : uint8_t
	{
		Float,
		Bool,
		Trigger
	};

	struct AnimParamDef
	{
		std::string    name;
		AnimParamType  type = AnimParamType::Float;
		float          defaultFloat = 0.0f;
		bool           defaultBool = false;
	};

	struct AnimCondition
	{
		uint32_t paramIndex = 0;
		enum class Op : uint8_t
		{
			Gt,
			Lt,
			Ge,
			Le,
			Eq,
			Ne
		} op = Op::Gt;
		float floatValue = 0.0f;
		bool  boolValue = true;
	};

	struct AnimTransitionDef
	{
		uint32_t                   from = 0;
		uint32_t                   to = 0;
		float                      blendSec = 0.15f;
		bool                       canInterrupt = true;
		bool                       onClipEnd = false;
		std::vector<AnimCondition> when;
	};

	struct AnimStateDef
	{
		std::string name;
		uint32_t    clipIndex = 0;
		float       speed = 1.0f;
		bool        loop = true;
	};

	class AnimGraphDef : public Asset
	{
	public:
		AnimGraphDef();

		uint32_t                       defaultState = 0;
		std::string                    modelPath;
		std::vector<AnimParamDef>      params;
		std::vector<AnimStateDef>      states;
		std::vector<AnimTransitionDef> transitions;
		AssetRef<AnimationSet>         animSet;
		std::vector<AnimMarker>        overlayMarkers;
		std::vector<uint32_t>          overlayClipIndex;

		uint32_t overlayForClip(uint32_t clipIndex, AnimMarker* out, uint32_t maxOut) const;
	};

	class AnimGraphInstance
	{
	public:
		bool bind(const AnimGraphDef* def, const Skeleton* skel);
		bool started() const { return m_started; }
		void evaluate();

		bool  setFloat(std::string_view name, float v);
		bool  setBool(std::string_view name, bool v);
		bool  setTrigger(std::string_view name);
		float getFloat(std::string_view name, float fallback = 0.0f) const;
		bool  getBool(std::string_view name, bool fallback = false) const;

		AnimPlayer&       player() { return m_player; }
		const AnimPlayer& player() const { return m_player; }
		void              setApplyRootMotion(bool apply);

		uint32_t            currentState() const { return m_state; }
		const char*         currentStateName() const;
		const AnimGraphDef* def() const { return m_def; }
		const Skeleton*     skeleton() const { return m_skel; }

		// Walk the transition graph from the current state to `stateIndex`, playing each hop's blend.
		// Locks automatic parameter-driven transitions until setStateLocked(false) or a parameter is driven by gameplay.
		bool requestState(uint32_t stateIndex);
		bool requestStateByName(std::string_view name);
		void clearPath();
		void setStateLocked(bool locked);
		bool stateLocked() const { return m_lockState; }
		bool pathPending() const { return !m_path.empty() && m_pathIndex < m_path.size(); }
		uint32_t requestedState() const { return m_requestedState; }
		uint32_t pendingPathLength() const { return static_cast<uint32_t>(m_path.size()); }
		uint32_t pendingPathCursor() const { return m_pathIndex; }
		uint32_t pendingPathTransition(uint32_t i) const;

		void setPreviewPaused(bool paused);
		void setPreviewSpeedScale(float scale);
		bool previewPaused() const { return m_previewPaused; }
		bool rebindKeepingState();
		void refreshParams();

	private:
		int32_t findParam(std::string_view name) const;
		bool    conditionPasses(const AnimCondition& c) const;
		bool    transitionMatches(const AnimTransitionDef& t) const;
		void    enterState(uint32_t stateIndex, float blendSec);
		void    consumeTriggers(const AnimTransitionDef& t);
		void    applyPreviewSpeed();
		void    advancePath(bool interruptBlend);

		const AnimGraphDef* m_def = nullptr;
		const Skeleton*     m_skel = nullptr;
		AnimPlayer          m_player;
		uint32_t            m_state = 0;
		uint32_t            m_requestedState = 0;
		uint32_t            m_pathIndex = 0;
		bool                m_started = false;
		bool                m_lockState = false;
		bool                m_previewPaused = false;
		float               m_previewSpeedScale = 1.0f;
		std::vector<float>  m_floats;
		std::vector<uint8_t> m_bools;
		std::vector<uint32_t> m_path;
	};

	// Shortest hop path through `def.transitions` (including Any-state edges). Empty `outTransitions` if from == to.
	bool findAnimStatePath(const AnimGraphDef& def, uint32_t from, uint32_t to, std::vector<uint32_t>& outTransitions);
}
