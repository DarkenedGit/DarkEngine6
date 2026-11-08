#pragma once

#include "Media/MovieSource.h"
#include "Media/MovieStatus.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Dark
{

    struct MovieDecodePcmTestAccess;

    class Renderer;
    class AssetManager;
    class MovieFrameTexture;
    class MoviePresentPipeline;

    namespace Audio
    {
        class AudioSystem;
    }

    // Filled by the host on each call. Pointers are not owned.
    struct MovieHost
    {
        Renderer*           renderer = nullptr;
        Audio::AudioSystem* audio    = nullptr;
        AssetManager*       assets   = nullptr;

        MovieHost()                            = default;
        MovieHost(const MovieHost&)            = delete;
        MovieHost& operator=(const MovieHost&) = delete;
        MovieHost(MovieHost&&)                 = delete;
        MovieHost& operator=(MovieHost&&)      = delete;
    };

    struct MovieStats
    {
        uint64_t framesDecoded      = 0;
        uint64_t framesPresented    = 0;
        uint64_t framesDropped      = 0;
        uint32_t audioUnderruns     = 0;
        uint32_t videoQueueDepth    = 0;
        uint32_t audioQueueDepth    = 0;
        uint32_t audioBuffersQueued = 0;
        double   clockSeconds       = 0.0;
        double   lastPresentPts     = 0.0;
        float    decodeMsAvg        = 0.0f;
    };

    // One movie. IMFSourceReader stays on the decode thread. Does not own the host.
    class MoviePlayer
    {
        friend struct MovieDecodePcmTestAccess;

    public:
        MoviePlayer();
        ~MoviePlayer();

        MoviePlayer(const MoviePlayer&)            = delete;
        MoviePlayer& operator=(const MoviePlayer&) = delete;
        MoviePlayer(MoviePlayer&&)                 = delete;
        MoviePlayer& operator=(MoviePlayer&&)      = delete;

        void bind(void (*fill)(void* user, MovieHost& host), void* user);

        MovieStatus open(const char* virtualPath);
        void        close();

        void play();
        void pause();
        void stop();
        void seek(double seconds);
        void setLoop(bool loop);
        void setVolume(float volume);

        // dt is ignored. The clock is the stream voice until that voice drains, then steady_clock.
        void tick(float dt);

        // Caller has already tonemapped. This pass does not. opacity scales the picture (1 = full).
        void draw(Renderer& renderer, float opacity = 1.f);

        bool           blocksWorld() const;
        MoviePlayState state() const;
        MovieStats     stats() const;
        bool           hasAudio() const;
        double         duration() const;
        uint32_t       width() const;
        uint32_t       height() const;

    private:
        enum class SlotState : uint8_t
        {
            Free = 0,
            Queued,
            HeldByMain,
        };

        enum class SeekIntent : uint8_t
        {
            Play = 0,
            Pause,
            Ready,
        };

        enum class CommandKind : uint8_t
        {
            Open = 0,
            Seek,
            Shutdown,
        };

        enum class EventKind : uint8_t
        {
            SeekDone = 0,
            SeekFailed,
            PlayFailed,
        };

        struct VideoSlot
        {
            SlotState            state          = SlotState::Free;
            uint64_t             epoch          = 0;
            double               pts            = 0.0;
            uint32_t             surface        = 0;
            uint64_t             decodeFence    = 0;
            uint64_t             convertFence   = 0;
            bool                 waitingConvert = false;
            bool                 hardware       = false;
            std::vector<uint8_t> bgra;
        };

        struct AudioPacket
        {
            std::vector<int16_t> pcm;
            uint64_t             epoch = 0;
            bool                 end   = false;
        };

        struct Command
        {
            CommandKind   kind        = CommandKind::Open;
            uint64_t      generation  = 0;
            uint64_t      epoch       = 0;
            uint64_t      seekSerial  = 0;
            double        seconds     = 0.0;
            std::wstring  absolutePath;
            int32_t adapterLow  = 0;
            int32_t adapterHigh = 0;
            void*   device      = nullptr;
        };

        struct WorkerEvent
        {
            EventKind   kind       = EventKind::PlayFailed;
            uint64_t serial = 0;
            double   pts    = 0.0;
        };

        enum class Pump : uint8_t
        {
            Continue = 0,
            Command,
            Stop,
            Fail,
            Idle,
        };

        void pull();
        void ensureThread();
        void enqueue(Command cmd); // Caller holds m_mutex.
        void workerMain();
        void runOpen(const Command& cmd);
        void runSeek(const Command& cmd);
        void decodeLoop(uint64_t generation, uint64_t epoch);
        Pump pumpOne(uint64_t generation, uint64_t epoch, bool& preferVideo, uint32_t& videos, double& audioSec, bool reportFailure);
        bool publishOpen(uint64_t generation, MovieStatus status, uint32_t videos, double audioSec);
        void postSeek(uint64_t generation, uint64_t serial, double pts, bool failed);
        void postPlayFailed();
        void waitOpenAck(uint64_t generation);
        void consumeEvents();
        void beginSeek(double seconds, SeekIntent intent);
        void finishSeek(double pts);
        void startPlayback();
        void openVoice();
        void enterFailed(const char* message);
        void presentFrame(double targetPts);
        void reclaimSlots(Renderer& renderer);
        void submitAudio();
        void drainAudio();
        void releaseNv12(Renderer* renderer, bool defer);
        bool adoptGpu(Renderer& renderer);
        int  findFreeSlot() const; // Caller holds m_mutex.
        void clearQueued(); // Caller holds m_mutex.
        bool queuedAhead(double targetPts) const; // Caller holds m_mutex.
        bool queuedVideo() const; // Caller holds m_mutex.
        float fadeOpacity() const;
        void noteUnderrun(double now);

        static double steadyNow();

        void (*m_fill)(void* user, MovieHost& host) = nullptr;
        void*  m_user                                = nullptr;
        Renderer*           m_renderer              = nullptr;
        Audio::AudioSystem* m_audio                 = nullptr;

        MoviePlayState m_state      = MoviePlayState::Closed;
        SeekIntent     m_seekIntent = SeekIntent::Play;
        bool           m_loop       = false;
        float          m_volume     = 1.f;

        bool     m_havePresented      = false;
        bool     m_presentedSlotValid = false;
        uint32_t m_presentedSlot      = 0;
        bool     m_hardware      = false;
        bool     m_hasAudio      = false;
        bool     m_flip180       = false;
        bool     m_audioMaster   = false;
        bool     m_videoEos      = false;
        bool     m_audioEos      = false;
        bool     m_eosSubmitted     = false;
        bool     m_decodeSubmitsPcm = false; // true only while MovieDecode is the PCM submitter
        bool     m_streamDropped    = false; // Voice for this epoch will not take PCM.
        bool     m_loggedWall    = false;
        bool     m_feederArmed   = false;
        bool     m_feederAsserted = false;
        uint32_t m_width         = 0;
        uint32_t m_height        = 0;
        uint32_t m_colorMode     = 1;
        uint32_t m_sampleRate    = 48000;
        uint32_t m_stream        = 0;
        double   m_duration      = 0.0;
        double   m_frameDuration = 1.0 / 30.0;
        double   m_seekOrigin    = 0.0;
        uint64_t m_segmentBase   = 0;
        double   m_wallAnchor    = 0.0;
        double   m_wallAnchorAt  = 0.0;
        double   m_pausedAccum   = 0.0;
        double   m_pauseBegan    = 0.0;
        double   m_lastAudio     = 0.0;
        double   m_lastPicture   = 0.0;
        double   m_fadeStart     = 0.0;
        double   m_seekDeadline  = 0.0;
        double   m_feederStart   = 0.0;

        std::unique_ptr<MovieFrameTexture>   m_frames;
        std::unique_ptr<MoviePresentPipeline> m_present;
        std::unique_ptr<MovieSource>         m_source;

        void* m_nv12[3]{};
        void* m_decodeFence = nullptr;

        mutable std::mutex      m_mutex;
        std::condition_variable m_cv;
        std::thread             m_thread;
        std::deque<Command>     m_commands;
        std::deque<AudioPacket> m_audioQueue;
        std::deque<WorkerEvent> m_events;
        VideoSlot               m_slot[3]{};

        bool     m_shutdown       = false;
        bool     m_dropReader     = false;
        bool     m_openPosted     = false;
        uint64_t m_liveGeneration = 0;
        uint64_t m_generationSerial = 0;
        uint64_t m_epoch          = 0;
        uint64_t m_epochSerial    = 0;
        uint64_t m_seekSerial     = 0;
        uint64_t m_openGeneration = 0;
        uint32_t    m_openVideos   = 0;
        double      m_openAudioSec = 0.0;
        MovieStatus     m_openStatus = MovieStatus::Ok;
        MovieSourceInfo m_openInfo{};
        void* m_openNv12Wait[3]{};
        void* m_openFenceWait = nullptr;

        MovieStats m_stats{};
    };

} // namespace Dark
