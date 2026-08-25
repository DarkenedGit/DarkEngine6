#include "Core/MemoryTracker.h"
#include "Debug/DebugProtocol.h"

#include <cstring>
#include <mutex>

namespace Dark
{

    namespace
    {
        struct TrackerState
        {
            std::mutex       mutex;
            DebugMemoryPool  pools[kDebugMaxPools]{};
            uint32_t         count = 0;
        };

        TrackerState& state()
        {
            static TrackerState s;
            return s;
        }

        int findIndexUnlocked(TrackerState& s, const char* name)
        {
            if (!name)
                return -1;
            for (uint32_t i = 0; i < s.count; ++i)
            {
                if (std::strncmp(s.pools[i].name, name, kDebugNameBytes) == 0)
                    return static_cast<int>(i);
            }
            return -1;
        }
    } // namespace

    void MemoryTracker::set(const char* name, uint64_t used, uint64_t capacity, uint32_t count)
    {
        if (!name || name[0] == 0)
            return;
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        const int idx = findIndexUnlocked(s, name);
        if (idx >= 0)
        {
            s.pools[idx].used     = used;
            s.pools[idx].capacity = capacity;
            s.pools[idx].count    = count;
            return;
        }
        if (s.count >= kDebugMaxPools)
            return;
        copyDebugName(s.pools[s.count].name, name);
        s.pools[s.count].used     = used;
        s.pools[s.count].capacity = capacity;
        s.pools[s.count].count    = count;
        ++s.count;
    }

    void MemoryTracker::clear(const char* name)
    {
        if (!name)
            return;
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        const int idx = findIndexUnlocked(s, name);
        if (idx < 0)
            return;
        const uint32_t u = static_cast<uint32_t>(idx);
        if (u + 1 < s.count)
            s.pools[u] = s.pools[s.count - 1];
        s.pools[s.count - 1] = {};
        --s.count;
    }

    void MemoryTracker::snapshot(DebugMemoryPool* out, uint32_t cap, uint32_t& count)
    {
        count = 0;
        if (!out || cap == 0)
            return;
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        const uint32_t n = s.count < cap ? s.count : cap;
        for (uint32_t i = 0; i < n; ++i)
            out[i] = s.pools[i];
        count = n;
    }

    void MemoryTracker::reset()
    {
        auto& s = state();
        std::lock_guard<std::mutex> lock(s.mutex);
        s.count = 0;
        for (auto& p : s.pools)
            p = {};
    }

} // namespace Dark
