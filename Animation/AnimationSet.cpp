#include "Animation/AnimationSet.h"
#include "Assets/GltfLoader.h"

namespace Dark
{
	AnimationSet::AnimationSet()
	{
		type = AssetType::AnimationSet;
	}

	void AnimationSet::setFromParsed(const GltfCpuModel& cpu)
	{
		m_clips = cpu.clips;
		m_jointNames.clear();
		m_jointNames.reserve(cpu.skeleton.joints.size());
		for (const Joint& j : cpu.skeleton.joints)
			m_jointNames.push_back(j.name);
	}

	void AnimationSet::setClips(std::vector<AnimationClip> clips)
	{
		m_clips = std::move(clips);
	}

	const AnimationClip* AnimationSet::findClip(std::string_view name) const
	{
		for (const AnimationClip& clip : m_clips)
		{
			if (clip.name == name)
				return &clip;
		}
		return nullptr;
	}

	const AnimationClip* AnimationSet::clipAt(uint32_t index) const
	{
		if (index >= m_clips.size())
			return nullptr;
		return &m_clips[index];
	}

	int32_t AnimationSet::findClipIndex(std::string_view name) const
	{
		for (uint32_t i = 0; i < m_clips.size(); ++i)
		{
			if (m_clips[i].name == name)
				return static_cast<int32_t>(i);
		}
		return -1;
	}
}
