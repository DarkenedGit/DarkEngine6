#include "AI/Awareness.h"

#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Dark::AI
{
    bool stepAwareness(AwarenessState& state, const AwarenessSettings& settings, const AwarenessInput& in, float dt)
    {
        const bool noticed = in.sees && in.exposure >= settings.noticeExposure;
        const bool feels   = in.nearby && in.motion > settings.noticeMotion;
        state.seesNow      = noticed || in.touching;
        if (state.seesNow || feels)
        {
            state.noticePos = in.playerPos;
            state.heardOnly = false;
        }
        else if (in.noise > 0.0f && state.alertness != Alertness::Alert)
        {
            state.noticePos = in.noisePos;
            state.heardOnly = state.alertness == Alertness::Calm || state.heardOnly;
        }
        const bool cue = state.seesNow || feels || in.noise > 0.0f;
        state.sinceCue = cue ? 0.0f : state.sinceCue + dt;

        float sight = 0.0f;
        if (noticed)
        {
            const float angleScale = in.primary ? 1.0f : settings.sideStill + settings.sideMoving * in.motion;
            sight                  = settings.fillNear * std::exp(-in.distance / settings.falloff) * angleScale * in.exposure;
            if (in.primary && in.distance < settings.nearRange && in.exposure >= settings.clearExposure && in.bodyShown >= settings.clearBody)
                sight = Math::Max(sight, settings.clearRate);
            if (in.looking)
                sight *= settings.lookBoost;
        }
        float felt = feels ? settings.proximityRate * in.motion : 0.0f;
        if (state.wary)
        {
            sight *= settings.waryFillScale;
            felt *= settings.waryFillScale;
        }
        const float drainScale = state.wary ? settings.waryDrainScale : 1.0f;
        const float timeScale  = state.wary ? settings.waryTimeScale : 1.0f;

        state.stateTime += dt;
        auto enter = [&](Alertness next) {
            state.alertness = next;
            state.stateTime = 0.0f;
            state.meter     = 0.0f;
        };

        const bool  tense      = state.alertness != Alertness::Calm || state.wary;
        const bool  pointBlank = noticed && in.primary && in.distance < settings.instantRange
                              && in.exposure >= (tense ? settings.instantExposureWary : settings.instantExposure);
        if ((in.touching || pointBlank) && state.alertness != Alertness::Alert)
        {
            enter(Alertness::Alert);
            return true;
        }

        const bool draining = state.sinceCue > settings.evidenceGrace;
        switch (state.alertness)
        {
        case Alertness::Calm:
            state.meter += Math::Max(sight, felt) * dt + in.noise;
            if (draining)
                state.meter -= settings.drainCalm * drainScale * dt;
            if (state.meter >= 1.0f)
                enter(Alertness::Suspicious);
            break;

        case Alertness::Suspicious:
        case Alertness::Investigating:
        case Alertness::Searching:
        {
            const float yellow = Math::Max(sight, felt);
            if (yellow > 0.0f)
                state.meter += yellow * (state.alertness == Alertness::Searching ? settings.searchFillScale : settings.suspiciousFillScale) * dt;
            else if (draining)
                state.meter -= settings.drainCurious * drainScale * dt;
            if (cue)
                state.stateTime = Math::Min(state.stateTime, settings.investigateAfter);
            if (state.meter >= 1.0f)
            {
                enter(Alertness::Alert);
                return true;
            }
            if (state.alertness == Alertness::Suspicious && state.stateTime >= settings.investigateAfter)
            {
                state.alertness = Alertness::Investigating;
                state.stateTime = 0.0f;
            }
            else if (state.alertness == Alertness::Investigating && state.stateTime >= settings.investigateSeconds * timeScale)
                enter(Alertness::Calm);
            else if (state.alertness == Alertness::Searching && state.stateTime >= settings.searchSeconds * timeScale)
            {
                state.alertness = Alertness::Investigating;
                state.stateTime = 0.0f;
            }
            break;
        }

        case Alertness::Alert:
            if (state.seesNow || feels)
                state.stateTime = 0.0f;
            else if (state.stateTime >= settings.alertLoseSeconds)
            {
                enter(Alertness::Searching);
                state.wary = true;
            }
            break;
        }
        state.meter = Math::Clamp(state.meter, 0.0f, 1.0f);
        return false;
    }

    void forceAlert(AwarenessState& state, const Math::Vector3f& where)
    {
        state.noticePos = where;
        state.heardOnly = false;
        state.seesNow   = true;
        state.alertness = Alertness::Alert;
        state.stateTime = 0.0f;
        state.meter     = 0.0f;
    }

    void alertToInvestigate(AwarenessState& state, const Math::Vector3f& where)
    {
        state.noticePos = where;
        state.heardOnly = true;
        if (state.alertness == Alertness::Calm)
        {
            state.alertness = Alertness::Investigating;
            state.stateTime = 0.0f;
            state.meter     = 0.0f;
        }
    }

    namespace
    {
        using json = nlohmann::json;

        constexpr std::uintmax_t kMaxJsonBytes = 64u * 1024u;

        struct Field
        {
            const char* key;
            float AwarenessSettings::* member;
            bool nonNegative;
            float hi;
        };

        constexpr Field kFields[] = {
            { "fillNear", &AwarenessSettings::fillNear, true, -1.0f },
            { "falloff", &AwarenessSettings::falloff, true, -1.0f },
            { "maxRange", &AwarenessSettings::maxRange, true, -1.0f },
            { "focusHalfDeg", &AwarenessSettings::focusHalfDeg, true, -1.0f },
            { "sideHalfDeg", &AwarenessSettings::sideHalfDeg, true, -1.0f },
            { "sideRange", &AwarenessSettings::sideRange, true, -1.0f },
            { "sideStill", &AwarenessSettings::sideStill, true, -1.0f },
            { "sideMoving", &AwarenessSettings::sideMoving, true, -1.0f },
            { "nearRange", &AwarenessSettings::nearRange, true, -1.0f },
            { "clearRate", &AwarenessSettings::clearRate, true, -1.0f },
            { "clearExposure", &AwarenessSettings::clearExposure, true, 1.0f },
            { "clearBody", &AwarenessSettings::clearBody, true, 1.0f },
            { "instantRange", &AwarenessSettings::instantRange, true, -1.0f },
            { "instantExposure", &AwarenessSettings::instantExposure, true, 1.0f },
            { "instantExposureWary", &AwarenessSettings::instantExposureWary, true, 1.0f },
            { "proximityRange", &AwarenessSettings::proximityRange, true, -1.0f },
            { "proximityRate", &AwarenessSettings::proximityRate, true, -1.0f },
            { "lookBoost", &AwarenessSettings::lookBoost, true, -1.0f },
            { "evidenceGrace", &AwarenessSettings::evidenceGrace, true, -1.0f },
            { "drainCalm", &AwarenessSettings::drainCalm, true, -1.0f },
            { "drainCurious", &AwarenessSettings::drainCurious, true, -1.0f },
            { "suspiciousFillScale", &AwarenessSettings::suspiciousFillScale, true, -1.0f },
            { "investigateAfter", &AwarenessSettings::investigateAfter, true, -1.0f },
            { "investigateSeconds", &AwarenessSettings::investigateSeconds, true, -1.0f },
            { "alertLoseSeconds", &AwarenessSettings::alertLoseSeconds, true, -1.0f },
            { "searchSeconds", &AwarenessSettings::searchSeconds, true, -1.0f },
            { "searchFillScale", &AwarenessSettings::searchFillScale, true, -1.0f },
            { "alertShareRadius", &AwarenessSettings::alertShareRadius, true, -1.0f },
            { "reactMeter", &AwarenessSettings::reactMeter, true, 1.0f },
            { "concealShown", &AwarenessSettings::concealShown, true, 1.0f },
            { "wallScale", &AwarenessSettings::wallScale, true, -1.0f },
            { "noiseBase", &AwarenessSettings::noiseBase, true, -1.0f },
            { "noiseFalloff", &AwarenessSettings::noiseFalloff, true, -1.0f },
            { "stanceCrouchWalk", &AwarenessSettings::stanceCrouchWalk, true, -1.0f },
            { "stanceCrouchStill", &AwarenessSettings::stanceCrouchStill, true, -1.0f },
            { "stanceSprint", &AwarenessSettings::stanceSprint, true, -1.0f },
            { "stanceWalk", &AwarenessSettings::stanceWalk, true, -1.0f },
            { "stanceStand", &AwarenessSettings::stanceStand, true, -1.0f },
            { "stanceCamouflage", &AwarenessSettings::stanceCamouflage, true, -1.0f },
            { "strideCrouch", &AwarenessSettings::strideCrouch, true, -1.0f },
            { "strideSprint", &AwarenessSettings::strideSprint, true, -1.0f },
            { "strideWalk", &AwarenessSettings::strideWalk, true, -1.0f },
        };

        constexpr int kScalarCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
        constexpr int kBodyStandBit  = kScalarCount;
        constexpr int kBodyCrouchBit = kScalarCount + 3;
        constexpr int kBodyWeightBit = kScalarCount + 6;
        static_assert(kBodyWeightBit + 3 < 64, "awareness field mask exceeds 64 bits");

        bool jsonToFloat(const json& v, float& out)
        {
            if (const auto* f = v.get_ptr<const json::number_float_t*>())
                out = static_cast<float>(*f);
            else if (const auto* i = v.get_ptr<const json::number_integer_t*>())
                out = static_cast<float>(*i);
            else if (const auto* u = v.get_ptr<const json::number_unsigned_t*>())
                out = static_cast<float>(*u);
            else
                return false;
            return std::isfinite(out);
        }

        void clampField(float& value, bool nonNegative, float hi)
        {
            if (nonNegative && value < 0.0f)
                value = 0.0f;
            if (hi >= 0.0f && value > hi)
                value = hi;
        }

        bool readArray(const json& obj, const char* key, float* dst, int firstBit, uint64_t& mask)
        {
            const auto it = obj.find(key);
            if (it == obj.end() || !it->is_array())
                return false;
            const int count = it->size() > 3 ? 3 : static_cast<int>(it->size());
            for (int i = 0; i < count; ++i)
            {
                float value = 0.0f;
                if (!jsonToFloat((*it)[static_cast<size_t>(i)], value))
                    continue;
                dst[i] = value;
                mask |= 1ull << (firstBit + i);
            }
            return true;
        }

        void writeArray(json& obj, const char* key, const float* src, int firstBit, uint64_t mask)
        {
            const uint64_t bits = (1ull << firstBit) | (1ull << (firstBit + 1)) | (1ull << (firstBit + 2));
            if ((mask & bits) == 0)
                return;
            obj[key] = json::array({ src[0], src[1], src[2] });
        }
    }

    void sanitizeAwarenessSettings(AwarenessSettings& settings)
    {
        for (const Field& field : kFields)
            clampField(settings.*(field.member), field.nonNegative, field.hi);
        for (float& weight : settings.bodyWeight)
            clampField(weight, true, -1.0f);
    }

    void applyAwarenessMask(AwarenessSettings& dst, const AwarenessSettings& src, uint64_t mask)
    {
        for (int i = 0; i < kScalarCount; ++i)
        {
            if ((mask & (1ull << i)) == 0)
                continue;
            dst.*(kFields[i].member) = src.*(kFields[i].member);
        }
        for (int i = 0; i < 3; ++i)
        {
            if ((mask & (1ull << (kBodyStandBit + i))) != 0)
                dst.bodyStandHeight[i] = src.bodyStandHeight[i];
            if ((mask & (1ull << (kBodyCrouchBit + i))) != 0)
                dst.bodyCrouchHeight[i] = src.bodyCrouchHeight[i];
            if ((mask & (1ull << (kBodyWeightBit + i))) != 0)
                dst.bodyWeight[i] = src.bodyWeight[i];
        }
    }

    bool readAwarenessSettings(std::string_view jsonText, AwarenessSettings& inout, uint64_t* writtenMask)
    {
        if (jsonText.empty() || jsonText.size() > kMaxJsonBytes)
        {
            DE_LOG_ERROR(LogCategory::AI, "AwarenessSettings: empty or oversized json");
            return false;
        }
        const json root = json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            DE_LOG_ERROR(LogCategory::AI, "AwarenessSettings: discarded or non-object json");
            return false;
        }
        const auto verIt = root.find("version");
        if (verIt != root.end())
        {
            float ver = 0.0f;
            if (!jsonToFloat(*verIt, ver) || static_cast<int>(ver) != 1)
            {
                DE_LOG_ERROR(LogCategory::AI, "AwarenessSettings: unsupported version");
                return false;
            }
        }

        AwarenessSettings next = inout;
        uint64_t mask = 0;
        for (int i = 0; i < kScalarCount; ++i)
        {
            const auto it = root.find(kFields[i].key);
            if (it == root.end())
                continue;
            float value = 0.0f;
            if (!jsonToFloat(*it, value))
                continue;
            next.*(kFields[i].member) = value;
            mask |= 1ull << i;
        }
        readArray(root, "bodyStandHeight", next.bodyStandHeight, kBodyStandBit, mask);
        readArray(root, "bodyCrouchHeight", next.bodyCrouchHeight, kBodyCrouchBit, mask);
        readArray(root, "bodyWeight", next.bodyWeight, kBodyWeightBit, mask);
        inout = next;
        if (writtenMask)
            *writtenMask = mask;
        return true;
    }

    bool writeAwarenessSettings(const AwarenessSettings& settings, uint64_t mask, std::string& jsonOut)
    {
        json obj;
        for (int i = 0; i < kScalarCount; ++i)
        {
            if ((mask & (1ull << i)) == 0)
                continue;
            obj[kFields[i].key] = settings.*(kFields[i].member);
        }
        writeArray(obj, "bodyStandHeight", settings.bodyStandHeight, kBodyStandBit, mask);
        writeArray(obj, "bodyCrouchHeight", settings.bodyCrouchHeight, kBodyCrouchBit, mask);
        writeArray(obj, "bodyWeight", settings.bodyWeight, kBodyWeightBit, mask);
        jsonOut = obj.dump();
        return true;
    }

    bool loadAwarenessSettings(AwarenessSettings& inout)
    {
        bool loaded = false;
        for (const std::filesystem::path& root : contentRootCandidates())
        {
            if (root.empty())
                continue;
            const std::filesystem::path path = root / "ai/awareness.json";
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec) || ec)
                continue;
            const auto sz = std::filesystem::file_size(path, ec);
            if (ec || sz == 0 || sz > kMaxJsonBytes)
            {
                DE_LOG_ERROR(LogCategory::AI, "AwarenessSettings: unreadable '{}'", path.string());
                break;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
                DE_LOG_ERROR(LogCategory::AI, "AwarenessSettings: failed to open '{}'", path.string());
                break;
            }
            std::ostringstream ss;
            ss << in.rdbuf();
            if (!readAwarenessSettings(ss.str(), inout, nullptr))
            {
                DE_LOG_ERROR(LogCategory::AI, "AwarenessSettings: failed to parse '{}'", path.string());
                break;
            }
            DE_LOG_INFO(LogCategory::AI, "AwarenessSettings: loaded '{}'", path.string());
            loaded = true;
            break;
        }
        sanitizeAwarenessSettings(inout);
        return loaded;
    }

} // namespace Dark::AI
