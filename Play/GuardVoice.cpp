#include "Play/GuardVoice.h"

#include "AI/AiComponents.h"
#include "Assets/AssetManager.h"
#include "Character/HealthComponent.h"
#include "Core/Json.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        constexpr uintmax_t kMaxBarkBytes = 256 * 1024;

        void readLines(const json& lines, const char* key, std::vector<std::string>& out)
        {
            if (!lines.contains(key))
                return;
            const json& list = lines[key];
            if (!list.is_array())
            {
                DE_LOG_WARN(LogCategory::AI, "GuardBarks: \"{}\" should be a list of lines", key);
                return;
            }
            for (const json& line : list)
            {
                if (line.is_string() && !line.get_ref<const std::string&>().empty())
                    out.push_back(line.get<std::string>());
            }
        }

        bool isGuard(World& world, Entity e)
        {
            const TagComponent*    tag = world.get<TagComponent>(e);
            const HealthComponent* hp  = world.get<HealthComponent>(e);
            return hp && hp->health.alive() && !(tag && tag->name == "Wolf");
        }
    } // namespace

    bool parseGuardBarks(std::string_view jsonText, GuardBarks& out, std::string* error)
    {
        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            if (error)
                *error = "not a JSON object";
            return false;
        }
        GuardBarks b;
        bool       ok = true;
        ok &= jsonBoolOr(root, "enabled", b.enabled, true, "GuardBarks", error);
        ok &= jsonFloatOr(root, "volume", b.volume, 1.0f, "GuardBarks", error);
        ok &= jsonFloatOr(root, "maxDistance", b.maxDistance, 40.0f, "GuardBarks", error);
        ok &= jsonFloatOr(root, "hearRange", b.hearRange, 30.0f, "GuardBarks", error);
        ok &= jsonFloatOr(root, "idleMin", b.idleMin, 25.0f, "GuardBarks", error);
        ok &= jsonFloatOr(root, "idleMax", b.idleMax, 55.0f, "GuardBarks", error);
        ok &= jsonFloatOr(root, "chatRange", b.chatRange, 8.0f, "GuardBarks", error);
        ok &= jsonFloatOr(root, "chatEvery", b.chatEvery, 40.0f, "GuardBarks", error);
        if (!ok)
            return false;
        if (root.contains("lines") && root["lines"].is_object())
        {
            const json& lines = root["lines"];
            readLines(lines, "suspicious", b.suspicious);
            readLines(lines, "investigate", b.investigate);
            readLines(lines, "alert", b.alert);
            readLines(lines, "answer", b.answer);
            readLines(lines, "lost", b.lost);
            readLines(lines, "giveUp", b.giveUp);
            readLines(lines, "foundBody", b.foundBody);
            readLines(lines, "idle", b.idle);
            readLines(lines, "missing", b.missing);
        }
        if (root.contains("chats") && root["chats"].is_array())
        {
            for (const json& pair : root["chats"])
            {
                if (pair.is_array() && pair.size() == 2 && pair[0].is_string() && pair[1].is_string())
                    b.chats.emplace_back(pair[0].get<std::string>(), pair[1].get<std::string>());
            }
        }
        out = std::move(b);
        return true;
    }

    bool loadGuardBarks(const std::filesystem::path& path, GuardBarks& out, std::string* error)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec) || ec)
        {
            if (error)
                *error = "file not found: " + path.string();
            return false;
        }
        const uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec || size > kMaxBarkBytes)
        {
            if (error)
                *error = "unreadable or too large: " + path.string();
            return false;
        }
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        return parseGuardBarks(ss.str(), out, error);
    }

    void GuardVoice::start(GuardBarks barks)
    {
        m_barks = std::move(barks);
        reset();
        if (m_barks.enabled)
            m_synth.start();
    }

    void GuardVoice::stop()
    {
        m_synth.stop();
        reset();
    }

    void GuardVoice::reset()
    {
        m_guards.clear();
        m_lines.clear();
        m_subtitles.clear();
        m_chatIn = 20.0f;
    }

    float GuardVoice::random01()
    {
        m_rng = m_rng * 1664525u + 1013904223u;
        return static_cast<float>((m_rng >> 8) & 0xFFFFFF) / 16777216.0f;
    }

    const std::string& GuardVoice::pickLine(const std::vector<std::string>& pool)
    {
        static const std::string kNone;
        if (pool.empty())
            return kNone;
        size_t i = static_cast<size_t>(random01() * static_cast<float>(pool.size())) % pool.size();
        if (pool.size() > 1 && pool[i] == m_lastLine)
            i = (i + 1) % pool.size();
        m_lastLine = pool[i];
        return pool[i];
    }

    GuardVoice::Guard& GuardVoice::guardFor(Entity e)
    {
        auto [it, added] = m_guards.try_emplace(e.id());
        if (added)
        {
            const uint32_t h = e.id() * 2654435761u;
            it->second.voice = static_cast<int>(h >> 7);
            it->second.pitch = static_cast<int>((h >> 12) % 6) - 3;
            it->second.rate  = static_cast<int>((h >> 17) % 3) - 1;
            it->second.idleIn = m_barks.idleMin + random01() * (m_barks.idleMax - m_barks.idleMin);
        }
        return it->second;
    }

    void GuardVoice::say(Entity guard, const std::string& text, float delay, Entity after)
    {
        if (!m_barks.enabled || text.empty() || !guard.valid() || m_lines.size() > 12)
            return;
        Guard&            g   = guardFor(guard);
        const std::string key = std::format("speech:{}:{}:{}:{}", g.voice, g.pitch, g.rate, text);
        bool              asked = m_ready.contains(key);
        for (const auto& [id, pending] : m_requests)
            asked = asked || pending == key;
        if (!asked && m_synth.running())
        {
            if (const uint64_t id = m_synth.request(text, g.voice, g.rate, g.pitch))
                m_requests[id] = key;
        }
        m_lines.push_back(Line{ guard, after, text, key, delay, 0.0f, 0 });
    }

    void GuardVoice::tick(World& world, Audio::AudioSystem& audio, AssetManager& assets, const Math::Vector3f& listener, float dt)
    {
        Audio::SpeechResult result;
        while (m_synth.poll(result))
        {
            const auto it = m_requests.find(result.id);
            if (it == m_requests.end())
                continue;
            AssetRef<Audio::SoundClip> clip;
            if (result.ok)
            {
                auto made = std::make_shared<Audio::SoundClip>();
                if (made->createFromPcm(std::move(result.pcm)))
                {
                    made->setKey(it->second);
                    clip = assets.internSoundClip(std::move(made), it->second);
                }
            }
            m_ready[it->second] = clip;
            m_requests.erase(it);
        }

        if (!m_barks.enabled)
            return;
        m_clock += dt;

        struct Seen
        {
            Entity         e{};
            Math::Vector3f pos{};
            AI::Alertness  now = AI::Alertness::Calm;
        };
        std::vector<Seen> guards;
        std::vector<Entity> reporting;
        world.each<AiAgentComponent>([&](Entity e, AiAgentComponent& ai) {
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (xf && isGuard(world, e))
                guards.push_back(Seen{ e, xf->position, ai.aware.alertness });
            if (ai.reportMissing)
            {
                ai.reportMissing = false;
                reporting.push_back(e);
            }
        });
        for (Entity e : reporting)
        {
            guardFor(e).last = AI::Alertness::Investigating;
            say(e, pickLine(m_barks.missing));
        }

        auto nearestBuddy = [&](const Seen& self, float range) {
            Entity best{};
            float  bestD = range * range;
            for (const Seen& other : guards)
            {
                const float d = (other.pos - self.pos).MagnitudeSqrd();
                if (other.e.id() != self.e.id() && d < bestD)
                {
                    bestD = d;
                    best  = other.e;
                }
            }
            return best;
        };

        for (const Seen& s : guards)
        {
            const bool fresh = !m_guards.contains(s.e.id());
            Guard&     g     = guardFor(s.e);
            if (g.playing && !audio.isPlaying(g.playing))
                g.playing = 0;
            if (g.playing)
                audio.setVoicePosition(g.playing, s.pos + Math::Vector3f{ 0.0f, 1.6f, 0.0f });
            g.cooldown -= dt;
            if (fresh)
            {
                g.last = s.now;
                continue;
            }
            const AI::Alertness was   = g.last;
            const bool          ready = g.cooldown <= 0.0f;
            const std::vector<std::string>* pool = nullptr;
            if (s.now == AI::Alertness::Alert && was != AI::Alertness::Alert)
            {
                const bool first = m_clock - m_lastAlertAt > 4.0f;
                if (first)
                    say(s.e, pickLine(m_barks.alert));
                else if (ready)
                    say(s.e, pickLine(m_barks.answer), 0.3f + 0.4f * random01());
                m_lastAlertAt = m_clock;
                g.cooldown    = 4.0f;
            }
            else if (s.now == AI::Alertness::Suspicious && was == AI::Alertness::Calm)
                pool = &m_barks.suspicious;
            else if (s.now == AI::Alertness::Investigating && (was == AI::Alertness::Calm || was == AI::Alertness::Suspicious))
                pool = &m_barks.investigate;
            else if (s.now == AI::Alertness::Searching && was == AI::Alertness::Alert)
                pool = &m_barks.lost;
            else if (s.now == AI::Alertness::Searching && was != AI::Alertness::Searching)
                pool = &m_barks.foundBody;
            else if (s.now == AI::Alertness::Calm && (was == AI::Alertness::Investigating || was == AI::Alertness::Searching))
                pool = &m_barks.giveUp;
            else if (s.now == AI::Alertness::Calm && was == AI::Alertness::Calm)
            {
                g.idleIn -= dt;
                if (g.idleIn <= 0.0f)
                {
                    g.idleIn = m_barks.idleMin + random01() * (m_barks.idleMax - m_barks.idleMin);
                    if ((s.pos - listener).MagnitudeSqrd() < m_barks.hearRange * m_barks.hearRange && !g.playing)
                        pool = &m_barks.idle;
                }
            }
            if (pool && (ready || pool == &m_barks.foundBody || pool == &m_barks.lost))
            {
                say(s.e, pickLine(*pool));
                g.cooldown = 4.0f;
            }
            g.last = s.now;
        }

        m_chatIn -= dt;
        if (m_chatIn <= 0.0f && !m_barks.chats.empty())
        {
            m_chatIn = m_barks.chatEvery * (0.7f + 0.6f * random01());
            for (size_t a = 0; a < guards.size(); ++a)
            {
                if (guards[a].now != AI::Alertness::Calm || (guards[a].pos - listener).MagnitudeSqrd() > m_barks.hearRange * m_barks.hearRange)
                    continue;
                bool spoke = false;
                for (size_t b = a + 1; b < guards.size() && !spoke; ++b)
                {
                    if (guards[b].now != AI::Alertness::Calm || (guards[b].pos - guards[a].pos).MagnitudeSqrd() > m_barks.chatRange * m_barks.chatRange)
                        continue;
                    const auto& chat = m_barks.chats[static_cast<size_t>(random01() * static_cast<float>(m_barks.chats.size())) % m_barks.chats.size()];
                    say(guards[a].e, chat.first);
                    say(guards[b].e, chat.second, 0.4f, guards[a].e);
                    spoke = true;
                }
                if (spoke)
                    break;
            }
        }

        for (size_t i = 0; i < m_lines.size();)
        {
            Line& line = m_lines[i];
            line.waited += dt;
            const TransformComponent* xf = world.alive(line.guard) ? world.get<TransformComponent>(line.guard) : nullptr;
            if (!xf || !isGuard(world, line.guard) || line.waited > 14.0f)
            {
                m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            bool waiting = false;
            for (size_t k = 0; k < i; ++k)
                waiting = waiting || m_lines[k].guard.id() == line.guard.id() || (line.after.valid() && m_lines[k].guard.id() == line.after.id());
            Guard& g = guardFor(line.guard);
            if (g.playing)
                waiting = true;
            if (line.after.valid())
            {
                const auto other = m_guards.find(line.after.id());
                waiting = waiting || (other != m_guards.end() && other->second.playing != 0);
            }
            if (waiting)
            {
                ++i;
                continue;
            }
            line.delay -= dt;
            const auto readyClip = m_ready.find(line.key);
            const bool pending   = readyClip == m_ready.end() && m_synth.running() && line.waited < 4.0f;
            if (line.delay > 0.0f || pending)
            {
                ++i;
                continue;
            }
            float shown = 2.5f;
            if (readyClip != m_ready.end() && readyClip->second)
            {
                const AssetRef<Audio::SoundClip>& clip = readyClip->second;
                Audio::PlayDesc desc;
                desc.spatial     = true;
                desc.position    = xf->position + Math::Vector3f{ 0.0f, 1.6f, 0.0f };
                desc.volume      = m_barks.volume;
                desc.minDistance = 3.0f;
                desc.maxDistance = m_barks.maxDistance;
                desc.sourceId    = line.guard.id();
                desc.doppler     = false;
                g.playing        = audio.play(clip, desc);
                if (clip->sampleRate() > 0)
                    shown = static_cast<float>(clip->frameCount()) / static_cast<float>(clip->sampleRate()) + 0.6f;
            }
            DE_LOG_INFO(LogCategory::AI, "Guard {} says: \"{}\"", line.guard.id(), line.text);
            m_subtitles.push_back(GuardSubtitle{ line.guard, line.text, shown });
            m_lines.erase(m_lines.begin() + static_cast<std::ptrdiff_t>(i));
        }

        for (size_t i = 0; i < m_subtitles.size();)
        {
            m_subtitles[i].timeLeft -= dt;
            if (m_subtitles[i].timeLeft <= 0.0f)
                m_subtitles.erase(m_subtitles.begin() + static_cast<std::ptrdiff_t>(i));
            else
                ++i;
        }
    }

} // namespace Dark
