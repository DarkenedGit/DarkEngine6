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
		m_player.setSpeed(st.speed);
		m_player.playIndex(st.clipIndex, blendSec, false);
		m_state = stateIndex;
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

	void AnimGraphInstance::evaluate()
	{
		if (!m_def || !m_skel || m_def->states.empty())
			return;
		if (!m_started)
		{
			enterState(m_def->defaultState, 0.0f);
			m_started = true;
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
	}
}
