#include "Media/MoviePlayer.h"

#include "Assets/AssetManager.h"
#include "Audio/AudioSystem.h"
#include "Core/Log.h"
#include "Media/MovieClock.h"
#include "Media/MoviePath.h"
#include "Media/MoviePresentMath.h"
#include "Media/MovieSource.h"
#include "Render/MovieFrameTexture.h"
#include "Render/MoviePresentPipeline.h"
#include "Render/Renderer.h"

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <utility>

namespace Dark
{

    namespace
    {

        constexpr uint32_t kVideoSlots           = 3;
        constexpr size_t   kAudioCap             = 8;
        constexpr size_t   kCommandCap           = 8;
        constexpr int      kSeekDiscardFrames   = 120;
        constexpr double   kSeekDiscardSeconds  = 2.0;
        constexpr double   kSeekDeadlineSeconds = 3.0;
        constexpr double   kOpenWaitSeconds     = 0.5;
        constexpr double   kPrerollBudgetSeconds = 0.4;
        constexpr double   kStallSeconds        = 2.0;
        constexpr double   kFadeSeconds         = 0.25;
        constexpr double   kAudioPrerollSeconds = 0.080;
        constexpr uint32_t kVideoPreroll        = 3;
        constexpr double   kFeederSeconds       = 0.5;

        using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

        void nameDecodeThread()
        {
            const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
            if (!kernel)
                return;
            auto setDesc = reinterpret_cast<SetThreadDescriptionFn>(GetProcAddress(kernel, "SetThreadDescription"));
            if (setDesc)
                setDesc(GetCurrentThread(), L"MovieDecode");
        }

    } // namespace

    double MoviePlayer::steadyNow()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    MoviePlayer::MoviePlayer() = default;

    MoviePlayer::~MoviePlayer()
    {
        close();
        m_present.reset();
    }

    void MoviePlayer::bind(void (*fill)(void* user, MovieHost& host), void* user)
    {
        m_fill = fill;
        m_user = user;
    }

    void MoviePlayer::pull()
    {
        m_renderer = nullptr;
        m_audio    = nullptr;
        if (!m_fill)
            return;
        MovieHost host;
        m_fill(m_user, host);
        m_renderer = host.renderer;
        m_audio    = host.audio;
    }

    float MoviePlayer::fadeOpacity() const
    {
        if (m_state != MoviePlayState::Ended)
            return 1.f;
        const double elapsed = steadyNow() - m_fadeStart;
        if (elapsed <= 0.0)
            return 1.f;
        if (elapsed >= kFadeSeconds)
            return 0.f;
        return static_cast<float>(1.0 - elapsed / kFadeSeconds);
    }

    bool MoviePlayer::blocksWorld() const
    {
        if (m_state == MoviePlayState::Playing || m_state == MoviePlayState::Paused || m_state == MoviePlayState::Seeking)
            return true;
        if (m_state == MoviePlayState::Ended)
            return fadeOpacity() > 0.f;
        return false;
    }

    MoviePlayState MoviePlayer::state() const
    {
        return m_state;
    }

    bool MoviePlayer::hasAudio() const
    {
        return m_hasAudio;
    }

    double MoviePlayer::duration() const
    {
        return m_duration;
    }

    uint32_t MoviePlayer::width() const
    {
        return m_width;
    }

    uint32_t MoviePlayer::height() const
    {
        return m_height;
    }

    MovieStats MoviePlayer::stats() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        MovieStats out          = m_stats;
        out.videoQueueDepth     = 0;
        out.audioQueueDepth     = 0;
        for (const VideoSlot& slot : m_slot)
        {
            if (slot.state == SlotState::Queued && slot.epoch == m_epoch)
                ++out.videoQueueDepth;
        }
        for (const AudioPacket& packet : m_audioQueue)
        {
            if (packet.epoch == m_epoch)
                ++out.audioQueueDepth;
        }
        return out;
    }

    void MoviePlayer::setVolume(float volume)
    {
        if (volume < 0.f)
            volume = 0.f;
        if (volume > 1.f)
            volume = 1.f;
        m_volume = volume;
        pull();
        if (m_stream != 0 && m_audio)
            m_audio->setStreamVolume(m_stream, m_volume);
    }

    void MoviePlayer::setLoop(bool loop)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_loop = loop;
    }

    int MoviePlayer::findFreeSlot() const
    {
        for (uint32_t i = 0; i < kVideoSlots; ++i)
        {
            if (m_slot[i].state == SlotState::Free)
                return static_cast<int>(i);
        }
        return -1;
    }

    void MoviePlayer::clearQueued()
    {
        for (VideoSlot& slot : m_slot)
        {
            if (slot.state != SlotState::HeldByMain)
                slot = VideoSlot{};
        }
        m_audioQueue.clear();
    }

    bool MoviePlayer::queuedAhead(double targetPts) const
    {
        for (const VideoSlot& slot : m_slot)
        {
            if (slot.state == SlotState::Queued && slot.epoch == m_epoch && slot.pts > targetPts)
                return true;
        }
        return false;
    }

    bool MoviePlayer::queuedVideo() const
    {
        for (const VideoSlot& slot : m_slot)
        {
            if (slot.state == SlotState::Queued && slot.epoch == m_epoch)
                return true;
        }
        return false;
    }

    void MoviePlayer::enqueue(Command cmd)
    {
        if (cmd.kind == CommandKind::Open)
        {
            for (auto it = m_commands.begin(); it != m_commands.end();)
            {
                if (it->kind == CommandKind::Seek)
                    it = m_commands.erase(it);
                else
                    ++it;
            }
        }
        if (cmd.kind == CommandKind::Seek)
        {
            for (Command& queued : m_commands)
            {
                if (queued.kind == CommandKind::Seek)
                {
                    queued = std::move(cmd);
                    return;
                }
            }
        }
        if (m_commands.size() >= kCommandCap && cmd.kind != CommandKind::Open && cmd.kind != CommandKind::Shutdown)
        {
            auto drop = m_commands.end();
            for (auto it = m_commands.begin(); it != m_commands.end(); ++it)
            {
                if (it->kind == CommandKind::Seek)
                {
                    drop = it;
                    break;
                }
            }
            if (drop != m_commands.end())
                m_commands.erase(drop);
        }
        if (m_commands.size() >= kCommandCap && cmd.kind != CommandKind::Open && cmd.kind != CommandKind::Shutdown)
            return;
        m_commands.push_back(std::move(cmd));
    }

    void MoviePlayer::ensureThread()
    {
        if (m_thread.joinable())
            return;
        m_shutdown = false;
        m_thread   = std::thread([this] { workerMain(); });
    }

    void MoviePlayer::releaseNv12(Renderer* renderer, bool defer)
    {
        for (void*& held : m_nv12)
        {
            auto* resource = static_cast<ID3D12Resource*>(held);
            if (!resource)
                continue;
            if (defer && renderer)
                renderer->deferRelease(resource);
            resource->Release();
            held = nullptr;
        }
        if (m_decodeFence)
        {
            auto* fence = static_cast<ID3D12Fence*>(m_decodeFence);
            if (defer && renderer)
                renderer->deferRelease(fence);
            fence->Release();
            m_decodeFence = nullptr;
        }
    }

    bool MoviePlayer::adoptGpu(Renderer& renderer)
    {
        if (m_hardware)
        {
            if (!m_decodeFence)
                return false;
            for (void* resource : m_nv12)
            {
                if (!resource)
                    return false;
            }
        }

        const bool same = m_frames && m_frames->valid() && m_frames->width() == m_width && m_frames->height() == m_height && m_frames->isHardware() == m_hardware;
        if (!same)
        {
            m_frames.reset();
            m_frames = std::make_unique<MovieFrameTexture>();
            if (!m_frames->create(renderer, m_width, m_height, m_hardware))
            {
                m_frames.reset();
                return false;
            }
        }
        if (m_hardware)
        {
            for (uint32_t i = 0; i < kVideoSlots; ++i)
            {
                if (!m_frames->placeNv12(i, static_cast<ID3D12Resource*>(m_nv12[i])))
                    return false;
            }
        }
        if (!m_present)
            m_present = std::make_unique<MoviePresentPipeline>();
        if (!m_present->isValid() && !m_present->create(renderer.device()))
            return false;
        return m_present->isValid();
    }

    void MoviePlayer::enterFailed(const char* message)
    {
        if (m_state == MoviePlayState::Failed)
            return;
        const uint32_t id = m_stream;
        m_stream          = 0;
        m_audioMaster     = false;
        if (id != 0 && m_audio)
            m_audio->stopStream(id);
        m_state        = MoviePlayState::Failed;
        m_seekDeadline = 0.0;
        if (message && message[0] != '\0')
            DE_LOG_ERROR(LogCategory::Media, "{}", message);
    }

    void MoviePlayer::openVoice()
    {
        m_audioMaster = false;
        m_stream      = 0;
        if (!m_hasAudio)
            return;
        if (!m_audio)
        {
            if (!m_loggedWall)
            {
                DE_LOG_WARN(LogCategory::Media, "Movie: audio stream unavailable, using the wall clock");
                m_loggedWall = true;
            }
            return;
        }

        Audio::StreamDesc desc;
        desc.volume              = m_volume;
        desc.format.sampleRate   = m_sampleRate != 0 ? m_sampleRate : 48000u;
        desc.format.channels     = 2;
        desc.format.bitsPerSample = 16;
        m_stream                 = m_audio->openStream(desc);
        if (m_stream == 0)
        {
            if (!m_loggedWall)
            {
                DE_LOG_WARN(LogCategory::Media, "Movie: audio stream unavailable, using the wall clock");
                m_loggedWall = true;
            }
            return;
        }
        m_audio->setStreamVolume(m_stream, m_volume);
        m_segmentBase = m_audio->streamSamplesPlayed(m_stream);
        m_audioMaster = true;
    }

    void MoviePlayer::startPlayback()
    {
        m_havePresented = false;
        m_wallAnchor    = m_seekOrigin;
        m_wallAnchorAt  = steadyNow();
        m_pausedAccum   = 0.0;
        m_lastPicture   = m_wallAnchorAt;
        openVoice();
        m_state = MoviePlayState::Playing;
    }

    void MoviePlayer::finishSeek(double pts)
    {
        m_seekOrigin   = pts;
        m_wallAnchor   = pts;
        m_wallAnchorAt = steadyNow();
        m_pausedAccum  = 0.0;
        m_lastPicture  = m_wallAnchorAt;
        m_seekDeadline = 0.0;
        if (m_seekIntent == SeekIntent::Play)
        {
            openVoice();
            m_state = MoviePlayState::Playing;
        }
        else if (m_seekIntent == SeekIntent::Pause)
        {
            openVoice();
            if (m_stream != 0 && m_audio)
                m_audio->pauseStream(m_stream);
            m_pauseBegan = steadyNow();
            m_state      = MoviePlayState::Paused;
        }
        else
        {
            m_havePresented = false;
            m_state         = MoviePlayState::Ready;
        }
    }

    void MoviePlayer::beginSeek(double seconds, SeekIntent intent)
    {
        uint32_t id = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            id            = m_stream;
            m_stream      = 0;
            m_audioMaster = false;
            m_seekIntent  = intent;
            ++m_seekSerial;
            m_epoch = ++m_epochSerial;
            m_videoEos     = false;
            m_audioEos     = false;
            m_eosSubmitted = false;
            m_feederArmed  = false;
            clearQueued();
            Command cmd;
            cmd.kind       = CommandKind::Seek;
            cmd.generation = m_liveGeneration;
            cmd.epoch      = m_epoch;
            cmd.seekSerial = m_seekSerial;
            cmd.seconds    = seconds;
            enqueue(std::move(cmd));
            m_state        = MoviePlayState::Seeking;
            m_seekDeadline = steadyNow() + kSeekDeadlineSeconds;
        }
        m_cv.notify_all();
        if (id != 0 && m_audio)
            m_audio->stopStream(id);
    }

    void MoviePlayer::play()
    {
        pull();
        if (m_state == MoviePlayState::Ready)
            startPlayback();
        else if (m_state == MoviePlayState::Paused)
        {
            const double now = steadyNow();
            if (!m_audioMaster)
                m_pausedAccum += now - m_pauseBegan;
            if (m_stream != 0 && m_audio)
                m_audio->resumeStream(m_stream);
            m_lastPicture = now;
            m_state       = MoviePlayState::Playing;
        }
        else if (m_state == MoviePlayState::Ended)
            beginSeek(0.0, SeekIntent::Play);
        else if (m_state == MoviePlayState::Seeking)
            m_seekIntent = SeekIntent::Play;
    }

    void MoviePlayer::pause()
    {
        pull();
        if (m_state == MoviePlayState::Seeking)
        {
            m_seekIntent = SeekIntent::Pause;
            return;
        }
        if (m_state != MoviePlayState::Playing)
            return;
        m_pauseBegan = steadyNow();
        if (m_stream != 0 && m_audio)
            m_audio->pauseStream(m_stream);
        m_state = MoviePlayState::Paused;
    }

    void MoviePlayer::stop()
    {
        pull();
        if (m_state == MoviePlayState::Closed || m_state == MoviePlayState::Opening || m_state == MoviePlayState::Failed)
            return;
        beginSeek(0.0, SeekIntent::Ready);
    }

    void MoviePlayer::seek(double seconds)
    {
        pull();
        if (m_state == MoviePlayState::Closed || m_state == MoviePlayState::Opening || m_state == MoviePlayState::Failed)
            return;
        if (seconds < 0.0)
            seconds = 0.0;
        if (m_duration > 0.0 && seconds > m_duration)
            seconds = m_duration;

        SeekIntent intent = SeekIntent::Play;
        if (m_state == MoviePlayState::Seeking)
            intent = m_seekIntent;
        else if (m_state == MoviePlayState::Paused || m_state == MoviePlayState::Ended)
            intent = SeekIntent::Pause;
        else if (m_state == MoviePlayState::Ready)
            intent = SeekIntent::Ready;
        beginSeek(seconds, intent);
    }

    void MoviePlayer::consumeEvents()
    {
        std::deque<WorkerEvent> events;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            events.swap(m_events);
        }
        for (const WorkerEvent& event : events)
        {
            if (event.kind == EventKind::SeekDone)
            {
                if (event.serial == m_seekSerial && m_state == MoviePlayState::Seeking)
                    finishSeek(event.pts);
            }
            else if (event.kind == EventKind::SeekFailed)
            {
                if (event.serial == m_seekSerial && m_state == MoviePlayState::Seeking)
                    enterFailed(nullptr);
            }
            else if (event.kind == EventKind::PlayFailed)
            {
                if (m_state != MoviePlayState::Closed && m_state != MoviePlayState::Opening)
                    enterFailed(nullptr);
            }
        }
    }

    void MoviePlayer::submitAudio()
    {
        if ((m_state != MoviePlayState::Playing && m_state != MoviePlayState::Paused) || m_stream == 0 || !m_audio)
            return;

        for (int n = 0; n < 8; ++n)
        {
            AudioPacket packet;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                while (!m_audioQueue.empty() && m_audioQueue.front().epoch != m_epoch)
                    m_audioQueue.pop_front();
                if (m_audioQueue.empty())
                    break;
                packet = std::move(m_audioQueue.front());
                m_audioQueue.pop_front();
            }
            if (packet.pcm.size() < 2)
                continue;
            const uint32_t frames = static_cast<uint32_t>(packet.pcm.size() / 2u);
            if (!m_audio->submitStreamPcm(m_stream, packet.pcm.data(), frames))
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_audioQueue.push_front(std::move(packet));
                break;
            }
            if (packet.end)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_eosSubmitted = true;
            }
        }
        m_cv.notify_all();
    }

    void MoviePlayer::noteUnderrun(double now)
    {
        if (m_state != MoviePlayState::Playing || !m_audioMaster || m_stream == 0 || !m_audio || m_eosSubmitted)
        {
            m_feederArmed = false;
            return;
        }
        const uint32_t queued = m_audio->streamBuffersQueued(m_stream);
        bool           cpuQueued = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stats.audioBuffersQueued = queued;
            for (const AudioPacket& packet : m_audioQueue)
            {
                if (packet.epoch == m_epoch && packet.pcm.size() >= 2)
                    cpuQueued = true;
            }
        }
        if (queued != 0)
        {
            m_feederArmed = false;
            return;
        }
        if (!cpuQueued)
        {
            m_feederArmed = false;
            std::lock_guard<std::mutex> lock(m_mutex);
            ++m_stats.audioUnderruns;
            if (m_stats.audioUnderruns == 1 || (m_stats.audioUnderruns % 64u) == 0)
                DE_LOG_WARN(LogCategory::Media, "Movie: audio underrun");
            return;
        }
        if (!m_feederArmed)
        {
            m_feederArmed = true;
            m_feederStart = now;
            return;
        }
        if ((now - m_feederStart) >= kFeederSeconds && !m_feederAsserted)
        {
            m_feederAsserted = true;
            DE_ASSERT(false);
        }
    }

    void MoviePlayer::reclaimSlots(Renderer& renderer)
    {
        ID3D12Fence* fence = renderer.texUpdateFence();
        if (!fence)
            return;
        const uint64_t done = fence->GetCompletedValue();
        if (done == UINT64_MAX)
        {
            enterFailed("Movie: device removed");
            return;
        }
        bool freed = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (VideoSlot& slot : m_slot)
            {
                if (slot.state == SlotState::HeldByMain && slot.waitingConvert && done >= slot.convertFence)
                {
                    slot               = VideoSlot{};
                    freed              = true;
                }
            }
        }
        if (freed)
            m_cv.notify_all();
    }

    void MoviePlayer::presentFrame(double targetPts)
    {
        if (!m_renderer || !m_frames || !m_frames->valid())
            return;

        int                  present = -1;
        bool                 ahead   = false;
        std::vector<uint8_t> bgra;
        bool                 hardware = false;
        uint64_t             fenceValue = 0;
        uint32_t             surface = 0;
        double               pts     = 0.0;
        uint64_t             epoch   = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            MovieQueuedFrame frames[kVideoSlots];
            int              count = 0;
            for (uint32_t i = 0; i < kVideoSlots; ++i)
            {
                if (m_slot[i].state == SlotState::Queued && m_slot[i].epoch == m_epoch)
                {
                    frames[count].pts   = m_slot[i].pts;
                    frames[count].index = static_cast<int>(i);
                    ++count;
                }
            }
            const MovieFrameChoice choice = selectMovieFrame(frames, count, targetPts, m_frameDuration, m_havePresented);
            for (int d = 0; d < choice.dropCount && d < 8; ++d)
            {
                const int index = choice.droppedIndex[d];
                if (index < 0 || static_cast<uint32_t>(index) >= kVideoSlots || index == choice.presentIndex)
                    continue;
                VideoSlot& slot = m_slot[index];
                if (slot.state == SlotState::Queued && slot.epoch == m_epoch)
                {
                    slot = VideoSlot{};
                    ++m_stats.framesDropped;
                }
            }
            ahead = queuedAhead(targetPts);
            if (choice.presentIndex < 0)
            {
                if (ahead)
                    m_lastPicture = steadyNow();
                m_cv.notify_all();
                return;
            }
            present = choice.presentIndex;
            VideoSlot& slot = m_slot[present];
            slot.state      = SlotState::HeldByMain;
            hardware        = slot.hardware;
            fenceValue      = slot.decodeFence;
            surface         = slot.surface;
            pts             = slot.pts;
            epoch           = slot.epoch;
            bgra            = std::move(slot.bgra);
        }
        m_cv.notify_all();

        if (hardware)
        {
            const uint64_t completed = m_decodeFence ? static_cast<ID3D12Fence*>(m_decodeFence)->GetCompletedValue() : 0;
            if (completed == UINT64_MAX)
            {
                enterFailed("Movie: device removed");
                return;
            }
            if (completed < fenceValue)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (present >= 0 && m_slot[present].state == SlotState::HeldByMain && m_slot[present].epoch == epoch)
                {
                    m_slot[present].state = SlotState::Queued;
                    ++m_stats.framesDropped;
                }
                return;
            }
            const bool converted = m_frames->convertNv12(m_frames->nv12PlaneCpu(surface, 0), m_frames->nv12PlaneCpu(surface, 1), m_colorMode, m_flip180 ? 1u : 0u, true);
            if (!converted)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (present >= 0 && m_slot[present].epoch == epoch)
                    m_slot[present] = VideoSlot{};
                m_cv.notify_all();
                enterFailed("Movie: NV12 convert failed");
                return;
            }
            const uint64_t signaled = m_renderer->texUpdateFenceValue();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (present >= 0 && m_slot[present].state == SlotState::HeldByMain && m_slot[present].epoch == epoch)
                {
                    m_slot[present].waitingConvert = true;
                    m_slot[present].convertFence   = signaled;
                    ++m_stats.framesPresented;
                    m_stats.lastPresentPts = pts;
                }
            }
            m_havePresented      = true;
            m_presentedSlot      = m_renderer->frameIndex();
            m_presentedSlotValid = m_presentedSlot < MovieFrameTexture::kSlotCount;
            m_lastPicture        = steadyNow();
            return;
        }

        const uint64_t bytes = static_cast<uint64_t>(m_width) * static_cast<uint64_t>(m_height) * 4u;
        if (bgra.size() != bytes || bytes > 0xffffffffu || !m_frames->upload(bgra.data(), static_cast<uint32_t>(bytes)))
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (present >= 0 && m_slot[present].epoch == epoch)
                m_slot[present] = VideoSlot{};
            m_cv.notify_all();
            enterFailed("Movie: BGRA upload failed");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (present >= 0 && m_slot[present].epoch == epoch)
                m_slot[present] = VideoSlot{};
            ++m_stats.framesPresented;
            m_stats.lastPresentPts = pts;
        }
        m_cv.notify_all();
        m_havePresented      = true;
        m_presentedSlot      = m_renderer->frameIndex();
        m_presentedSlotValid = m_presentedSlot < MovieFrameTexture::kSlotCount;
        m_lastPicture        = steadyNow();
    }

    void MoviePlayer::tick(float dt)
    {
        (void)dt;
        pull();
        consumeEvents();
        if (m_state == MoviePlayState::Failed || m_state == MoviePlayState::Closed || m_state == MoviePlayState::Opening)
            return;

        const double now = steadyNow();
        if (m_state == MoviePlayState::Seeking && m_seekDeadline > 0.0 && now >= m_seekDeadline)
        {
            enterFailed("Movie: seek timed out");
            return;
        }
        if (!m_renderer || !m_renderer->isValid())
        {
            if (m_state == MoviePlayState::Playing || m_state == MoviePlayState::Paused || m_state == MoviePlayState::Seeking)
                enterFailed("Movie: device lost");
            return;
        }

        reclaimSlots(*m_renderer);
        if (m_state == MoviePlayState::Failed)
            return;

        if (m_audioMaster && m_stream != 0 && m_audio && m_audio->streamActive(m_stream))
            m_lastAudio = movieAudioSeconds(m_audio->streamSamplesPlayed(m_stream), m_segmentBase, m_sampleRate, m_seekOrigin);

        if ((m_state == MoviePlayState::Playing || m_state == MoviePlayState::Paused) && m_audioMaster && m_stream != 0 && m_audio && !m_audio->streamActive(m_stream))
        {
            m_wallAnchor   = m_lastAudio;
            m_wallAnchorAt = now;
            m_pausedAccum  = 0.0;
            m_audioMaster  = false;
            m_stream       = 0;
            if (m_state == MoviePlayState::Paused)
                m_pauseBegan = now;
            if (!m_loggedWall)
            {
                DE_LOG_WARN(LogCategory::Media, "Movie: audio stream unavailable, using the wall clock");
                m_loggedWall = true;
            }
        }

        submitAudio();
        noteUnderrun(now);

        double clockSec = 0.0;
        if (m_audioMaster && m_stream != 0 && m_audio)
            clockSec = movieAudioSeconds(m_audio->streamSamplesPlayed(m_stream), m_segmentBase, m_sampleRate, m_seekOrigin);
        else
        {
            clockSec = m_wallAnchor + (now - m_wallAnchorAt) - m_pausedAccum;
            if (m_state == MoviePlayState::Paused)
                clockSec -= now - m_pauseBegan;
        }

        MovieClockInput clockIn;
        clockIn.audioMaster   = m_audioMaster && m_stream != 0;
        clockIn.audioSeconds  = clockSec;
        clockIn.wallSeconds   = clockSec;
        clockIn.frameDuration = m_frameDuration > 0.0 ? m_frameDuration : (1.0 / 30.0);
        const double target   = evaluateMovieClock(clockIn).targetPts;

        if (m_state == MoviePlayState::Playing)
            presentFrame(target);

        bool videoEos = false;
        if (m_state == MoviePlayState::Playing)
        {
            bool audioEos     = false;
            bool eosSubmitted = false;
            bool pendingVideo = false;
            bool audioQueued  = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                videoEos     = m_videoEos;
                audioEos     = m_audioEos;
                eosSubmitted = m_eosSubmitted;
                pendingVideo = queuedVideo();
                for (const AudioPacket& packet : m_audioQueue)
                {
                    if (packet.epoch == m_epoch)
                        audioQueued = true;
                }
            }
            uint32_t buffers = 0;
            if (m_stream != 0 && m_audio)
                buffers = m_audio->streamBuffersQueued(m_stream);
            const bool drained = !m_hasAudio || !m_audioMaster || (audioEos && !audioQueued && eosSubmitted && buffers == 0);
            if (videoEos && !pendingVideo && drained)
            {
                if (m_loop)
                    beginSeek(0.0, SeekIntent::Play);
                else
                {
                    m_fadeStart = steadyNow();
                    m_state     = MoviePlayState::Ended;
                }
            }
        }

        if (m_state == MoviePlayState::Playing && !videoEos && (steadyNow() - m_lastPicture) >= kStallSeconds)
        {
            enterFailed("Movie: no frame for 2s");
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stats.clockSeconds       = clockSec;
            m_stats.videoQueueDepth    = 0;
            m_stats.audioQueueDepth    = 0;
            for (const VideoSlot& slot : m_slot)
            {
                if (slot.state == SlotState::Queued && slot.epoch == m_epoch)
                    ++m_stats.videoQueueDepth;
            }
            for (const AudioPacket& packet : m_audioQueue)
            {
                if (packet.epoch == m_epoch)
                    ++m_stats.audioQueueDepth;
            }
        }
    }

    void MoviePlayer::draw(Renderer& renderer)
    {
        const bool show = m_havePresented && (m_state == MoviePlayState::Playing || m_state == MoviePlayState::Paused || m_state == MoviePlayState::Seeking
            || (m_state == MoviePlayState::Ended && fadeOpacity() > 0.f));
        if (!show || !m_present || !m_present->isValid() || !m_frames || !m_frames->valid())
            return;
        const float opacity = m_state == MoviePlayState::Ended ? fadeOpacity() : 1.f;
        if (opacity <= 0.f)
            return;

        MoviePresentMath::MovieLetterbox box{};
        if (!MoviePresentMath::letterbox(renderer.width(), renderer.height(), m_width, m_height, box))
            return;
        if (!m_presentedSlotValid || m_presentedSlot >= MovieFrameTexture::kSlotCount)
            return;
        if (m_frames->state(m_presentedSlot) != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
            return;
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = m_frames->srvGpu(m_presentedSlot);
        if (srv.ptr == 0)
            return;

        renderer.bindColorTargetOnly();
        const float rect[4] = { box.x0, box.y0, box.x1, box.y1 };
        m_present->draw(renderer.commandList(), m_frames->shaderHeap(), srv, rect, opacity);
    }

    bool MoviePlayer::publishOpen(uint64_t generation, MovieStatus status, uint32_t videos, double audioSec)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_liveGeneration != generation || m_shutdown)
            return false;
        m_openGeneration = generation;
        m_openStatus     = status;
        m_openVideos     = videos;
        m_openAudioSec   = audioSec;
        if (status == MovieStatus::Ok && m_source)
            m_openInfo = m_source->info();
        for (uint32_t i = 0; i < kVideoSlots; ++i)
            m_openNv12Wait[i] = nullptr;
        m_openFenceWait = nullptr;
        if (status == MovieStatus::Ok && m_source)
        {
            for (uint32_t i = 0; i < kVideoSlots; ++i)
                m_openNv12Wait[i] = m_source->nv12(i);
            m_openFenceWait = m_source->decodeFence();
        }
        m_openPosted = true;
        m_cv.notify_all();
        return true;
    }

    void MoviePlayer::postSeek(uint64_t generation, uint64_t serial, double pts, bool failed)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_liveGeneration != generation || m_seekSerial != serial || m_shutdown)
            return;
        WorkerEvent event;
        event.kind   = failed ? EventKind::SeekFailed : EventKind::SeekDone;
        event.serial = serial;
        event.pts    = pts;
        m_events.push_back(event);
        m_cv.notify_all();
    }

    void MoviePlayer::postPlayFailed()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        WorkerEvent event;
        event.kind = EventKind::PlayFailed;
        m_events.push_back(event);
        m_cv.notify_all();
    }

    void MoviePlayer::waitOpenAck(uint64_t generation)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [&] {
            return !m_openPosted || m_liveGeneration != generation || m_shutdown || m_dropReader;
        });
    }

    MoviePlayer::Pump MoviePlayer::pumpOne(uint64_t generation, uint64_t epoch, bool& preferVideo, uint32_t& videos, double& audioSec, bool reportFailure)
    {
        bool     readVideo = true;
        uint32_t surface   = 0;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [&] {
                if (m_shutdown || m_dropReader || m_liveGeneration != generation || !m_commands.empty())
                    return true;
                const bool canVideo    = !m_videoEos && findFreeSlot() >= 0;
                const bool canAudio    = m_hasAudio && !m_audioEos && m_audioQueue.size() < kAudioCap;
                const bool streamsDone = m_videoEos && (!m_hasAudio || m_audioEos);
                if (streamsDone)
                    return true;
                return canVideo || canAudio;
            });
            if (m_shutdown || m_dropReader || m_liveGeneration != generation)
                return Pump::Stop;
            if (!m_commands.empty())
                return Pump::Command;
            const bool canVideo    = !m_videoEos && findFreeSlot() >= 0;
            const bool canAudio    = m_hasAudio && !m_audioEos && m_audioQueue.size() < kAudioCap;
            const bool streamsDone = m_videoEos && (!m_hasAudio || m_audioEos);
            if (streamsDone)
                return Pump::Idle;
            if (!canVideo && !canAudio)
                return Pump::Continue;
            if (canVideo && canAudio)
                readVideo = preferVideo;
            else
                readVideo = canVideo;
            if (readVideo)
            {
                const int slot = findFreeSlot();
                if (slot < 0)
                    return Pump::Continue;
                surface = static_cast<uint32_t>(slot);
            }
            if (canVideo && canAudio)
                preferVideo = !preferVideo;
        }

        if (!m_source)
            return Pump::Fail;
        MovieRead read;
        m_source->read(readVideo, surface, read);
        const bool sourceAudio = m_source->info().hasAudio;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!sourceAudio)
            m_hasAudio = false;
        if (m_shutdown || m_dropReader || m_liveGeneration != generation)
            return Pump::Stop;
        if (m_epoch != epoch)
            return Pump::Command;
        if (read.kind != MovieReadKind::None)
            m_stats.decodeMsAvg = read.decodeMs;
        if (read.kind == MovieReadKind::Error)
        {
            if (reportFailure)
            {
                WorkerEvent event;
                event.kind = EventKind::PlayFailed;
                m_events.push_back(event);
                m_cv.notify_all();
            }
            return Pump::Fail;
        }
        if (read.kind == MovieReadKind::Video && read.end)
            m_videoEos = true;
        if (read.kind == MovieReadKind::Audio && read.end)
        {
            m_audioEos = true;
            if (!read.hasSample)
                m_eosSubmitted = true;
        }
        if (!read.hasSample)
            return Pump::Continue;

        if (read.kind == MovieReadKind::Video)
        {
            if (surface >= kVideoSlots || m_slot[surface].state != SlotState::Free)
                return Pump::Continue;
            VideoSlot& slot     = m_slot[surface];
            slot.state          = SlotState::Queued;
            slot.epoch          = epoch;
            slot.pts            = read.video.pts;
            slot.surface        = surface;
            slot.decodeFence    = read.video.fenceValue;
            slot.hardware       = read.video.hardware;
            slot.bgra           = std::move(read.video.bgra);
            slot.waitingConvert = false;
            ++m_stats.framesDecoded;
            ++videos;
        }
        else if (read.kind == MovieReadKind::Audio)
        {
            const size_t count = read.audio.packets.size();
            size_t       last  = count;
            while (last > 0 && read.audio.packets[last - 1].empty())
                --last;
            for (size_t i = 0; i < last; ++i)
            {
                if (read.audio.packets[i].empty())
                    continue;
                AudioPacket packet;
                packet.pcm   = std::move(read.audio.packets[i]);
                packet.epoch = epoch;
                packet.end   = read.end && (i + 1 == last);
                m_audioQueue.push_back(std::move(packet));
            }
            if (read.audio.duration > 0.0)
                audioSec += read.audio.duration;
        }
        return Pump::Continue;
    }

    void MoviePlayer::decodeLoop(uint64_t generation, uint64_t epoch)
    {
        // IMFSourceReader calls stay on this thread.
        bool     prefer   = true;
        uint32_t videos   = 0;
        double   audioSec = 0.0;
        while (true)
        {
            const Pump pump = pumpOne(generation, epoch, prefer, videos, audioSec, true);
            if (pump == Pump::Continue)
                continue;
            if (pump == Pump::Idle)
                return;
            if (pump == Pump::Fail || pump == Pump::Stop)
            {
                if (m_source)
                    m_source->close();
                std::lock_guard<std::mutex> lock(m_mutex);
                m_dropReader = false;
            }
            return;
        }
    }

    void MoviePlayer::runOpen(const Command& cmd)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_liveGeneration != cmd.generation || m_shutdown)
                return;
        }
        if (!m_source)
            m_source = std::make_unique<MovieSource>();

        const auto          started = std::chrono::steady_clock::now();
        MovieSourceRequest  request;
        request.absolutePath = cmd.absolutePath;
        request.adapterLow   = cmd.adapterLow;
        request.adapterHigh  = cmd.adapterHigh;
        request.device       = cmd.device;

        MovieSourceInfo info;
        const MovieStatus opened = m_source->open(request, info);
        if (opened != MovieStatus::Ok)
        {
            m_source->close();
            publishOpen(cmd.generation, opened, 0, 0.0);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_liveGeneration != cmd.generation || m_shutdown)
            {
                m_source->close();
                return;
            }
            m_hasAudio   = info.hasAudio;
            m_sampleRate = info.sampleRate != 0 ? info.sampleRate : 48000u;
            m_frameDuration = info.frameDuration > 0.0 ? info.frameDuration : (1.0 / 30.0);
            m_videoEos   = false;
            m_audioEos   = false;
        }

        uint32_t videos   = 0;
        double   audioSec = 0.0;
        bool     prefer   = true;
        while (true)
        {
            bool done = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_liveGeneration != cmd.generation || m_shutdown)
                {
                    m_source->close();
                    return;
                }
                const bool audioDone = !m_hasAudio || m_audioEos || audioSec >= kAudioPrerollSeconds;
                const bool budget    = videos >= 1 && (std::chrono::steady_clock::now() - started) >= std::chrono::duration<double>(kPrerollBudgetSeconds);
                const bool ended     = m_videoEos && (!m_hasAudio || m_audioEos);
                done                 = (videos >= kVideoPreroll && audioDone) || budget || ended;
            }
            if (done)
                break;

            const Pump pump = pumpOne(cmd.generation, cmd.epoch, prefer, videos, audioSec, false);
            if (pump == Pump::Fail)
            {
                m_source->close();
                publishOpen(cmd.generation, MovieStatus::DecodeError, videos, audioSec);
                return;
            }
            if (pump == Pump::Stop)
            {
                m_source->close();
                return;
            }
            if (pump == Pump::Idle)
                break;
            if (pump == Pump::Command)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_shutdown || m_liveGeneration != cmd.generation)
                {
                    m_source->close();
                    return;
                }
                break;
            }
        }

        if (videos == 0)
        {
            m_source->close();
            publishOpen(cmd.generation, MovieStatus::DecodeError, 0, 0.0);
            return;
        }
        if (!publishOpen(cmd.generation, MovieStatus::Ok, videos, audioSec))
        {
            m_source->close();
            return;
        }
        waitOpenAck(cmd.generation);
        bool abandon = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            abandon = m_liveGeneration != cmd.generation || m_shutdown || m_dropReader;
            if (abandon)
                m_dropReader = false;
        }
        if (abandon)
        {
            if (m_source)
                m_source->close();
            return;
        }
        decodeLoop(cmd.generation, cmd.epoch);
    }

    void MoviePlayer::runSeek(const Command& cmd)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_liveGeneration != cmd.generation || m_shutdown || m_seekSerial != cmd.seekSerial)
                return;
        }
        if (!m_source || m_source->info().width == 0)
        {
            postSeek(cmd.generation, cmd.seekSerial, cmd.seconds, true);
            return;
        }
        const MovieStatus status = m_source->setPosition(cmd.seconds);
        if (status != MovieStatus::Ok)
        {
            postSeek(cmd.generation, cmd.seekSerial, cmd.seconds, true);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_videoEos     = false;
            m_audioEos     = false;
            m_eosSubmitted = false;
        }

        int    discarded = 0;
        double firstPts  = -1.0;
        bool   shortStop = false;
        bool   kept      = false;
        double keptPts   = cmd.seconds;
        bool   prefer    = true;

        while (!shortStop)
        {
            bool     readVideo = true;
            uint32_t surface   = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_shutdown || m_liveGeneration != cmd.generation || m_seekSerial != cmd.seekSerial)
                    return;
                if (!m_commands.empty())
                    return;
                const bool canVideo = !m_videoEos && findFreeSlot() >= 0;
                const bool canAudio = m_hasAudio && !m_audioEos && m_audioQueue.size() < kAudioCap;
                if (!canVideo && !canAudio)
                    break;
                readVideo = canVideo && (prefer || !canAudio);
                if (readVideo)
                {
                    const int slot = findFreeSlot();
                    if (slot < 0)
                        break;
                    surface = static_cast<uint32_t>(slot);
                }
                prefer = !prefer;
            }

            MovieRead read;
            m_source->read(readVideo, surface, read);
            if (read.kind == MovieReadKind::Error)
            {
                postPlayFailed();
                m_source->close();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_shutdown || m_liveGeneration != cmd.generation || m_seekSerial != cmd.seekSerial)
                    return;
                if (read.kind == MovieReadKind::Video && read.end)
                    m_videoEos = true;
                if (read.kind == MovieReadKind::Audio && read.end)
                    m_audioEos = true;
                if (!m_source->info().hasAudio)
                    m_hasAudio = false;
            }
            if (read.kind == MovieReadKind::Audio)
            {
                const double endPts = read.audio.pts + read.audio.duration;
                if (read.hasSample && endPts >= cmd.seconds - 0.0001)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_epoch == cmd.epoch)
                    {
                        const size_t count = read.audio.packets.size();
                        size_t       last  = count;
                        while (last > 0 && read.audio.packets[last - 1].empty())
                            --last;
                        for (size_t i = 0; i < last; ++i)
                        {
                            if (read.audio.packets[i].empty())
                                continue;
                            AudioPacket packet;
                            packet.pcm   = std::move(read.audio.packets[i]);
                            packet.epoch = cmd.epoch;
                            packet.end   = read.end && (i + 1 == last);
                            m_audioQueue.push_back(std::move(packet));
                        }
                    }
                }
                else if (read.end && !read.hasSample)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_eosSubmitted = true;
                }
                continue;
            }
            if (read.kind != MovieReadKind::Video || !read.hasSample)
            {
                if (read.kind == MovieReadKind::Video && read.end)
                    break;
                continue;
            }

            if (firstPts < 0.0)
                firstPts = read.video.pts;
            const double span    = read.video.pts - firstPts;
            const bool   reached = (read.video.pts + read.video.duration) >= (cmd.seconds - 0.0001);
            if (!reached && discarded < kSeekDiscardFrames && span < kSeekDiscardSeconds)
            {
                ++discarded;
                continue;
            }
            if (!reached)
                shortStop = true;

            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_epoch == cmd.epoch && surface < kVideoSlots && m_slot[surface].state == SlotState::Free)
            {
                VideoSlot& slot     = m_slot[surface];
                slot.state          = SlotState::Queued;
                slot.epoch          = cmd.epoch;
                slot.pts            = read.video.pts;
                slot.surface        = surface;
                slot.decodeFence    = read.video.fenceValue;
                slot.hardware       = read.video.hardware;
                slot.bgra           = std::move(read.video.bgra);
                slot.waitingConvert = false;
                ++m_stats.framesDecoded;
                kept    = true;
                keptPts = read.video.pts;
            }
            break;
        }

        if (shortStop)
            DE_LOG_WARN(LogCategory::Media, "Movie: seek stopped short of the target");

        bool     fillPrefer = true;
        uint32_t videos     = kept ? 1u : 0u;
        double   audioSec   = 0.0;
        while (videos < kVideoPreroll)
        {
            bool audioDone = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_shutdown || m_liveGeneration != cmd.generation || m_seekSerial != cmd.seekSerial)
                    return;
                if (!m_commands.empty() || m_videoEos)
                    break;
                audioDone = !m_hasAudio || m_audioEos || audioSec >= kAudioPrerollSeconds;
            }
            if (videos >= kVideoPreroll && audioDone)
                break;
            const Pump pump = pumpOne(cmd.generation, cmd.epoch, fillPrefer, videos, audioSec, true);
            if (pump == Pump::Fail)
            {
                if (m_source)
                    m_source->close();
                return;
            }
            if (pump == Pump::Idle)
                break;
            if (pump != Pump::Continue)
                return;
            if (videos >= kVideoPreroll && audioSec >= kAudioPrerollSeconds)
                break;
        }
        while (true)
        {
            bool needAudio = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_shutdown || m_seekSerial != cmd.seekSerial || !m_commands.empty())
                    return;
                needAudio = m_hasAudio && !m_audioEos && audioSec < kAudioPrerollSeconds && m_audioQueue.size() < kAudioCap;
            }
            if (!needAudio)
                break;
            const Pump pump = pumpOne(cmd.generation, cmd.epoch, fillPrefer, videos, audioSec, true);
            if (pump == Pump::Idle)
                break;
            if (pump != Pump::Continue)
            {
                if (pump == Pump::Fail && m_source)
                    m_source->close();
                return;
            }
        }

        postSeek(cmd.generation, cmd.seekSerial, kept ? keptPts : cmd.seconds, false);
        bool resume = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            resume = m_liveGeneration == cmd.generation && m_seekSerial == cmd.seekSerial && !m_shutdown && !m_dropReader;
        }
        if (resume)
            decodeLoop(cmd.generation, cmd.epoch);
    }

    void MoviePlayer::workerMain()
    {
        const HRESULT com    = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool    uninit = SUCCEEDED(com);
        const bool    comReady = uninit || com == RPC_E_CHANGED_MODE;
        if (com == RPC_E_CHANGED_MODE)
            DE_LOG_WARN(LogCategory::Media, "Movie: COM apartment already initialized");
        else if (!comReady)
            DE_LOG_ERROR(LogCategory::Media, "Movie: CoInitializeEx failed (HRESULT 0x{:08X})", static_cast<unsigned>(com));
        nameDecodeThread();

        bool mfReady = false;
        for (;;)
        {
            Command cmd;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [&] { return m_shutdown || !m_commands.empty(); });
                if (m_commands.empty())
                    break;
                cmd = std::move(m_commands.front());
                m_commands.pop_front();
                if (cmd.kind == CommandKind::Shutdown)
                    break;
            }

            if (!comReady)
            {
                if (cmd.kind == CommandKind::Open)
                    publishOpen(cmd.generation, MovieStatus::DecodeError, 0, 0.0);
                continue;
            }
            if (!mfReady)
            {
                if (MovieSource::startup() == MovieStatus::Ok)
                    mfReady = true;
                else if (cmd.kind == CommandKind::Open)
                {
                    publishOpen(cmd.generation, MovieStatus::DecodeError, 0, 0.0);
                    continue;
                }
                else
                    continue;
            }
            if (!m_source)
                m_source = std::make_unique<MovieSource>();
            if (cmd.kind == CommandKind::Open)
                runOpen(cmd);
            else if (cmd.kind == CommandKind::Seek)
                runSeek(cmd);
        }

        if (m_source)
            m_source->close();
        m_source.reset();
        if (mfReady)
            MovieSource::shutdown();
        if (uninit)
            CoUninitialize();
    }

    MovieStatus MoviePlayer::open(const char* virtualPath)
    {
        pull();
        if (m_state != MoviePlayState::Closed)
        {
            DE_LOG_WARN(LogCategory::Media, "Movie: open busy");
            return MovieStatus::Busy;
        }
        if (!m_renderer || !m_renderer->isValid() || !m_renderer->device())
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: device lost");
            return MovieStatus::DeviceLost;
        }
        AssetManager* assets = nullptr;
        {
            MovieHost host;
            if (m_fill)
                m_fill(m_user, host);
            assets = host.assets;
        }
        if (!assets)
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: device lost");
            return MovieStatus::DeviceLost;
        }

        const MovieStatus pathStatus = moviePathStatus(*assets, virtualPath);
        if (pathStatus != MovieStatus::Ok)
            return pathStatus;

        const std::filesystem::path resolved = assets->resolve(virtualPath ? virtualPath : "");
        if (resolved.empty())
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: empty path");
            return MovieStatus::DecodeError;
        }

        const LUID luid = m_renderer->adapterLuid();
        Command    cmd;
        cmd.kind         = CommandKind::Open;
        cmd.absolutePath = resolved.wstring();
        cmd.adapterLow   = static_cast<int32_t>(luid.LowPart);
        cmd.adapterHigh  = static_cast<int32_t>(luid.HighPart);
        cmd.device       = m_renderer->device();

        ensureThread();
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            generation = ++m_generationSerial;
            if (generation == 0)
                generation = ++m_generationSerial;
            m_liveGeneration = generation;
            m_epoch          = ++m_epochSerial;
            cmd.generation   = generation;
            cmd.epoch        = m_epoch;
            m_openPosted     = false;
            m_dropReader     = false;
            m_videoEos       = false;
            m_audioEos       = false;
            m_eosSubmitted   = false;
            m_stats          = {};
            m_seekOrigin     = 0.0;
            clearQueued();
            enqueue(std::move(cmd));
            m_state = MoviePlayState::Opening;
        }
        m_cv.notify_all();

        MovieStatus     status = MovieStatus::DecodeError;
        MovieSourceInfo info;
        uint32_t        videos   = 0;
        double          audioSec = 0.0;
        ID3D12Resource* adoptedNv12[kVideoSlots]{};
        ID3D12Fence*    adoptedFence = nullptr;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            // 500 ms bounds the worker status. Texture create runs after the wait.
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(kOpenWaitSeconds);
            const bool posted   = m_cv.wait_until(lock, deadline, [&] {
                return (m_openPosted && m_openGeneration == generation) || m_shutdown;
            });
            if (m_shutdown)
            {
                m_state = MoviePlayState::Closed;
                return MovieStatus::DecodeError;
            }
            if (!posted)
            {
                m_liveGeneration = 0;
                clearQueued();
                m_state = MoviePlayState::Closed;
                m_cv.notify_all();
                DE_LOG_ERROR(LogCategory::Media, "Movie: open timed out");
                return MovieStatus::DecodeError;
            }

            status   = m_openStatus;
            info     = m_openInfo;
            videos   = m_openVideos;
            audioSec = m_openAudioSec;
            if (status == MovieStatus::Ok)
            {
                for (uint32_t i = 0; i < kVideoSlots; ++i)
                {
                    adoptedNv12[i] = static_cast<ID3D12Resource*>(m_openNv12Wait[i]);
                    if (adoptedNv12[i])
                        adoptedNv12[i]->AddRef();
                    m_nv12[i] = adoptedNv12[i];
                }
                adoptedFence = static_cast<ID3D12Fence*>(m_openFenceWait);
                if (adoptedFence)
                    adoptedFence->AddRef();
                m_decodeFence   = adoptedFence;
                m_hardware      = info.hardware;
                m_hasAudio      = info.hasAudio;
                m_flip180       = info.flip180;
                m_width         = info.width;
                m_height        = info.height;
                m_colorMode     = info.colorMode;
                m_sampleRate    = info.sampleRate != 0 ? info.sampleRate : 48000u;
                m_duration      = info.durationSec;
                m_frameDuration = info.frameDuration > 0.0 ? info.frameDuration : (1.0 / 30.0);
                m_loggedWall         = false;
                m_havePresented      = false;
                m_presentedSlotValid = false;
                m_seekOrigin    = 0.0;
            }
            m_openPosted = false;
            m_cv.notify_all();
        }

        if (status != MovieStatus::Ok)
        {
            m_state = MoviePlayState::Closed;
            return status;
        }

        Renderer* renderer = m_renderer;
        if (!renderer || !adoptGpu(*renderer))
        {
            DE_LOG_ERROR(LogCategory::Media, "Movie: device lost");
            m_frames.reset();
            releaseNv12(nullptr, false);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_dropReader     = true;
                m_liveGeneration = 0;
                m_state          = MoviePlayState::Closed;
                m_stats          = {};
            }
            m_cv.notify_all();
            return MovieStatus::DeviceLost;
        }

        m_state = MoviePlayState::Ready;
        const char* pathText = virtualPath ? virtualPath : "";
        const char* hwText   = info.hardware ? "hardware" : "software";
        const char* matrix   = info.bt709 ? "bt709" : "bt601";
        const char* range    = info.fullRange ? "full" : "limited";
        if (info.hasAudio)
        {
            DE_LOG_INFO(LogCategory::Media, "Movie: '{}' {} {} {} {} {}x{} {}/{} fps {} Hz {} ch {:.3f}s", pathText, info.codec, hwText, matrix, range, info.width, info.height,
                info.fpsNum, info.fpsDen, info.sampleRate, info.audioChannels, info.durationSec);
        }
        else
        {
            DE_LOG_INFO(LogCategory::Media, "Movie: '{}' {} {} {} {} {}x{} {}/{} fps silent {:.3f}s", pathText, info.codec, hwText, matrix, range, info.width, info.height, info.fpsNum,
                info.fpsDen, info.durationSec);
        }
        const bool shortPreroll = videos < kVideoPreroll || (info.hasAudio && audioSec < kAudioPrerollSeconds);
        if (shortPreroll)
            DE_LOG_WARN(LogCategory::Media, "Movie: short preroll, {} video frames, {:.0f} ms audio", videos, audioSec * 1000.0);
        return MovieStatus::Ok;
    }

    void MoviePlayer::close()
    {
        pull();
        if (!m_thread.joinable() && m_state == MoviePlayState::Closed && !m_nv12[0] && !m_decodeFence && !m_frames)
            return;

        if (m_thread.joinable())
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_shutdown       = true;
                m_liveGeneration = 0;
                m_openPosted     = false;
                Command cmd;
                cmd.kind = CommandKind::Shutdown;
                enqueue(std::move(cmd));
            }
            m_cv.notify_all();
            m_thread.join();
            m_shutdown = false;
        }

        const uint32_t id = m_stream;
        m_stream          = 0;
        m_audioMaster     = false;
        if (id != 0 && m_audio)
            m_audio->stopStream(id);

        Renderer* renderer = m_renderer;
        releaseNv12(renderer, true);
        m_frames.reset();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_commands.clear();
            m_audioQueue.clear();
            m_events.clear();
            for (VideoSlot& slot : m_slot)
                slot = VideoSlot{};
            m_stats = {};
        }
        m_state              = MoviePlayState::Closed;
        m_havePresented      = false;
        m_presentedSlotValid = false;
        m_hasAudio      = false;
        m_hardware      = false;
        m_width         = 0;
        m_height        = 0;
        m_duration      = 0.0;
        m_videoEos      = false;
        m_audioEos      = false;
        m_eosSubmitted  = false;
        m_seekDeadline  = 0.0;
    }

} // namespace Dark
