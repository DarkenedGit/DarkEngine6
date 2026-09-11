#pragma once

#include "Animation/AnimationClip.h"
#include "Assets/AssetHandle.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{
	struct GltfCpuModel;

	class AnimationSet : public Asset
	{
	public:
		AnimationSet();

		void setFromParsed(const GltfCpuModel& cpu);
		void setClips(std::vector<AnimationClip> clips);

		const std::vector<AnimationClip>& clips() const { return m_clips; }
		const std::vector<std::string>&   jointNames() const { return m_jointNames; }

		uint32_t clipCount() const { return static_cast<uint32_t>(m_clips.size()); }
		bool     empty() const { return m_clips.empty() && m_jointNames.empty(); }

		const AnimationClip* findClip(std::string_view name) const;
		const AnimationClip* clipAt(uint32_t index) const;
		int32_t              findClipIndex(std::string_view name) const;

	private:
		std::vector<AnimationClip> m_clips;
		std::vector<std::string>   m_jointNames;
	};
}
