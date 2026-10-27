#include "Audio/SpeechSynth.h"

#include "Core/Log.h"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <sapi.h>
#    pragma comment(lib, "sapi.lib")
#endif

namespace Dark::Audio
{
#if defined(_WIN32)
    namespace
    {
        constexpr uint32_t kSpeechRate = 22050;

        std::wstring widen(const std::string& text)
        {
            if (text.empty())
                return {};
            const int    n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            std::wstring out(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
            return out;
        }

        std::wstring speechXml(const std::string& text, int pitch)
        {
            std::wstring escaped;
            for (wchar_t c : widen(text))
            {
                if (c == L'&')
                    escaped += L"&amp;";
                else if (c == L'<')
                    escaped += L"&lt;";
                else if (c == L'>')
                    escaped += L"&gt;";
                else
                    escaped += c;
            }
            return L"<pitch absmiddle=\"" + std::to_wstring(pitch) + L"\">" + escaped + L"</pitch>";
        }

        bool speakToPcm(ISpVoice* voice, const std::string& text, int rate, int pitch, PcmWav& out)
        {
            IStream* base = nullptr;
            if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &base)))
                return false;
            ISpStream* stream = nullptr;
            if (FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream, reinterpret_cast<void**>(&stream))))
            {
                base->Release();
                return false;
            }
            WAVEFORMATEX fmt{};
            fmt.wFormatTag      = WAVE_FORMAT_PCM;
            fmt.nChannels       = 1;
            fmt.nSamplesPerSec  = kSpeechRate;
            fmt.wBitsPerSample  = 16;
            fmt.nBlockAlign     = 2;
            fmt.nAvgBytesPerSec = kSpeechRate * 2;
            bool ok = SUCCEEDED(stream->SetBaseStream(base, SPDFID_WaveFormatEx, &fmt)) && SUCCEEDED(voice->SetOutput(stream, TRUE));
            if (ok)
            {
                voice->SetRate(rate);
                const std::wstring xml = speechXml(text, pitch);
                ok = SUCCEEDED(voice->Speak(xml.c_str(), SPF_IS_XML, nullptr));
            }
            voice->SetOutput(nullptr, TRUE);
            if (ok)
            {
                STATSTG stat{};
                HGLOBAL mem = nullptr;
                ok          = SUCCEEDED(base->Stat(&stat, STATFLAG_NONAME)) && SUCCEEDED(GetHGlobalFromStream(base, &mem));
                const size_t bytes = ok ? static_cast<size_t>(stat.cbSize.QuadPart) : 0;
                const void*  data  = ok ? GlobalLock(mem) : nullptr;
                if (data && bytes >= 2)
                {
                    out.channels   = 1;
                    out.sampleRate = kSpeechRate;
                    out.samples.resize(bytes / 2);
                    std::memcpy(out.samples.data(), data, out.samples.size() * 2);
                }
                if (data)
                    GlobalUnlock(mem);
                ok = !out.samples.empty();
            }
            stream->Release();
            base->Release();
            return ok;
        }
    } // namespace
#endif

    SpeechSynth::~SpeechSynth()
    {
        stop();
    }

    bool SpeechSynth::start()
    {
#if defined(_WIN32)
        if (m_thread.joinable())
            return true;
        m_quit.store(false);
        m_thread = std::thread([this] { run(); });
        return true;
#else
        return false;
#endif
    }

    void SpeechSynth::stop()
    {
        if (!m_thread.joinable())
            return;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_quit.store(true);
            m_jobs.clear();
        }
        m_wake.notify_all();
        m_thread.join();
        m_running.store(false);
    }

    uint64_t SpeechSynth::request(std::string text, int voice, int rate, int pitch)
    {
        if (!m_thread.joinable() || text.empty())
            return 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t id = m_nextId++;
        m_jobs.push_back(Job{ id, std::move(text), voice, rate, pitch });
        m_wake.notify_one();
        return id;
    }

    bool SpeechSynth::poll(SpeechResult& out)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_done.empty())
            return false;
        out = std::move(m_done.front());
        m_done.pop_front();
        return true;
    }

    void SpeechSynth::run()
    {
#if defined(_WIN32)
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ISpVoice*     voice = nullptr;
        if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&voice))))
        {
            DE_LOG_WARN(LogCategory::Audio, "Speech: no SAPI voice available, guards will only show subtitles");
            if (SUCCEEDED(com))
                CoUninitialize();
            return;
        }
        std::vector<ISpObjectToken*> tokens;
        ISpObjectTokenCategory*      category = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory, reinterpret_cast<void**>(&category))))
        {
            IEnumSpObjectTokens* list = nullptr;
            if (SUCCEEDED(category->SetId(SPCAT_VOICES, FALSE)) && SUCCEEDED(category->EnumTokens(nullptr, nullptr, &list)))
            {
                ISpObjectToken* token = nullptr;
                while (list->Next(1, &token, nullptr) == S_OK)
                    tokens.push_back(token);
                list->Release();
            }
            category->Release();
        }
        m_voiceCount.store(static_cast<int>(tokens.size()));
        m_running.store(true);
        DE_LOG_INFO(LogCategory::Audio, "Speech: SAPI ready with {} voice(s)", tokens.size());

        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_wake.wait(lock, [this] { return m_quit.load() || !m_jobs.empty(); });
                if (m_quit.load())
                    break;
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            if (!tokens.empty())
                voice->SetVoice(tokens[static_cast<size_t>(job.voice) % tokens.size()]);
            SpeechResult result;
            result.id = job.id;
            result.ok = speakToPcm(voice, job.text, job.rate, job.pitch, result.pcm);
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done.push_back(std::move(result));
        }

        for (ISpObjectToken* token : tokens)
            token->Release();
        voice->Release();
        if (SUCCEEDED(com))
            CoUninitialize();
#endif
    }

} // namespace Dark::Audio
