#pragma once

#include <cstdint>
#include <vector>

// The descriptor ring's bookkeeping (render-graph plan §3.9, Phase 6). Pure C++ and unit-tested;
// DescriptorRingHeap wraps it around one shader-visible CBV/SRV/UAV heap.
//
// The heap is split in two:
// - a persistent region [0, persistentCount): long-lived tables (first fit over a free list,
//   neighbours merged on free);
// - a frame region [persistentCount, capacity): a ring of per-frame tables. allocateFrame() hands
//   out contiguous ranges (a range never wraps; the tail of the region is skipped instead),
//   endFrame(fence) closes the frame's ranges under the fence its list is signalled with, and
//   retire(completedFence) gives them back once the GPU is done with them.
//
// Offsets are descriptor indices from the start of the heap. kInvalid when there is no room.

namespace Dark
{

    class DescriptorRing
    {
    public:
        static constexpr uint32_t kInvalid = 0xFFFFFFFFu;

        // False when the split does not fit (persistent > capacity) or the frame region is empty.
        bool init(uint32_t capacity, uint32_t persistentCount);
        void reset();

        uint32_t capacity() const
        {
            return m_capacity;
        }
        uint32_t persistentCount() const
        {
            return m_persistent;
        }
        uint32_t frameCount() const
        {
            return m_capacity - m_persistent;
        }

        // ---- persistent region
        uint32_t allocatePersistent(uint32_t count);
        void     freePersistent(uint32_t offset, uint32_t count);
        uint32_t persistentFree() const; // descriptors not handed out

        // ---- frame region
        uint32_t allocateFrame(uint32_t count);
        // Closes the ranges allocated since the last endFrame under `fence` (the value the list that
        // reads them is signalled with). Fences must not decrease.
        void endFrame(uint64_t fence);
        // Frees every closed frame whose fence the GPU completed.
        void retire(uint64_t completedFence);
        uint32_t frameUsed() const // descriptors in flight or allocated this frame
        {
            return m_used;
        }
        uint32_t framesInFlight() const
        {
            return static_cast<uint32_t>(m_frames.size());
        }
        uint32_t highWater() const // most frame descriptors in use at once
        {
            return m_highWater;
        }
        uint32_t failures() const // allocateFrame calls that found no room
        {
            return m_failures;
        }

    private:
        struct Range
        {
            uint32_t offset = 0;
            uint32_t count  = 0;
        };
        struct Frame
        {
            uint64_t fence = 0;
            uint32_t end   = 0; // ring position after the frame's last range
            uint32_t used  = 0; // descriptors it holds (including a skipped tail)
        };

        uint32_t           m_capacity   = 0;
        uint32_t           m_persistent = 0;
        std::vector<Range> m_free; // persistent free list, sorted by offset

        // Frame ring positions are relative to the region start (0 .. frameCount()).
        uint32_t           m_head      = 0; // next allocation
        uint32_t           m_tail      = 0; // oldest descriptor still in use
        uint32_t           m_used      = 0;
        uint32_t           m_openUsed  = 0; // used by the open (not yet ended) frame
        uint32_t           m_highWater = 0;
        uint32_t           m_failures  = 0;
        std::vector<Frame> m_frames; // closed frames, oldest first
    };

} // namespace Dark
