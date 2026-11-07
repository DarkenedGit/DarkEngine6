#pragma once
#include "Core/Log.h"
#include <atomic>
#include <thread>

namespace Dark
{

    namespace MainThreadDetail
    {

        struct State
        {
            std::thread::id   id{};
            std::atomic<bool> bound{false};
        };

        inline State& state()
        {
            static State s;
            return s;
        }

    } // namespace MainThreadDetail

    inline void bindMainThread()
    {
        MainThreadDetail::State& state = MainThreadDetail::state();
        state.id = std::this_thread::get_id();
        state.bound.store(true, std::memory_order_release);
    }

    inline bool isMainThread()
    {
        MainThreadDetail::State& state = MainThreadDetail::state();
        if (!state.bound.load(std::memory_order_acquire))
            return false;
        return std::this_thread::get_id() == state.id;
    }

    inline void assertMainThread()
    {
        // Unbound means "no token yet", not "wrong thread". Tests that never bind stay quiet.
        if (!MainThreadDetail::state().bound.load(std::memory_order_acquire))
            return;
        DE_ASSERT(isMainThread());
    }

} // namespace Dark
