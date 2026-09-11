#pragma once

#include "Animation/AnimNotify.h"
#include "Animation/AnimationSet.h"
#include "Animation/Pose.h"
#include "Animation/Skeleton.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <string_view>

namespace Dark
{
	class AnimPlayer
	{
	public:
		static constexpr uint32_t kInvalidClip = ~0u;

		void bind(const Skeleton* skel, const AnimationSet* set);
		bool play(std::string_view clipName, float blendSec, bool restart = false);
		bool playIndex(uint32_t clipIndex, float blendSec, bool restart = false);
		void setSpeed(float s);
		void setLoopOverride(int loop);
		void setApplyRootMotion(bool apply);
		bool applyRootMotion() const { return m_applyRootMotion; }
		void stop();
		void update(float dt, AnimNotifyQueue& outNotifies, const AnimMarker* overlay = nullptr, uint32_t overlayCount = 0);

		bool addListener(AnimNotifyFn fn, void* user);

		bool        playing() const { return m_playing; }
		bool        finished() const { return m_finished && !blending(); }
		float       time() const { return m_time; }
		const char* clipName() const;
		const AnimPose& pose() const { return m_pose; }
		uint32_t    incomingClip() const { return m_incoming; }
		uint32_t    outgoingClip() const { return blending() ? m_outgoing : kInvalidClip; }
		float       blendAlpha() const { return blending() ? m_blendAlpha : 1.0f; }
		Math::Vector3f rootMotionDelta() const { return m_rootMotionDelta; }

	private:
		bool blending() const { return m_outgoing != kInvalidClip; }
		bool clipLoops(const AnimationClip& clip) const;
		void sampleClipOrRest(const AnimationClip* clip, float t, Math::Vector3f* T, Math::Quaternion* R, Math::Vector3f* S) const;
		void evalPose(const Math::Vector3f* T, const Math::Quaternion* R, const Math::Vector3f* S);
		Math::Vector3f rootTranslation(const Math::Vector3f* T, const Math::Quaternion* R, const Math::Vector3f* S) const;
		void cacheIncomingRootEndpoints();
		void collectNotifies(const AnimationClip& clip, uint32_t clipIndex, float prevT, float newT, bool looped,
			const AnimMarker* overlay, uint32_t overlayCount, AnimNotifyQueue& q) const;
		void dispatch(const AnimNotifyQueue& q);

		const Skeleton*     m_skel = nullptr;
		const AnimationSet* m_set = nullptr;
		AnimPose            m_pose{};
		uint32_t            m_incoming = kInvalidClip;
		uint32_t            m_outgoing = kInvalidClip;
		float               m_time = 0.0f;
		float               m_prevTime = 0.0f;
		float               m_outTime = 0.0f;
		float               m_speed = 1.0f;
		float               m_blendAlpha = 1.0f;
		float               m_blendSec = 0.0f;
		int                 m_loopOverride = -1;
		bool                m_playing = false;
		bool                m_finished = false;
		bool                m_applyRootMotion = false;
		bool                m_rootPrimed = false;
		Math::Vector3f      m_rootMotionDelta{ 0.0f, 0.0f, 0.0f };
		Math::Vector3f      m_prevRootPos{ 0.0f, 0.0f, 0.0f };
		Math::Vector3f      m_clipStartRoot{ 0.0f, 0.0f, 0.0f };
		Math::Vector3f      m_clipEndRoot{ 0.0f, 0.0f, 0.0f };
		AnimNotifyListener  m_listeners[kMaxAnimNotifyListeners]{};
		uint32_t            m_listenerCount = 0;
	};
}
