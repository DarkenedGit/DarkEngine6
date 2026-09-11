#pragma once

#include "Animation/AnimationClip.h"

#include <cstdint>

namespace Dark
{
	struct AnimNotify
	{
		const char* name = "";
		float       time = 0.0f;
		int32_t     intPayload = 0;
		float       floatPayload = 0.0f;
		uint32_t    clipIndex = 0;
	};

	using AnimNotifyFn = void (*)(void* user, const AnimNotify& n);

	struct AnimNotifyListener
	{
		AnimNotifyFn fn = nullptr;
		void*        user = nullptr;
	};

	struct AnimNotifyQueue
	{
		static constexpr uint32_t kCapacity = 16;

		AnimNotify items[kCapacity]{};
		uint32_t   count = 0;

		bool push(const AnimNotify& n)
		{
			if (count >= kCapacity)
				return false;
			items[count++] = n;
			return true;
		}

		void clear() { count = 0; }
	};

	constexpr uint32_t kMaxAnimNotifyListeners = 8;
}
