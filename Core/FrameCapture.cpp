#include "Core/FrameCapture.h"

#include "Core/Json.h"
#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>

namespace Dark
{
    namespace
    {
        bool isSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n';
        }

        // Splits on spaces. A double-quoted run keeps its spaces and loses the quotes.
        std::vector<std::string> splitCommandLine(const char* cmdLine)
        {
            std::vector<std::string> tokens;
            const char* p = cmdLine ? cmdLine : "";
            while (*p)
            {
                while (*p && isSpace(*p))
                    ++p;
                if (!*p)
                    break;
                std::string token;
                bool        quoted = false;
                while (*p && (quoted || !isSpace(*p)))
                {
                    if (*p == '"')
                        quoted = !quoted;
                    else
                        token.push_back(*p);
                    ++p;
                }
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        bool equalsNoCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            }
            return true;
        }

        bool parseU32(const std::string& text, uint32_t& out)
        {
            if (text.empty())
                return false;
            uint32_t   value = 0;
            const auto res   = std::from_chars(text.data(), text.data() + text.size(), value);
            if (res.ec != std::errc{} || res.ptr != text.data() + text.size())
                return false;
            out = value;
            return true;
        }

        bool fail(std::string* error, std::string message)
        {
            DE_LOG_ERROR(LogCategory::Render, "Frame capture: {}", message);
            if (error)
                *error = std::move(message);
            return false;
        }

        bool readVec3(const nlohmann::json& object, const char* key, float (&out)[3], const std::string& where, std::string* error)
        {
            if (!object.contains(key))
                return fail(error, where + ": \"" + key + "\" is missing");
            const nlohmann::json& value = object[key];
            if (!value.is_array() || value.size() != 3)
                return fail(error, where + ": \"" + key + "\" must be an array of 3 numbers");
            for (size_t i = 0; i < 3; ++i)
            {
                const nlohmann::json& n = value[i];
                if (!n.is_number())
                    return fail(error, where + ": \"" + key + "\" must be an array of 3 numbers");
                const double d = n.get<double>();
                if (!std::isfinite(d))
                    return fail(error, where + ": \"" + key + "\" has a non-finite number");
                out[i] = static_cast<float>(d);
            }
            return true;
        }

        bool visionFromName(std::string_view name, CaptureVision& out)
        {
            static constexpr CaptureVision kAll[] = { CaptureVision::None, CaptureVision::NightSight, CaptureVision::Infrared, CaptureVision::Ultraviolet,
                                                      CaptureVision::Echo };
            if (name.empty())
            {
                out = CaptureVision::None;
                return true;
            }
            for (CaptureVision v : kAll)
            {
                if (equalsNoCase(name, captureVisionName(v)))
                {
                    out = v;
                    return true;
                }
            }
            return false;
        }

        nlohmann::json statJson(const CaptureStat& s)
        {
            nlohmann::json j = nlohmann::json::object();
            j["count"]       = s.count;
            j["mean"]        = s.mean;
            j["min"]         = s.min;
            j["max"]         = s.max;
            j["p50"]         = s.p50;
            j["p95"]         = s.p95;
            return j;
        }
    } // namespace

    bool parseFrameCaptureArgs(const char* cmdLine, FrameCaptureArgs& out, std::string* error)
    {
        FrameCaptureArgs                args{};
        const std::vector<std::string> tokens = splitCommandLine(cmdLine);
        bool                           sawFrame = false;
        auto value = [&](size_t& i, const char* flag, std::string& dst) -> bool {
            if (i + 1 >= tokens.size() || tokens[i + 1].empty() || tokens[i + 1][0] == '-')
                return fail(error, std::string(flag) + " needs a value");
            dst = tokens[++i];
            return true;
        };

        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const std::string& tok = tokens[i];
            std::string        text;
            if (equalsNoCase(tok, "-capture-frame"))
            {
                if (!value(i, "-capture-frame", text))
                    return false;
                if (!parseU32(text, args.frame) || args.frame < 2)
                    return fail(error, "-capture-frame needs a whole number >= 2, got '" + text + "'");
                sawFrame = true;
            }
            else if (equalsNoCase(tok, "-capture-avg"))
            {
                if (!value(i, "-capture-avg", text))
                    return false;
                if (!parseU32(text, args.averageCount) || args.averageCount == 0)
                    return fail(error, "-capture-avg needs a whole number >= 1, got '" + text + "'");
            }
            else if (equalsNoCase(tok, "-camera"))
            {
                if (!value(i, "-camera", args.camera))
                    return false;
            }
            else if (equalsNoCase(tok, "-capture-out"))
            {
                if (!value(i, "-capture-out", args.outDir))
                    return false;
            }
            else if (equalsNoCase(tok, "-capture-stay"))
                args.stay = true;
        }

        if (!sawFrame)
        {
            if (!args.camera.empty())
                return fail(error, "-camera needs -capture-frame N");
            out = args;
            return true;
        }
        if (args.camera.empty())
            return fail(error, "-capture-frame needs -camera <preset>");
        args.enabled = true;
        out          = std::move(args);
        return true;
    }

    const char* captureVisionName(CaptureVision vision)
    {
        switch (vision)
        {
        case CaptureVision::None:
            return "none";
        case CaptureVision::NightSight:
            return "nightSight";
        case CaptureVision::Infrared:
            return "infrared";
        case CaptureVision::Ultraviolet:
            return "ultraviolet";
        case CaptureVision::Echo:
            return "echo";
        }
        return "none";
    }

    bool parseCapturePresets(std::string_view jsonText, std::vector<CapturePreset>& out, std::string* error)
    {
        const nlohmann::json root = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return fail(error, "camera presets are not a JSON object");
        if (!root.contains("presets") || !root["presets"].is_array())
            return fail(error, "camera presets need a \"presets\" array");

        std::vector<CapturePreset> presets;
        const nlohmann::json&      list = root["presets"];
        for (size_t i = 0; i < list.size(); ++i)
        {
            const nlohmann::json& item  = list[i];
            const std::string     where = "presets[" + std::to_string(i) + "]";
            if (!item.is_object())
                return fail(error, where + " is not an object");

            CapturePreset p{};
            std::string   anchor;
            std::string   vision;
            std::string   why;
            if (!jsonStringOr(item, "name", p.name, "", where.c_str(), &why) || !jsonStringOr(item, "host", p.host, "", where.c_str(), &why)
                || !jsonStringOr(item, "note", p.note, "", where.c_str(), &why) || !jsonStringOr(item, "anchor", anchor, "world", where.c_str(), &why)
                || !jsonStringOr(item, "vision", vision, "none", where.c_str(), &why)
                || !jsonFloatOr(item, "fovYDegrees", p.fovYDegrees, 0.0f, where.c_str(), &why)
                || !jsonFloatOr(item, "visionAmount", p.visionAmount, 1.0f, where.c_str(), &why)
                || !jsonFloatOr(item, "echoAgeSeconds", p.echoAgeSeconds, 1.0f, where.c_str(), &why)
                || !jsonBoolOr(item, "camouflage", p.camouflage, false, where.c_str(), &why)
                || !jsonBoolOr(item, "underwater", p.underwater, false, where.c_str(), &why))
                return fail(error, where + ": " + why);
            if (p.name.empty())
                return fail(error, where + ": \"name\" is missing");
            for (const CapturePreset& seen : presets)
            {
                if (seen.name == p.name && seen.host == p.host)
                    return fail(error, where + ": duplicate preset \"" + p.name + "\"");
            }
            if (equalsNoCase(anchor, "world"))
                p.anchor = CaptureAnchor::World;
            else if (equalsNoCase(anchor, "player"))
                p.anchor = CaptureAnchor::Player;
            else
                return fail(error, where + ": \"anchor\" must be \"world\" or \"player\", got \"" + anchor + "\"");
            if (!visionFromName(vision, p.vision))
                return fail(error, where + ": unknown \"vision\" \"" + vision + "\"");
            if (!(p.visionAmount >= 0.0f && p.visionAmount <= 1.0f))
                return fail(error, where + ": \"visionAmount\" must be in 0..1");
            if (p.fovYDegrees < 0.0f || p.fovYDegrees >= 179.0f)
                return fail(error, where + ": \"fovYDegrees\" must be 0 (host lens) or in (0, 179)");
            if (p.echoAgeSeconds < 0.0f)
                return fail(error, where + ": \"echoAgeSeconds\" must be >= 0");
            if (!readVec3(item, "eye", p.eye, where, error) || !readVec3(item, "target", p.target, where, error))
                return false;
            const float dx = p.target[0] - p.eye[0];
            const float dy = p.target[1] - p.eye[1];
            const float dz = p.target[2] - p.eye[2];
            if (dx * dx + dy * dy + dz * dz < 1e-8f)
                return fail(error, where + ": \"eye\" and \"target\" are the same point");
            presets.push_back(std::move(p));
        }
        out = std::move(presets);
        return true;
    }

    const CapturePreset* findCapturePreset(const std::vector<CapturePreset>& presets, std::string_view name, std::string_view host)
    {
        const CapturePreset* any = nullptr;
        for (const CapturePreset& p : presets)
        {
            if (p.name != name)
                continue;
            if (equalsNoCase(p.host, host))
                return &p;
            if (p.host.empty() && !any)
                any = &p;
        }
        return any;
    }

    CaptureStat captureStat(std::vector<float> values)
    {
        CaptureStat s{};
        if (values.empty())
            return s;
        std::sort(values.begin(), values.end());
        double sum = 0.0;
        for (float v : values)
            sum += v;
        const size_t n = values.size();
        auto rank = [&](double pct) {
            size_t r = static_cast<size_t>(std::ceil(pct * static_cast<double>(n)));
            if (r < 1)
                r = 1;
            if (r > n)
                r = n;
            return values[r - 1];
        };
        s.count = static_cast<uint32_t>(n);
        s.mean  = static_cast<float>(sum / static_cast<double>(n));
        s.min   = values.front();
        s.max   = values.back();
        s.p50   = rank(0.50);
        s.p95   = rank(0.95);
        return s;
    }

    void CaptureTimingAccumulator::clear()
    {
        m_frameMs.clear();
        m_cpuMs.clear();
        m_gpuMs.clear();
        m_drawCalls.clear();
        m_ranges.clear();
    }

    void CaptureTimingAccumulator::add(const CaptureFrameSample& sample)
    {
        m_frameMs.push_back(sample.frameMs);
        m_cpuMs.push_back(sample.cpuMs);
        if (sample.gpuValid)
            m_gpuMs.push_back(sample.gpuMs);
        m_drawCalls.push_back(static_cast<float>(sample.drawCalls));
        for (const CaptureRangeSample& r : sample.ranges)
        {
            RangeTotals* totals = nullptr;
            for (RangeTotals& t : m_ranges)
            {
                if (t.depth == r.depth && t.name == r.name)
                {
                    totals = &t;
                    break;
                }
            }
            if (!totals)
            {
                m_ranges.push_back(RangeTotals{ r.name, r.depth, {}, {} });
                totals = &m_ranges.back();
            }
            totals->cpuMs.push_back(r.cpuMs);
            if (r.gpuMs >= 0.0f)
                totals->gpuMs.push_back(r.gpuMs);
        }
    }

    std::string captureReportJson(const CaptureReportInfo& info, const CaptureTimingAccumulator& timings)
    {
        nlohmann::json j    = nlohmann::json::object();
        j["schemaVersion"]  = 1;
        j["host"]           = info.host;
        j["preset"]         = info.preset;
        j["png"]            = info.png;
        j["shotOk"]         = info.shotOk;
        j["passSequenceFailures"] = info.passSequenceFailures;
        j["baselineValid"]  = info.shotOk && timings.frames() > 0 && info.passSequenceFailures == 0;
        j["build"]          = info.build;
        j["version"]        = info.version;
        j["git"]            = info.git;
        j["shaderCompiler"] = info.shaderCompiler;
        j["frame"]          = info.frame;
        j["width"]          = info.width;
        j["height"]         = info.height;
        j["vsync"]          = info.vsync;
        j["framesAveraged"] = timings.frames();
        j["frameMs"]        = statJson(timings.frameStat());
        j["cpuMs"]          = statJson(timings.cpuStat());
        j["gpuMs"]          = statJson(timings.gpuStat());
        j["drawCalls"]      = statJson(timings.drawCallStat());

        nlohmann::json ranges = nlohmann::json::array();
        for (const CaptureTimingAccumulator::RangeTotals& r : timings.ranges())
        {
            nlohmann::json row = nlohmann::json::object();
            row["name"]        = r.name;
            row["depth"]       = r.depth;
            row["cpuMs"]       = statJson(captureStat(r.cpuMs));
            row["gpuMs"]       = statJson(captureStat(r.gpuMs));
            ranges.push_back(std::move(row));
        }
        j["ranges"] = std::move(ranges);
        return j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
    }

    std::string captureFileStem(std::string_view host, std::string_view preset)
    {
        std::string stem;
        stem.reserve(host.size() + preset.size() + 1);
        auto append = [&](std::string_view part) {
            for (char c : part)
            {
                const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
                stem.push_back(ok ? c : '_');
            }
        };
        append(host.empty() ? std::string_view("app") : host);
        stem.push_back('_');
        append(preset.empty() ? std::string_view("preset") : preset);
        return stem;
    }

    void FrameCaptureSchedule::configure(uint32_t shotFrame, uint32_t averageCount)
    {
        *this          = FrameCaptureSchedule{};
        m_shotFrame    = shotFrame < 2 ? 2 : shotFrame;
        m_averageCount = averageCount == 0 ? 1 : averageCount;
    }

    uint32_t FrameCaptureSchedule::firstRecordedFrame() const
    {
        return m_shotFrame > m_averageCount ? m_shotFrame - m_averageCount : 1u;
    }

    CaptureFrameAction FrameCaptureSchedule::step(bool sceneReady, float wallSeconds)
    {
        CaptureFrameAction a{};
        if (m_finished || m_shotFrame == 0)
            return a;
        if (!m_armed)
        {
            if (!sceneReady)
            {
                if (m_waitStart < 0.0f)
                    m_waitStart = wallSeconds;
                else if (wallSeconds - m_waitStart > kReadyTimeoutSeconds)
                {
                    m_finished = true;
                    a.timedOut = true;
                }
                return a;
            }
            m_armed = true;
            a.firstFrame = true;
        }
        ++m_frame;
        a.applyPreset = true;
        if (m_frame == m_shotFrame)
        {
            a.shoot    = true;
            m_finished = true;
        }
        else if (m_frame >= firstRecordedFrame())
            a.record = true;
        return a;
    }

} // namespace Dark
