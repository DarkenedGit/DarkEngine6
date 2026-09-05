#include "Audio/WavFile.h"
#include "Core/Log.h"

#include <cstring>
#include <fstream>

namespace Dark::Audio
{
    bool readExact(std::ifstream& f, void* dst, std::streamsize n)
    {
        f.read(static_cast<char*>(dst), n);
        return f.good() && f.gcount() == n;
    }

    int16_t sample8To16(uint8_t s)
    {
        const int centered = static_cast<int>(s) - 128;
        return static_cast<int16_t>(centered * 256);
    }

    bool loadPcmWav(const std::filesystem::path& path, PcmWav& out)
    {
        out = PcmWav{};
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Wav: cannot open '{}'", path.string());
            return false;
        }

        char riff[4]{};
        uint32_t riffSize = 0;
        char wave[4]{};
        if (!readExact(f, riff, 4) || !readExact(f, &riffSize, 4) || !readExact(f, wave, 4))
            return false;
        if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Wav: not a RIFF/WAVE file '{}'", path.string());
            return false;
        }

        uint16_t audioFormat = 0;
        uint16_t channels    = 0;
        uint32_t sampleRate  = 0;
        uint16_t bits        = 0;
        bool     haveFmt     = false;
        std::vector<uint8_t> dataBytes;

        while (f && !f.eof())
        {
            char     id[4]{};
            uint32_t size = 0;
            if (!readExact(f, id, 4) || !readExact(f, &size, 4))
                break;

            if (std::memcmp(id, "fmt ", 4) == 0)
            {
                if (size < 16)
                {
                    DE_LOG_ERROR(LogCategory::Audio, "Wav: fmt chunk too small in '{}'", path.string());
                    return false;
                }
                uint16_t blockAlign = 0;
                uint32_t byteRate   = 0;
                if (!readExact(f, &audioFormat, 2) || !readExact(f, &channels, 2) || !readExact(f, &sampleRate, 4)
                    || !readExact(f, &byteRate, 4) || !readExact(f, &blockAlign, 2) || !readExact(f, &bits, 2))
                {
                    return false;
                }
                const uint32_t remain = size - 16;
                if (remain > 0)
                    f.seekg(remain, std::ios::cur);
                haveFmt = true;
            }
            else if (std::memcmp(id, "data", 4) == 0)
            {
                dataBytes.resize(size);
                if (size > 0 && !readExact(f, dataBytes.data(), static_cast<std::streamsize>(size)))
                    return false;
            }
            else
            {
                f.seekg(size, std::ios::cur);
            }

            if ((size & 1u) != 0)
                f.seekg(1, std::ios::cur);
        }

        if (!haveFmt || dataBytes.empty())
        {
            DE_LOG_ERROR(LogCategory::Audio, "Wav: missing fmt/data in '{}'", path.string());
            return false;
        }
        if (audioFormat != 1)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Wav: only PCM supported (format {}) '{}'", audioFormat, path.string());
            return false;
        }
        if (channels < 1 || channels > 2)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Wav: unsupported channel count {} '{}'", channels, path.string());
            return false;
        }
        if (bits != 8 && bits != 16)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Wav: unsupported bit depth {} '{}'", bits, path.string());
            return false;
        }
        if (sampleRate < 8000 || sampleRate > 96000)
        {
            DE_LOG_ERROR(LogCategory::Audio, "Wav: unsupported sample rate {} '{}'", sampleRate, path.string());
            return false;
        }

        out.channels   = channels;
        out.sampleRate = sampleRate;
        if (bits == 16)
        {
            if ((dataBytes.size() & 1u) != 0)
                dataBytes.pop_back();
            out.samples.resize(dataBytes.size() / 2);
            std::memcpy(out.samples.data(), dataBytes.data(), dataBytes.size());
        }
        else
        {
            out.samples.resize(dataBytes.size());
            for (size_t i = 0; i < dataBytes.size(); ++i)
                out.samples[i] = sample8To16(dataBytes[i]);
        }

        const uint32_t frames = out.frameCount();
        if (frames == 0)
        {
            out = PcmWav{};
            return false;
        }
        return true;
    }

    bool writePcmWav(const std::filesystem::path& path, const PcmWav& wav)
    {
        if (!wav.valid())
            return false;

        const uint16_t channels   = wav.channels;
        const uint32_t sampleRate = wav.sampleRate;
        const uint16_t bits       = 16;
        const uint32_t dataBytes  = static_cast<uint32_t>(wav.samples.size() * sizeof(int16_t));
        const uint16_t blockAlign = static_cast<uint16_t>(channels * (bits / 8));
        const uint32_t byteRate   = sampleRate * blockAlign;
        const uint32_t fmtSize    = 16;
        const uint32_t riffSize   = 4 + (8 + fmtSize) + (8 + dataBytes);

        std::ofstream f(path, std::ios::binary);
        if (!f)
            return false;

        f.write("RIFF", 4);
        f.write(reinterpret_cast<const char*>(&riffSize), 4);
        f.write("WAVE", 4);
        f.write("fmt ", 4);
        f.write(reinterpret_cast<const char*>(&fmtSize), 4);
        const uint16_t pcmFormat = 1;
        f.write(reinterpret_cast<const char*>(&pcmFormat), 2);
        f.write(reinterpret_cast<const char*>(&channels), 2);
        f.write(reinterpret_cast<const char*>(&sampleRate), 4);
        f.write(reinterpret_cast<const char*>(&byteRate), 4);
        f.write(reinterpret_cast<const char*>(&blockAlign), 2);
        f.write(reinterpret_cast<const char*>(&bits), 2);
        f.write("data", 4);
        f.write(reinterpret_cast<const char*>(&dataBytes), 4);
        f.write(reinterpret_cast<const char*>(wav.samples.data()), static_cast<std::streamsize>(dataBytes));
        return f.good();
    }

} // namespace Dark
