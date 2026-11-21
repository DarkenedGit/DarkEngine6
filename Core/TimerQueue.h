#pragma once

#include <cstdint>
#include <memory>

namespace Dark
{

    using TimerFn = void (*)(void* user) noexcept;

    enum class TimerMode : uint8_t
    {
        Once,
        Loop,
    };

    struct TimerHandle
    {
        uint32_t index      = 0; // 0 = none. Slot 0 is reserved.
        uint32_t generation = 0;

        bool valid() const noexcept { return index != 0; }
        friend bool operator==(TimerHandle, TimerHandle) = default;
    };

    struct TimerDesc
    {
        double      delaySec = 0.0; // Once: delay. Loop: interval. First loop fire is one interval later.
        TimerMode   mode     = TimerMode::Once;
        TimerFn     fn       = nullptr;
        void*       user     = nullptr;
        const char* name     = nullptr; // optional, non-owning, for the rejection log only
    };

    class TimerQueue
    {
    public:
        static constexpr int kDefaultCapacity = 256;
        static constexpr int kHardCap         = 4096;

        explicit TimerQueue(int capacity = kDefaultCapacity);
        ~TimerQueue();

        TimerQueue(const TimerQueue&)            = delete;
        TimerQueue& operator=(const TimerQueue&) = delete;
        TimerQueue(TimerQueue&&)                 = delete;
        TimerQueue& operator=(TimerQueue&&)      = delete;

        TimerHandle schedule(const TimerDesc& desc);
        bool        cancel(TimerHandle handle);
        bool        alive(TimerHandle handle) const;
        void        tick(double dtSeconds);
        void        clear(); // no callbacks
        int         liveCount() const;
        int         capacity() const;

    private:
        struct State;
        std::unique_ptr<State> m_state;
    };

    template <typename T, void (T::*Method)() noexcept>
    void timerThunk(void* user) noexcept
    {
        (static_cast<T*>(user)->*Method)();
    }

    // Fills a TimerDesc with timerThunk. Allocates nothing.
    template <typename T, void (T::*Method)() noexcept>
    TimerHandle scheduleMember(TimerQueue& queue, T& object, double delaySec, TimerMode mode, const char* name = nullptr)
    {
        TimerDesc desc{};
        desc.delaySec = delaySec;
        desc.mode     = mode;
        desc.fn       = &timerThunk<T, Method>;
        desc.user     = &object;
        desc.name     = name;
        return queue.schedule(desc);
    }

    class TimerGuard
    {
    public:
        TimerGuard() noexcept = default;
        TimerGuard(TimerQueue& queue, TimerHandle handle) noexcept;
        ~TimerGuard();

        TimerGuard(const TimerGuard&)            = delete;
        TimerGuard& operator=(const TimerGuard&) = delete;
        TimerGuard(TimerGuard&& other) noexcept;
        TimerGuard& operator=(TimerGuard&& other) noexcept;

        void        reset() noexcept;
        TimerHandle handle() const noexcept { return m_handle; }
        bool        armed() const noexcept { return m_queue != nullptr; }

    private:
        TimerQueue* m_queue = nullptr;
        TimerHandle m_handle{};
    };

    inline TimerGuard::TimerGuard(TimerQueue& queue, TimerHandle handle) noexcept
        : m_queue(&queue)
        , m_handle(handle)
    {
    }

    inline TimerGuard::~TimerGuard()
    {
        reset();
    }

    inline TimerGuard::TimerGuard(TimerGuard&& other) noexcept
        : m_queue(other.m_queue)
        , m_handle(other.m_handle)
    {
        other.m_queue  = nullptr;
        other.m_handle = {};
    }

    inline TimerGuard& TimerGuard::operator=(TimerGuard&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_queue        = other.m_queue;
            m_handle       = other.m_handle;
            other.m_queue  = nullptr;
            other.m_handle = {};
        }
        return *this;
    }

    inline void TimerGuard::reset() noexcept
    {
        if (m_queue)
            m_queue->cancel(m_handle);
        m_queue  = nullptr;
        m_handle = {};
    }

} // namespace Dark
