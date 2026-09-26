#include "Animation/AnimGraph.h"
#include "Core/Log.h"

namespace Dark
{
	AnimGraphDef::AnimGraphDef()
	{
		type = AssetType::AnimGraph;
	}

	uint32_t AnimGraphDef::overlayForClip(uint32_t clipIndex, AnimMarker* out, uint32_t maxOut) const
	{
		uint32_t n = 0;
		const uint32_t count = static_cast<uint32_t>(
			overlayMarkers.size() < overlayClipIndex.size() ? overlayMarkers.size() : overlayClipIndex.size());
		for (uint32_t i = 0; i < count && n < maxOut; ++i)
		{
			if (overlayClipIndex[i] != clipIndex)
				continue;
			if (out)
				out[n] = overlayMarkers[i];
			++n;
		}
		return n;
	}

	int32_t AnimGraphInstance::findParam(std::string_view name) const
	{
		if (!m_def)
			return -1;
		for (uint32_t i = 0; i < m_def->params.size(); ++i)
		{
			if (m_def->params[i].name == name)
				return static_cast<int32_t>(i);
		}
		return -1;
	}

	bool AnimGraphInstance::bind(const AnimGraphDef* def, const Skeleton* skel)
	{
		m_def = nullptr;
		m_skel = nullptr;
		m_started = false;
		m_state = 0;
		m_floats.clear();
		m_bools.clear();
		m_player.bind(nullptr, nullptr);
		if (!def || !skel || !def->animSet || def->states.empty())
			return false;
		m_def = def;
		m_skel = skel;
		m_floats.resize(def->params.size(), 0.0f);
		m_bools.resize(def->params.size(), 0);
		for (uint32_t i = 0; i < def->params.size(); ++i)
		{
			m_floats[i] = def->params[i].defaultFloat;
			m_bools[i] = def->params[i].defaultBool ? 1 : 0;
		}
		m_player.bind(skel, def->animSet.get());
		m_state = def->defaultState;
		m_requestedState = def->defaultState;
		m_path.clear();
		m_pathIndex = 0;
		m_lockState = false;
		return true;
	}

	void AnimGraphInstance::setApplyRootMotion(bool apply)
	{
		m_player.setApplyRootMotion(apply);
	}

	bool AnimGraphInstance::setFloat(std::string_view name, float v)
	{
		const int32_t i = findParam(name);
		if (i < 0 || m_def->params[static_cast<uint32_t>(i)].type != AnimParamType::Float)
			return false;
		m_floats[static_cast<uint32_t>(i)] = v;
		return true;
	}

	bool AnimGraphInstance::setBool(std::string_view name, bool v)
	{
		const int32_t i = findParam(name);
		if (i < 0)
			return false;
		const AnimParamType t = m_def->params[static_cast<uint32_t>(i)].type;
		if (t != AnimParamType::Bool && t != AnimParamType::Trigger)
			return false;
		m_bools[static_cast<uint32_t>(i)] = v ? 1 : 0;
		return true;
	}

	bool AnimGraphInstance::setTrigger(std::string_view name)
	{
		const int32_t i = findParam(name);
		if (i < 0 || m_def->params[static_cast<uint32_t>(i)].type != AnimParamType::Trigger)
			return false;
		m_bools[static_cast<uint32_t>(i)] = 1;
		return true;
	}

	float AnimGraphInstance::getFloat(std::string_view name, float fallback) const
	{
		const int32_t i = findParam(name);
		if (i < 0)
			return fallback;
		if (m_def->params[static_cast<uint32_t>(i)].type == AnimParamType::Float)
			return m_floats[static_cast<uint32_t>(i)];
		return m_bools[static_cast<uint32_t>(i)] ? 1.0f : 0.0f;
	}

	bool AnimGraphInstance::getBool(std::string_view name, bool fallback) const
	{
		const int32_t i = findParam(name);
		if (i < 0)
			return fallback;
		if (m_def->params[static_cast<uint32_t>(i)].type == AnimParamType::Float)
			return m_floats[static_cast<uint32_t>(i)] != 0.0f;
		return m_bools[static_cast<uint32_t>(i)] != 0;
	}

	const char* AnimGraphInstance::currentStateName() const
	{
		if (!m_def || m_state >= m_def->states.size())
			return "";
		return m_def->states[m_state].name.c_str();
	}

	void AnimGraphInstance::setPreviewPaused(bool paused)
	{
		m_previewPaused = paused;
		applyPreviewSpeed();
	}

	void AnimGraphInstance::setPreviewSpeedScale(float scale)
	{
		m_previewSpeedScale = (scale < 0.0f) ? 0.0f : scale;
		applyPreviewSpeed();
	}

	void AnimGraphInstance::setPlaybackScale(float scale)
	{
		m_playbackScale = (scale < 0.0f) ? 0.0f : scale;
		applyPreviewSpeed();
	}

	void AnimGraphInstance::applyPreviewSpeed()
	{
		float spd = 1.0f;
		if (m_def && m_state < m_def->states.size())
			spd = m_def->states[m_state].speed;
		if (m_previewPaused)
			spd = 0.0f;
		else
			spd *= m_previewSpeedScale * m_playbackScale;
		m_player.setSpeed(spd);
	}

	void AnimGraphInstance::setStateLocked(bool locked)
	{
		m_lockState = locked;
		if (!locked)
			clearPath();
	}

	void AnimGraphInstance::clearPath()
	{
		m_path.clear();
		m_pathIndex = 0;
	}

	uint32_t AnimGraphInstance::pendingPathTransition(uint32_t i) const
	{
		if (i >= m_path.size())
			return ~0u;
		return m_path[i];
	}

	void AnimGraphInstance::refreshParams()
	{
		if (!m_def)
			return;
		const uint32_t n = static_cast<uint32_t>(m_def->params.size());
		const uint32_t oldN = static_cast<uint32_t>(m_floats.size());
		m_floats.resize(n, 0.0f);
		m_bools.resize(n, 0);
		for (uint32_t i = oldN; i < n; ++i)
		{
			m_floats[i] = m_def->params[i].defaultFloat;
			m_bools[i] = m_def->params[i].defaultBool ? 1 : 0;
		}
	}

	bool AnimGraphInstance::rebindKeepingState()
	{
		const uint32_t old = m_state;
		const bool locked = m_lockState;
		const bool paused = m_previewPaused;
		const float scale = m_previewSpeedScale;
		const float playback = m_playbackScale;
		const AnimGraphDef* def = m_def;
		const Skeleton* skel = m_skel;
		if (!bind(def, skel))
			return false;
		m_previewPaused = paused;
		m_previewSpeedScale = scale;
		m_playbackScale = playback;
		m_lockState = locked;
		evaluate();
		if (old < def->states.size() && old != m_state)
			enterState(old, 0.0f);
		m_requestedState = (old < def->states.size()) ? old : m_state;
		return true;
	}

	bool AnimGraphInstance::conditionPasses(const AnimCondition& c) const
	{
		if (c.paramIndex >= m_def->params.size())
			return false;
		const AnimParamType type = m_def->params[c.paramIndex].type;
		if (type == AnimParamType::Float)
		{
			const float v = m_floats[c.paramIndex];
			switch (c.op)
			{
			case AnimCondition::Op::Gt: return v > c.floatValue;
			case AnimCondition::Op::Lt: return v < c.floatValue;
			case AnimCondition::Op::Ge: return v >= c.floatValue;
			case AnimCondition::Op::Le: return v <= c.floatValue;
			case AnimCondition::Op::Eq: return v == c.floatValue;
			case AnimCondition::Op::Ne: return v != c.floatValue;
			}
			return false;
		}
		const bool v = m_bools[c.paramIndex] != 0;
		if (c.op == AnimCondition::Op::Eq)
			return v == c.boolValue;
		if (c.op == AnimCondition::Op::Ne)
			return v != c.boolValue;
		return false;
	}

	bool AnimGraphInstance::transitionMatches(const AnimTransitionDef& t) const
	{
		if (t.from != kAnyState && t.from != m_state)
			return false;
		if (t.to >= m_def->states.size())
			return false;
		if (t.onClipEnd)
		{
			if (m_state < m_def->states.size() && m_def->states[m_state].loop)
				return false;
			if (!m_player.finished())
				return false;
		}
		if (m_player.outgoingClip() != AnimPlayer::kInvalidClip && !t.canInterrupt)
			return false;
		for (const AnimCondition& c : t.when)
		{
			if (!conditionPasses(c))
				return false;
		}
		return true;
	}

	void AnimGraphInstance::enterState(uint32_t stateIndex, float blendSec)
	{
		if (!m_def || stateIndex >= m_def->states.size())
			return;
		const AnimStateDef& st = m_def->states[stateIndex];
		m_player.setLoopOverride(st.loop ? 1 : 0);
		m_player.playIndex(st.clipIndex, blendSec, false);
		m_state = stateIndex;
		applyPreviewSpeed();
	}

	void AnimGraphInstance::consumeTriggers(const AnimTransitionDef& t)
	{
		for (const AnimCondition& c : t.when)
		{
			if (c.paramIndex >= m_def->params.size())
				continue;
			if (m_def->params[c.paramIndex].type == AnimParamType::Trigger)
				m_bools[c.paramIndex] = 0;
		}
	}

	void AnimGraphInstance::advancePath(bool interruptBlend)
	{
		if (!m_def || m_pathIndex >= m_path.size())
		{
			m_path.clear();
			m_pathIndex = 0;
			return;
		}
		const uint32_t ti = m_path[m_pathIndex];
		if (ti >= m_def->transitions.size())
		{
			clearPath();
			return;
		}
		const AnimTransitionDef& t = m_def->transitions[ti];
		if (!interruptBlend && m_player.outgoingClip() != AnimPlayer::kInvalidClip)
			return;
		if (t.onClipEnd && !m_player.finished())
			return;
		enterState(t.to, t.blendSec);
		++m_pathIndex;
		if (m_pathIndex >= m_path.size())
			clearPath();
	}

	bool AnimGraphInstance::requestState(uint32_t stateIndex)
	{
		if (!m_def || !m_skel || stateIndex >= m_def->states.size())
			return false;
		m_requestedState = stateIndex;
		m_lockState = true;
		clearPath();
		if (!m_started)
		{
			enterState(m_def->defaultState, 0.0f);
			m_started = true;
		}
		if (stateIndex == m_state && m_player.outgoingClip() == AnimPlayer::kInvalidClip)
		{
			const AnimStateDef& st = m_def->states[stateIndex];
			m_player.setLoopOverride(st.loop ? 1 : 0);
			m_player.playIndex(st.clipIndex, 0.0f, true);
			applyPreviewSpeed();
			return true;
		}
		if (findAnimStatePath(*m_def, m_state, stateIndex, m_path) && !m_path.empty())
		{
			m_pathIndex = 0;
			advancePath(true);
			return true;
		}
		if (stateIndex != m_state)
			enterState(stateIndex, 0.15f);
		return true;
	}

	bool AnimGraphInstance::requestStateByName(std::string_view name)
	{
		if (!m_def)
			return false;
		for (uint32_t i = 0; i < m_def->states.size(); ++i)
		{
			if (m_def->states[i].name == name)
				return requestState(i);
		}
		return false;
	}

	void AnimGraphInstance::evaluate()
	{
		if (!m_def || !m_skel || m_def->states.empty())
			return;
		if (!m_started)
		{
			enterState(m_def->defaultState, 0.0f);
			m_started = true;
			applyPreviewSpeed();
			return;
		}
		if (pathPending())
		{
			advancePath(false);
			applyPreviewSpeed();
			return;
		}
		if (m_lockState)
		{
			applyPreviewSpeed();
			return;
		}
		for (const AnimTransitionDef& t : m_def->transitions)
		{
			if (!transitionMatches(t))
				continue;
			enterState(t.to, t.blendSec);
			consumeTriggers(t);
			break;
		}
		applyPreviewSpeed();
	}

	bool findAnimStatePath(const AnimGraphDef& def, uint32_t from, uint32_t to, std::vector<uint32_t>& outTransitions)
	{
		outTransitions.clear();
		const uint32_t n = static_cast<uint32_t>(def.states.size());
		if (from >= n || to >= n)
			return false;
		if (from == to)
			return true;

		std::vector<int32_t> prevTrans(n, -1);
		std::vector<int32_t> prevState(n, -1);
		std::vector<uint8_t> seen(n, 0);
		std::vector<uint32_t> queue;
		queue.push_back(from);
		seen[from] = 1;

		uint32_t qh = 0;
		while (qh < queue.size())
		{
			const uint32_t s = queue[qh++];
			for (uint32_t ti = 0; ti < def.transitions.size(); ++ti)
			{
				const AnimTransitionDef& t = def.transitions[ti];
				if (t.to >= n || t.to == s)
					continue;
				if (t.from != s && t.from != kAnyState)
					continue;
				if (seen[t.to])
					continue;
				seen[t.to] = 1;
				prevTrans[t.to] = static_cast<int32_t>(ti);
				prevState[t.to] = static_cast<int32_t>(s);
				if (t.to == to)
				{
					std::vector<uint32_t> rev;
					uint32_t cur = to;
					while (cur != from)
					{
						rev.push_back(static_cast<uint32_t>(prevTrans[cur]));
						cur = static_cast<uint32_t>(prevState[cur]);
					}
					outTransitions.resize(rev.size());
					for (uint32_t i = 0; i < rev.size(); ++i)
						outTransitions[i] = rev[rev.size() - 1 - i];
					return true;
				}
				queue.push_back(t.to);
			}
		}
		return false;
	}
}
