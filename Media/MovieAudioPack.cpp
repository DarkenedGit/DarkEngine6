#include "Media/MovieAudioPack.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace Dark
{

    namespace
    {

        int16_t loadI16(const uint8_t* p)
        {
            int16_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        float loadF32(const uint8_t* p)
        {
            float v = 0.f;
            std::memcpy(&v, p, sizeof(v));
            return v;
        }

        int16_t clampPcm(float v)
        {
            if (!std::isfinite(v))
                return 0;
            if (v >= 32767.f)
                return 32767;
            if (v <= -32767.f)
                return static_cast<int16_t>(-32767);
            return static_cast<int16_t>(std::round(v));
        }

        int16_t fromFloatSample(float s)
        {
            return clampPcm(s * 32767.f);
        }

        struct Stereo
        {
            int16_t l = 0;
            int16_t r = 0;
        };

        Stereo mixFrame(const uint8_t* frame, uint32_t channels, bool float32)
        {
            Stereo out;
            if (channels == 1)
            {
                if (float32)
                {
                    const int16_t s = fromFloatSample(loadF32(frame));
                    out.l           = s;
                    out.r           = s;
                    return out;
                }
                const int16_t s = loadI16(frame);
                out.l           = s;
                out.r           = s;
                return out;
            }

            if (channels == 2)
            {
                if (float32)
                {
                    out.l = fromFloatSample(loadF32(frame));
                    out.r = fromFloatSample(loadF32(frame + 4));
                    return out;
                }
                out.l = loadI16(frame);
                out.r = loadI16(frame + 2);
                return out;
            }

            float fl = 0.f;
            float fr = 0.f;
            float fc = 0.f;
            float bl = 0.f;
            float br = 0.f;
            if (float32)
            {
                fl = loadF32(frame);
                fr = loadF32(frame + 4);
                fc = loadF32(frame + 8);
                bl = loadF32(frame + 16);
                br = loadF32(frame + 20);
                const float l = (fl + kMovieSurroundGain * fc + kMovieSurroundGain * bl) * 32767.f;
                const float r = (fr + kMovieSurroundGain * fc + kMovieSurroundGain * br) * 32767.f;
                out.l         = clampPcm(l);
                out.r         = clampPcm(r);
                return out;
            }

            fl = static_cast<float>(loadI16(frame));
            fr = static_cast<float>(loadI16(frame + 2));
            fc = static_cast<float>(loadI16(frame + 4));
            bl = static_cast<float>(loadI16(frame + 8));
            br = static_cast<float>(loadI16(frame + 10));
            // 0.707 * full-scale does not fit in an int16 intermediate.
            out.l = clampPcm(fl + kMovieSurroundGain * fc + kMovieSurroundGain * bl);
            out.r = clampPcm(fr + kMovieSurroundGain * fc + kMovieSurroundGain * br);
            return out;
        }

        uint32_t sampleBytes(uint32_t channels, bool float32)
        {
            const uint32_t bps = float32 ? 4u : 2u;
            return channels * bps;
        }

    } // namespace

    bool packMovieAudio(const MovieAudioPackInput& in, std::vector<int16_t>& remainder, std::vector<std::vector<int16_t>>& packets, uint32_t packetFrames)
    {
        if (packetFrames == 0)
            return false;
        if (in.channels != 1 && in.channels != 2 && in.channels != 6)
            return false;
        if (in.frameCount > 0 && !in.samples)
            return false;
        if (in.frameCount > (std::numeric_limits<uint32_t>::max() / 2u))
            return false;

        const uint32_t bytesPerFrame = sampleBytes(in.channels, in.float32);
        if (in.channels > 0 && bytesPerFrame / in.channels != (in.float32 ? 4u : 2u))
            return false;

        const auto*    bytes = static_cast<const uint8_t*>(in.samples);
        std::vector<int16_t> converted;
        converted.reserve(static_cast<size_t>(in.frameCount) * 2u);
        for (uint32_t i = 0; i < in.frameCount; ++i)
        {
            const Stereo s = mixFrame(bytes + static_cast<size_t>(i) * bytesPerFrame, in.channels, in.float32);
            converted.push_back(s.l);
            converted.push_back(s.r);
        }

        remainder.insert(remainder.end(), converted.begin(), converted.end());

        const size_t packetSamples = static_cast<size_t>(packetFrames) * 2u;
        while (remainder.size() >= packetSamples)
        {
            std::vector<int16_t> packet(remainder.begin(), remainder.begin() + static_cast<std::ptrdiff_t>(packetSamples));
            remainder.erase(remainder.begin(), remainder.begin() + static_cast<std::ptrdiff_t>(packetSamples));
            packets.push_back(std::move(packet));
        }
        return true;
    }

} // namespace Dark
