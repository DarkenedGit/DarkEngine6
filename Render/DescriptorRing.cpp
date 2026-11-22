#include "Render/DescriptorRing.h"

#include <algorithm>

namespace Dark
{

    bool DescriptorRing::init(uint32_t capacity, uint32_t persistentCount)
    {
        reset();
        if (persistentCount >= capacity)
            return false;
        m_capacity   = capacity;
        m_persistent = persistentCount;
        if (persistentCount > 0)
            m_free.push_back({ 0, persistentCount });
        return true;
    }

    void DescriptorRing::reset()
    {
        m_capacity = m_persistent = 0;
        m_free.clear();
        m_head = m_tail = m_used = m_openUsed = m_highWater = m_failures = 0;
        m_frames.clear();
    }

    uint32_t DescriptorRing::allocatePersistent(uint32_t count)
    {
        if (count == 0)
            return kInvalid;
        for (size_t i = 0; i < m_free.size(); ++i)
        {
            Range& r = m_free[i];
            if (r.count < count)
                continue;
            const uint32_t offset = r.offset;
            r.offset += count;
            r.count -= count;
            if (r.count == 0)
                m_free.erase(m_free.begin() + static_cast<std::ptrdiff_t>(i));
            return offset;
        }
        return kInvalid;
    }

    void DescriptorRing::freePersistent(uint32_t offset, uint32_t count)
    {
        if (count == 0 || offset >= m_persistent || count > m_persistent - offset)
            return;
        auto it = std::lower_bound(m_free.begin(), m_free.end(), offset, [](const Range& r, uint32_t o) { return r.offset < o; });
        it      = m_free.insert(it, { offset, count });
        // Merge with the next range, then with the previous one.
        auto next = it + 1;
        if (next != m_free.end() && it->offset + it->count == next->offset)
        {
            it->count += next->count;
            m_free.erase(next);
        }
        if (it != m_free.begin())
        {
            auto prev = it - 1;
            if (prev->offset + prev->count == it->offset)
            {
                prev->count += it->count;
                m_free.erase(it);
            }
        }
    }

    uint32_t DescriptorRing::persistentFree() const
    {
        uint32_t n = 0;
        for (const Range& r : m_free)
            n += r.count;
        return n;
    }

    uint32_t DescriptorRing::allocateFrame(uint32_t count)
    {
        const uint32_t size = frameCount();
        if (count == 0 || count > size)
        {
            ++m_failures;
            return kInvalid;
        }
        const uint32_t freeCount = size - m_used;
        // Contiguous room at the head: up to the region end when head is at or past the tail (or
        // everything is free), else up to the tail.
        uint32_t offset = kInvalid;
        uint32_t cost   = count;
        if (m_used == 0)
        {
            m_head = m_tail = 0;
            offset          = 0;
        }
        else if (m_head >= m_tail)
        {
            if (size - m_head >= count)
                offset = m_head;
            else if (m_tail >= count)
            {
                // Skip the tail end of the region; the skipped descriptors free with this frame.
                cost   = (size - m_head) + count;
                offset = 0;
            }
        }
        else if (m_tail - m_head >= count)
        {
            offset = m_head;
        }
        if (offset == kInvalid || cost > freeCount)
        {
            ++m_failures;
            return kInvalid;
        }
        m_head = offset + count;
        if (m_head == size)
            m_head = 0;
        m_used += cost;
        m_openUsed += cost;
        m_highWater = std::max(m_highWater, m_used);
        return m_persistent + offset;
    }

    void DescriptorRing::endFrame(uint64_t fence)
    {
        if (m_openUsed == 0)
            return;
        m_frames.push_back({ fence, m_head, m_openUsed });
        m_openUsed = 0;
    }

    void DescriptorRing::retire(uint64_t completedFence)
    {
        size_t done = 0;
        while (done < m_frames.size() && m_frames[done].fence <= completedFence)
        {
            m_tail = m_frames[done].end;
            m_used -= m_frames[done].used;
            ++done;
        }
        m_frames.erase(m_frames.begin(), m_frames.begin() + static_cast<std::ptrdiff_t>(done));
        if (m_used == 0)
            m_head = m_tail = 0;
    }

} // namespace Dark
