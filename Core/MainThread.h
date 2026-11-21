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

    inline void unbindMainThread()
    {
        MainThreadDetail::state().bound.store(false, std::memory_order_release);
    }

    inline bool isMainThread()
    {
        MainThreadDetail::State& state = MainThreadDetail::state();
        if (!state.bound.load(std::memory_order_acquire))
            return false;
        return std::this_thread::get_id() == state.id;
    }

    // False until bindMainThread. isMainThread is also false then, so callers that must
    // allow unbound tests use this to tell "never bound" from "bound to someone else".
    inline bool mainThreadBound()
    {
        return MainThreadDetail::state().bound.load(std::memory_order_acquire);
    }

    inline void assertMainThread()
    {
        // Unbound means "no token yet", not "wrong thread". Tests that never bind stay quiet.
        if (!MainThreadDetail::state().bound.load(std::memory_order_acquire))
            return;
        DE_ASSERT(isMainThread());
    }

} // namespace Dark
