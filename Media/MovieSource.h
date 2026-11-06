#pragma once

#include "Media/MovieStatus.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Dark
{

    struct MovieSourceRequest
    {
        std::wstring absolutePath;
        int32_t      adapterLow  = 0;
        int32_t adapterHigh = 0;
        void*   device      = nullptr;
    };

    struct MovieSourceInfo
    {
        bool        hardware      = false;
        bool        hasAudio      = false;
        bool        fullRange     = false;
        bool        bt709         = true;
        bool        flip180       = false;
        uint32_t    width         = 0;
        uint32_t    height        = 0;
        uint32_t    fpsNum        = 30;
        uint32_t    fpsDen        = 1;
        uint32_t    sampleRate    = 48000;
        uint32_t    audioChannels = 0;
        uint32_t    colorMode     = 1;
        double      frameDuration = 1.0 / 30.0;
        double      durationSec   = 0.0;
        std::string codec;
    };

    struct MovieVideoView
    {
        double               pts        = 0.0;
        double               duration   = 0.0;
        uint32_t             surface    = 0;
        uint64_t             fenceValue = 0;
        bool                 hardware   = false;
        std::vector<uint8_t> bgra;
    };

    struct MovieAudioView
    {
        std::vector<std::vector<int16_t>> packets;
        double                            pts      = 0.0;
        double                            duration = 0.0;
    };

    enum class MovieReadKind : uint8_t
    {
        None = 0,
        Video,
        Audio,
        Error,
    };

    struct MovieRead
    {
        MovieReadKind  kind      = MovieReadKind::None;
        MovieStatus    status    = MovieStatus::Ok;
        bool           hasSample = false;
        bool           end       = false;
        float          decodeMs  = 0.0f;
        MovieVideoView video;
        MovieAudioView audio;
    };

    // Media Foundation source reader and the private D3D11 video device. The decode thread is the only caller.
    class MovieSource
    {
    public:
        MovieSource();
        ~MovieSource();

        MovieSource(const MovieSource&)            = delete;
        MovieSource& operator=(const MovieSource&) = delete;
        MovieSource(MovieSource&&)                 = delete;
        MovieSource& operator=(MovieSource&&)      = delete;

        static MovieStatus startup();
        static void        shutdown();

        MovieStatus open(const MovieSourceRequest& request, MovieSourceInfo& info);
        void        close();

        // One stream per call. preferVideo is skipped when that stream has ended.
        // surface is the NV12 pool slot the worker may copy into. Audio ignores it.
        void read(bool preferVideo, uint32_t surface, MovieRead& out);

        MovieStatus setPosition(double seconds);

        void* nv12(uint32_t surface) const;
        void* decodeFence() const;
        const MovieSourceInfo& info() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };

} // namespace Dark
