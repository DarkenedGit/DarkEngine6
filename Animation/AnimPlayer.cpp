#include "Animation/AnimPlayer.h"
#include "Animation/AnimSampler.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"

#include <cmath>

namespace Dark
{
	using namespace Math;

	void AnimPlayer::bind(const Skeleton* skel, const AnimationSet* set)
	{
		m_skel = skel;
		m_set = set;
		m_incoming = kInvalidClip;
		m_outgoing = kInvalidClip;
		m_time = 0.0f;
		m_prevTime = 0.0f;
		m_outTime = 0.0f;
		m_blendAlpha = 1.0f;
		m_playing = false;
		m_finished = false;
		m_rootPrimed = false;
		m_rootMotionDelta = Vector3f::ZERO;
		if (skel)
			m_pose = skel->restPose;
		else
			m_pose = AnimPose{};
	}

	bool AnimPlayer::clipLoops(const AnimationClip& clip) const
	{
		if (m_loopOverride == 0)
			return false;
		if (m_loopOverride > 0)
			return true;
		return clip.loopDefault;
	}

	void AnimPlayer::sampleClipOrRest(const AnimationClip* clip, float t, Vector3f* T, Quaternion* R, Vector3f* S) const
	{
		const uint32_t n = m_skel ? static_cast<uint32_t>(m_skel->joints.size()) : 0;
		if (!m_skel || n == 0)
			return;
		if (!clip || !sampleClipLocal(*clip, *m_skel, t, T, R, S))
		{
			for (uint32_t i = 0; i < n; ++i)
			{
				T[i] = m_skel->joints[i].restT;
				R[i] = m_skel->joints[i].restR;
				S[i] = m_skel->joints[i].restS;
			}
		}
	}

	void AnimPlayer::evalPose(const Vector3f* T, const Quaternion* R, const Vector3f* S)
	{
		if (!m_skel)
			return;
		const uint32_t n = static_cast<uint32_t>(m_skel->joints.size());
		const uint32_t count = (n > AnimPose::kMaxBones) ? AnimPose::kMaxBones : n;
		for (uint32_t i = 0; i < count; ++i)
			m_pose.prevPalette[i] = m_pose.palette[i];
		localToPalette(*m_skel, T, R, S, m_pose);
		if (!m_rootPrimed)
		{
			for (uint32_t i = 0; i < count; ++i)
				m_pose.prevPalette[i] = m_pose.palette[i];
		}
	}

	Vector3f AnimPlayer::rootTranslation(const Vector3f* T, const Quaternion* R, const Vector3f* S) const
	{
		if (!m_skel || m_skel->joints.empty())
			return Vector3f::ZERO;
		Matrix4f worlds[AnimPose::kMaxBones];
		localToJointWorlds(*m_skel, T, R, S, worlds);
		uint32_t j = static_cast<uint32_t>(m_skel->rootMotionJoint);
		if (j >= m_skel->joints.size())
			j = 0;
		return worlds[j].GetTranslation();
	}

	void AnimPlayer::cacheIncomingRootEndpoints()
	{
		if (!m_skel || !m_set || m_incoming == kInvalidClip)
			return;
		const AnimationClip* clip = m_set->clipAt(m_incoming);
		Vector3f T[AnimPose::kMaxBones];
		Quaternion R[AnimPose::kMaxBones];
		Vector3f S[AnimPose::kMaxBones];
		sampleClipOrRest(clip, 0.0f, T, R, S);
		m_clipStartRoot = rootTranslation(T, R, S);
		const float dur = clip ? clip->duration : 0.0f;
		sampleClipOrRest(clip, dur, T, R, S);
		m_clipEndRoot = rootTranslation(T, R, S);
	}

	bool AnimPlayer::playIndex(uint32_t clipIndex, float blendSec, bool restart)
	{
		if (!m_set || !m_skel)
		{
			DE_LOG_WARN("AnimPlayer: playIndex with no bind");
			return false;
		}
		const AnimationClip* clip = m_set->clipAt(clipIndex);
		if (!clip)
		{
			DE_LOG_WARN("AnimPlayer: unknown clip index {}", clipIndex);
			return false;
		}
		if (!restart && m_playing && m_incoming == clipIndex)
			return true;

		const bool canBlend = blendSec > 0.0f && m_playing && m_incoming != kInvalidClip && m_incoming != clipIndex;
		if (canBlend)
		{
			m_outgoing = m_incoming;
			m_outTime = m_time;
			m_blendSec = blendSec;
			m_blendAlpha = 0.0f;
		}
		else
		{
			m_outgoing = kInvalidClip;
			m_blendAlpha = 1.0f;
			m_blendSec = 0.0f;
		}

		m_incoming = clipIndex;
		m_time = 0.0f;
		m_prevTime = 0.0f;
		m_playing = true;
		m_finished = false;
		cacheIncomingRootEndpoints();
		if (!canBlend)
		{
			m_prevRootPos = m_clipStartRoot;
			m_rootPrimed = true;
			m_rootMotionDelta = Vector3f::ZERO;
		}
		return true;
	}

	bool AnimPlayer::play(std::string_view clipName, float blendSec, bool restart)
	{
		if (!m_set)
		{
			DE_LOG_WARN("AnimPlayer: play with no animation set");
			return false;
		}
		const int32_t idx = m_set->findClipIndex(clipName);
		if (idx < 0)
		{
			DE_LOG_WARN("AnimPlayer: unknown clip '{}'", clipName);
			return false;
		}
		return playIndex(static_cast<uint32_t>(idx), blendSec, restart);
	}

	void AnimPlayer::setSpeed(float s)
	{
		m_speed = (s < 0.0f) ? 0.0f : s;
	}

	void AnimPlayer::setLoopOverride(int loop)
	{
		m_loopOverride = loop;
	}

	void AnimPlayer::setApplyRootMotion(bool apply)
	{
		if (apply && !m_applyRootMotion)
			m_rootPrimed = false;
		m_applyRootMotion = apply;
		if (!apply)
			m_rootMotionDelta = Vector3f::ZERO;
	}

	void AnimPlayer::stop()
	{
		m_playing = false;
		m_finished = false;
		m_outgoing = kInvalidClip;
		m_rootMotionDelta = Vector3f::ZERO;
	}

	const char* AnimPlayer::clipName() const
	{
		if (!m_set || m_incoming == kInvalidClip)
			return "";
		const AnimationClip* clip = m_set->clipAt(m_incoming);
		return clip ? clip->name.c_str() : "";
	}

	bool AnimPlayer::addListener(AnimNotifyFn fn, void* user)
	{
		if (!fn)
			return false;
		if (m_listenerCount >= kMaxAnimNotifyListeners)
		{
			DE_LOG_WARN("AnimPlayer: notify listener list full");
			return false;
		}
		m_listeners[m_listenerCount++] = AnimNotifyListener{ fn, user };
		return true;
	}

	static bool sameMarker(const AnimMarker& a, const AnimMarker& b)
	{
		return a.name == b.name && fabsf(a.time - b.time) <= 1.0e-4f;
	}

	void AnimPlayer::collectNotifies(const AnimationClip& clip, uint32_t clipIndex, float prevT, float newT, bool looped,
		const AnimMarker* overlay, uint32_t overlayCount, AnimNotifyQueue& q) const
	{
		auto overridden = [&](const AnimMarker& m) {
			for (uint32_t i = 0; i < overlayCount; ++i)
			{
				if (sameMarker(m, overlay[i]))
					return true;
			}
			return false;
		};

		auto pushIf = [&](const AnimMarker& m, bool inWindow) {
			if (!inWindow)
				return;
			AnimNotify n;
			n.name = m.name.c_str();
			n.time = m.time;
			n.intPayload = m.intPayload;
			n.floatPayload = m.floatPayload;
			n.clipIndex = clipIndex;
			if (!q.push(n))
				DE_LOG_WARN("AnimPlayer: notify queue overflow, dropping '{}'", m.name);
		};

		auto pass = [&](auto inWindow) {
			for (const AnimMarker& m : clip.markers)
			{
				if (!overridden(m))
					pushIf(m, inWindow(m.time));
			}
			for (uint32_t i = 0; i < overlayCount; ++i)
				pushIf(overlay[i], inWindow(overlay[i].time));
		};

		if (!looped)
		{
			pass([&](float t) { return t > prevT && t <= newT; });
			return;
		}
		pass([&](float t) { return t > prevT && t <= clip.duration; });
		pass([&](float t) { return t >= 0.0f && t <= newT; });
	}

	void AnimPlayer::dispatch(const AnimNotifyQueue& q)
	{
		for (uint32_t i = 0; i < q.count; ++i)
		{
			for (uint32_t l = 0; l < m_listenerCount; ++l)
			{
				if (m_listeners[l].fn)
					m_listeners[l].fn(m_listeners[l].user, q.items[i]);
			}
		}
	}

	void AnimPlayer::update(float dt, AnimNotifyQueue& outNotifies, const AnimMarker* overlay, uint32_t overlayCount)
	{
		outNotifies.clear();
		m_rootMotionDelta = Vector3f::ZERO;
		if (!m_skel || m_skel->joints.empty())
			return;

		const uint32_t n = static_cast<uint32_t>(m_skel->joints.size());
		Vector3f inT[AnimPose::kMaxBones];
		Quaternion inR[AnimPose::kMaxBones];
		Vector3f inS[AnimPose::kMaxBones];

		if (!m_playing || m_incoming == kInvalidClip || !m_set)
		{
			sampleClipOrRest(nullptr, 0.0f, inT, inR, inS);
			evalPose(inT, inR, inS);
			if (m_applyRootMotion && !m_rootPrimed)
			{
				m_prevRootPos = rootTranslation(inT, inR, inS);
				m_rootPrimed = true;
			}
			return;
		}

		const AnimationClip* inClip = m_set->clipAt(m_incoming);
		if (!inClip)
			return;

		const float speed = m_speed;
		const float step = dt * speed;
		const bool looping = clipLoops(*inClip);
		const float dur = inClip->duration;
		m_prevTime = m_time;
		float newT = m_time + step;
		bool looped = false;
		if (looping && dur > Epsilon)
		{
			if (newT >= dur)
			{
				looped = true;
				newT = fmodf(newT, dur);
				if (newT < 0.0f)
					newT += dur;
			}
		}
		else if (dur > Epsilon)
		{
			if (newT >= dur)
			{
				newT = dur;
				m_finished = true;
			}
		}
		else
		{
			newT = 0.0f;
			if (!looping)
				m_finished = true;
		}
		m_time = newT;

		if (blending())
		{
			m_outTime += step;
			const AnimationClip* outClip = m_set->clipAt(m_outgoing);
			if (outClip && clipLoops(*outClip) && outClip->duration > Epsilon)
			{
				m_outTime = fmodf(m_outTime, outClip->duration);
				if (m_outTime < 0.0f)
					m_outTime += outClip->duration;
			}
			else if (outClip && m_outTime > outClip->duration)
				m_outTime = outClip->duration;
			m_blendAlpha += (m_blendSec > Epsilon) ? (dt / m_blendSec) : 1.0f;
			if (m_blendAlpha >= 1.0f)
			{
				m_blendAlpha = 1.0f;
				m_outgoing = kInvalidClip;
			}
		}

		sampleClipOrRest(inClip, m_time, inT, inR, inS);
		Vector3f T[AnimPose::kMaxBones];
		Quaternion R[AnimPose::kMaxBones];
		Vector3f S[AnimPose::kMaxBones];
		if (blending())
		{
			Vector3f outT[AnimPose::kMaxBones];
			Quaternion outR[AnimPose::kMaxBones];
			Vector3f outS[AnimPose::kMaxBones];
			sampleClipOrRest(m_set->clipAt(m_outgoing), m_outTime, outT, outR, outS);
			const float a = m_blendAlpha;
			for (uint32_t i = 0; i < n; ++i)
			{
				T[i] = Lerp(outT[i], inT[i], a);
				R[i] = Quaternion::Slerp(outR[i], inR[i], a);
				S[i] = Lerp(outS[i], inS[i], a);
			}
		}
		else
		{
			for (uint32_t i = 0; i < n; ++i)
			{
				T[i] = inT[i];
				R[i] = inR[i];
				S[i] = inS[i];
			}
		}

		evalPose(T, R, S);

		const Vector3f newRoot = rootTranslation(T, R, S);
		if (m_applyRootMotion)
		{
			if (!m_rootPrimed)
			{
				m_prevRootPos = newRoot;
				m_rootPrimed = true;
			}
			else if (looped)
			{
				m_rootMotionDelta = (m_clipEndRoot - m_prevRootPos) + (newRoot - m_clipStartRoot);
			}
			else
			{
				m_rootMotionDelta = newRoot - m_prevRootPos;
			}
			m_prevRootPos = newRoot;
		}
		else
		{
			m_prevRootPos = newRoot;
			m_rootPrimed = true;
		}

		if (!overlay)
			overlayCount = 0;
		collectNotifies(*inClip, m_incoming, m_prevTime, m_time, looped, overlay, overlayCount, outNotifies);
		dispatch(outNotifies);
	}
}
