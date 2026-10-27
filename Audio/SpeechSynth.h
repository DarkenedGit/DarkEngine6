#pragma once

#include "Audio/WavFile.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace Dark::Audio
{

    struct SpeechResult
    {
        uint64_t id = 0;
        bool     ok = false;
        PcmWav   pcm;
    };

    // Text to speech on a worker thread. Windows uses the installed SAPI voices; other platforms never produce audio.
    class SpeechSynth
    {
    public:
        SpeechSynth() = default;
        ~SpeechSynth();

        SpeechSynth(const SpeechSynth&)            = delete;
        SpeechSynth& operator=(const SpeechSynth&) = delete;

        bool start();
        void stop();
        bool running() const { return m_running.load(); }
        int  voiceCount() const { return m_voiceCount.load(); }

        // rate is -10..10, pitch is -10..10. Returns 0 when the worker is not running.
        uint64_t request(std::string text, int voice, int rate, int pitch);
        bool     poll(SpeechResult& out);

    private:
        struct Job
        {
            uint64_t    id = 0;
            std::string text;
            int         voice = 0;
            int         rate  = 0;
            int         pitch = 0;
        };

        void run();

        std::thread             m_thread;
        std::mutex              m_mutex;
        std::condition_variable m_wake;
        std::deque<Job>         m_jobs;
        std::deque<SpeechResult> m_done;
        std::atomic<bool>       m_running{ false };
        std::atomic<bool>       m_quit{ false };
        std::atomic<int>        m_voiceCount{ 0 };
        uint64_t                m_nextId = 1;
    };

} // namespace Dark::Audio
