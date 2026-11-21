#include "Core/TimerQueue.h"

#include "Core/Log.h"
#include "Core/MainThread.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Dark
{

    namespace
    {

        uint32_t nextGeneration(uint32_t generation)
        {
            uint32_t next = generation + 1u;
            if (next == 0u)
                next = 1u;
            return next;
        }

        void reject(const char* name, const char* reason)
        {
            if (name)
                DE_LOG_ERROR(LogCategory::Core, "TimerQueue: {} ({})", reason, name);
            else
                DE_LOG_ERROR(LogCategory::Core, "TimerQueue: {}", reason);
        }

        // Release still refuses. assertMainThread compiles out, and isMainThread is false when unbound.
        bool blockedThread(const char* name)
        {
            assertMainThread();
            if (!mainThreadBound() || isMainThread())
                return false;
            reject(name, "thread");
            return true;
        }

    } // namespace

    struct TimerQueue::State
    {
        struct Slot
        {
            TimerFn     fn         = nullptr;
            void*       user       = nullptr;
            const char* name       = nullptr;
            double      due        = 0.0;
            double      interval   = 0.0;
            uint64_t    stamp      = 0;
            uint32_t    generation = 1;
            uint32_t    next       = 0;
            uint32_t    prev       = 0;
            uint32_t    nextFree   = 0;
            bool        looping    = false;
            bool        live       = false;
        };

        std::vector<Slot>     slots;
        std::vector<uint32_t> retire;
        uint32_t              freeHead = 0;
        uint32_t              liveHead = 0;
        uint32_t              liveTail = 0;
        double                now      = 0.0;
        uint64_t              epoch    = 0;
        bool                  inTick   = false;
        int                   live     = 0;
        int                   capacity = 0;

        // keepNext: an in-tick cancel must leave next readable so the walk can skip the dead slot.
        // The slot is not recycled until tick returns, so that index cannot be handed out again yet.
        void unlinkLive(uint32_t index, bool keepNext)
        {
            Slot&          slot = slots[index];
            const uint32_t next = slot.next;
            const uint32_t prev = slot.prev;
            if (prev != 0u)
                slots[prev].next = next;
            else
                liveHead = next;
            if (next != 0u)
                slots[next].prev = prev;
            else
                liveTail = prev;
            slot.prev = 0u;
            if (!keepNext)
                slot.next = 0u;
        }

        void pushFree(uint32_t index)
        {
            Slot& slot     = slots[index];
            slot.fn        = nullptr;
            slot.user      = nullptr;
            slot.name      = nullptr;
            slot.due       = 0.0;
            slot.interval  = 0.0;
            slot.stamp     = 0;
            slot.looping   = false;
            slot.live      = false;
            slot.next      = 0u;
            slot.prev      = 0u;
            slot.nextFree  = freeHead;
            freeHead       = index;
        }

        uint32_t popFree()
        {
            const uint32_t index = freeHead;
            if (index == 0u)
                return 0u;
            freeHead              = slots[index].nextFree;
            slots[index].nextFree = 0u;
            return index;
        }

        void appendLive(uint32_t index)
        {
            Slot& slot = slots[index];
            slot.prev  = liveTail;
            slot.next  = 0u;
            if (liveTail != 0u)
                slots[liveTail].next = index;
            else
                liveHead = index;
            liveTail = index;
            slot.live = true;
        }

        void retireOrFree(uint32_t index)
        {
            Slot& slot      = slots[index];
            slot.fn         = nullptr;
            slot.user       = nullptr;
            slot.live       = false;
            slot.generation = nextGeneration(slot.generation);
            if (inTick)
                retire.push_back(index);
            else
                pushFree(index);
        }

        void checkPartition() const
        {
#if DE_ENABLE_ASSERTS
            int      freeCount = 0;
            uint32_t index     = freeHead;
            while (index != 0u && freeCount <= capacity)
            {
                ++freeCount;
                index = slots[index].nextFree;
            }
            const int retireCount = static_cast<int>(retire.size());
            DE_ASSERT(index == 0u);
            DE_ASSERT(freeCount + live + retireCount == capacity);
#else
            (void)this;
#endif
        }
    };

    TimerQueue::TimerQueue(int capacity)
    {
        if (capacity < 1)
            capacity = 1;
        if (capacity > kHardCap)
            capacity = kHardCap;

        m_state           = std::make_unique<State>();
        m_state->capacity = capacity;
        m_state->slots.resize(static_cast<size_t>(capacity) + 1u);
        m_state->retire.reserve(static_cast<size_t>(capacity));
        for (int i = capacity; i >= 1; --i)
        {
            State::Slot& slot   = m_state->slots[static_cast<size_t>(i)];
            slot.generation     = 1u;
            slot.nextFree       = m_state->freeHead;
            m_state->freeHead   = static_cast<uint32_t>(i);
        }
    }

    TimerQueue::~TimerQueue()
    {
        if (!m_state)
            return;
        const bool foreign = mainThreadBound() && !isMainThread();
        DE_ASSERT(!m_state->inTick);
        DE_ASSERT(!foreign);
        // Still free the slot block. Outstanding handles are dangling once this returns.
    }

    TimerHandle TimerQueue::schedule(const TimerDesc& desc)
    {
        if (blockedThread(desc.name))
            return {};
        if (!desc.fn)
        {
            DE_ASSERT(desc.fn);
            reject(desc.name, "null fn");
            return {};
        }
        if (!std::isfinite(desc.delaySec) || !(desc.delaySec >= 0.0))
        {
            DE_ASSERT(std::isfinite(desc.delaySec) && desc.delaySec >= 0.0);
            reject(desc.name, "bad delay");
            return {};
        }
        if (desc.mode == TimerMode::Loop && !(desc.delaySec > 0.0))
        {
            DE_ASSERT(desc.mode != TimerMode::Loop || desc.delaySec > 0.0);
            reject(desc.name, "loop interval");
            return {};
        }

        State& state = *m_state;
        if (state.live >= state.capacity)
        {
            reject(desc.name, "full");
            return {};
        }
        const uint32_t index = state.popFree();
        if (index == 0u)
        {
            // A loop cancelled during tick sits on the retire list until the walk ends.
            reject(desc.name, "full");
            return {};
        }

        State::Slot& slot = state.slots[index];
        slot.fn           = desc.fn;
        slot.user         = desc.user;
        slot.name         = desc.name;
        slot.interval     = desc.delaySec;
        slot.looping      = desc.mode == TimerMode::Loop;
        slot.due          = state.now + desc.delaySec;
        slot.stamp        = state.epoch;
        state.appendLive(index);
        state.live += 1;
        state.checkPartition();

        TimerHandle handle;
        handle.index      = index;
        handle.generation = slot.generation;
        return handle;
    }

    bool TimerQueue::cancel(TimerHandle handle)
    {
        if (blockedThread(nullptr))
            return false;
        State& state = *m_state;
        if (handle.index == 0u || handle.index >= state.slots.size())
            return false;
        State::Slot& slot = state.slots[handle.index];
        if (!slot.live || slot.generation != handle.generation)
            return false;

        state.unlinkLive(handle.index, state.inTick);
        state.live -= 1;
        state.retireOrFree(handle.index);
        state.checkPartition();
        return true;
    }

    bool TimerQueue::alive(TimerHandle handle) const
    {
        if (blockedThread(nullptr))
            return false;
        const State& state = *m_state;
        if (handle.index == 0u || handle.index >= state.slots.size())
            return false;
        const State::Slot& slot = state.slots[handle.index];
        return slot.live && slot.generation == handle.generation;
    }

    void TimerQueue::tick(double dtSeconds)
    {
        if (blockedThread(nullptr))
            return;
        State& state = *m_state;
        if (state.inTick)
        {
            DE_ASSERT(!state.inTick);
            DE_LOG_ERROR(LogCategory::Core, "TimerQueue: nested tick");
            return;
        }
        if (dtSeconds == 0.0)
            return;
        if (!(dtSeconds > 0.0) || !std::isfinite(dtSeconds))
        {
            DE_ASSERT(dtSeconds > 0.0 && std::isfinite(dtSeconds));
            DE_LOG_ERROR(LogCategory::Core, "TimerQueue: bad dt");
            return;
        }

        state.inTick = true;
        state.epoch += 1u;
        state.now += dtSeconds;

        uint32_t index = state.liveHead;
        while (index != 0u)
        {
            State::Slot&   slot = state.slots[index];
            const uint32_t next = slot.next;
            if (slot.stamp == state.epoch || !slot.live)
            {
                index = next;
                continue;
            }

            slot.stamp = state.epoch;
            if (slot.due > state.now)
            {
                index = next;
                continue;
            }

            const TimerFn fn   = slot.fn;
            void* const   user = slot.user;
            if (slot.looping)
            {
                slot.due += slot.interval;
                if (slot.due <= state.now)
                    slot.due = state.now + slot.interval;
            }
            else
            {
                // Saved next is already copied. This index is not that next, so schedule inside the
                // callback may reuse it. A later cancel of that replacement retires the new occupant.
                state.unlinkLive(index, false);
                state.live -= 1;
                slot.generation = nextGeneration(slot.generation);
                state.pushFree(index);
            }

            if (fn)
                fn(user);
            state.checkPartition();
            index = next;
        }

        state.inTick = false;
        for (uint32_t retired : state.retire)
            state.pushFree(retired);
        state.retire.clear();
        state.checkPartition();
    }

    void TimerQueue::clear()
    {
        if (blockedThread(nullptr))
            return;
        State&   state = *m_state;
        uint32_t index = state.liveHead;
        while (index != 0u)
        {
            const uint32_t next = state.slots[index].next;
            state.unlinkLive(index, state.inTick);
            state.retireOrFree(index);
            index = next;
        }
        state.liveHead = 0u;
        state.liveTail = 0u;
        state.live     = 0;
        state.checkPartition();
    }

    int TimerQueue::liveCount() const
    {
        return m_state->live;
    }

    int TimerQueue::capacity() const
    {
        return m_state->capacity;
    }

} // namespace Dark
