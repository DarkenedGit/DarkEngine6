#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    // Render graph Phase 0 baselines: run a host to a fixed frame from a named camera preset,
    // save that frame as a PNG and the frames before it as averaged timings, then quit.
    //
    //   -capture-frame N      shoot frame N, counted from the first frame the host reports its scene ready (N >= 2)
    //   -camera <preset>      preset name in content/perf/capture_cameras.json (required with -capture-frame)
    //   -capture-avg K        average the K frames before N (default 300; fewer when N - 1 < K)
    //   -capture-out <dir>    output folder (default captures/ under the working directory)
    //   -capture-stay         keep running after the shot
    //
    // No D3D12 here. Application drives it, the Renderer reads back the back buffer, and the
    // hosts apply the preset to their own camera and vision state.

    struct FrameCaptureArgs
    {
        bool        enabled      = false; // -capture-frame was given
        uint32_t    frame        = 0;
        uint32_t    averageCount = 300;
        bool        stay         = false;
        std::string camera;
        std::string outDir;
    };

    // Reads only the capture tokens; every other token is ignored. A bad value logs, leaves
    // enabled false, and returns false. Quoted values ("D:\perf runs\fxc") keep their spaces.
    bool parseFrameCaptureArgs(const char* cmdLine, FrameCaptureArgs& out, std::string* error = nullptr);

    enum class CaptureAnchor : uint8_t
    {
        World,  // eye and target are world positions
        Player, // eye and target are offsets from the possessed body's position (Sandbox only)
    };

    enum class CaptureVision : uint8_t
    {
        None,
        NightSight,
        Infrared,
        Ultraviolet,
        Echo,
    };

    struct CapturePreset
    {
        std::string   name;
        std::string   host; // "sandbox", "editor"; empty = any host
        std::string   note;
        CaptureAnchor anchor = CaptureAnchor::World;
        float         eye[3]{ 0.0f, 0.0f, 0.0f };
        float         target[3]{ 0.0f, 0.0f, 1.0f };
        float         fovYDegrees = 0.0f; // 0 keeps the host's lens
        CaptureVision vision       = CaptureVision::None;
        float         visionAmount = 1.0f; // 0..1; values below 1 pin a mid-fade
        float         echoAgeSeconds = 1.0f; // pinned chirp age, so the ring sits at a fixed radius
        bool          camouflage   = false;
        // eye.y and target.y are heights relative to the scene water level (replacing the
        // anchor's height), and the host pins its underwater amount from that eye height.
        bool          underwater   = false;
    };

    // { "presets": [ { "name": ..., "host": ..., "eye": [x,y,z], "target": [x,y,z], ... } ] }
    // Unknown keys are ignored. A wrong type or a missing name/eye/target rejects that file.
    bool parseCapturePresets(std::string_view jsonText, std::vector<CapturePreset>& out, std::string* error = nullptr);
    // Exact name match; a preset for another host is skipped.
    const CapturePreset* findCapturePreset(const std::vector<CapturePreset>& presets, std::string_view name, std::string_view host);
    const char*          captureVisionName(CaptureVision vision);

    // One frame of timings, filled by Application from the profiler snapshot.
    struct CaptureRangeSample
    {
        std::string name;
        uint16_t    depth = 0;
        float       cpuMs = 0.0f;
        float       gpuMs = -1.0f; // negative = no GPU timestamp
    };

    struct CaptureFrameSample
    {
        float                           frameMs   = 0.0f;
        float                           cpuMs     = 0.0f;
        float                           gpuMs     = 0.0f;
        bool                            gpuValid  = false;
        uint32_t                        drawCalls = 0;
        uint32_t                        triangles = 0;
        std::vector<CaptureRangeSample> ranges;
    };

    struct CaptureStat
    {
        uint32_t count = 0;
        float    mean  = 0.0f;
        float    min   = 0.0f;
        float    max   = 0.0f;
        float    p50   = 0.0f;
        float    p95   = 0.0f;
    };

    // Nearest-rank percentiles over the stored values. Empty input gives count 0 and zeros.
    CaptureStat captureStat(std::vector<float> values);

    class CaptureTimingAccumulator
    {
    public:
        struct RangeTotals
        {
            std::string        name;
            uint16_t           depth = 0;
            std::vector<float> cpuMs;
            std::vector<float> gpuMs; // only frames that had a GPU timestamp
        };

        void     clear();
        void     add(const CaptureFrameSample& sample);
        uint32_t frames() const { return static_cast<uint32_t>(m_frameMs.size()); }

        CaptureStat frameStat() const { return captureStat(m_frameMs); }
        CaptureStat cpuStat() const { return captureStat(m_cpuMs); }
        CaptureStat gpuStat() const { return captureStat(m_gpuMs); }
        CaptureStat drawCallStat() const { return captureStat(m_drawCalls); }
        // First-seen order; a range is keyed by name and depth.
        const std::vector<RangeTotals>& ranges() const { return m_ranges; }

    private:
        std::vector<float>       m_frameMs;
        std::vector<float>       m_cpuMs;
        std::vector<float>       m_gpuMs;
        std::vector<float>       m_drawCalls;
        std::vector<RangeTotals> m_ranges;
    };

    struct CaptureReportInfo
    {
        std::string host;
        std::string preset;
        std::string png;      // file name written next to the report, empty when the shot failed
        std::string build;    // "Debug" / "Release"
        std::string version;
        std::string git;
        std::string shaderCompiler; // "FXC" until Phase 2
        uint32_t    frame  = 0;
        uint32_t    width  = 0;
        uint32_t    height = 0;
        bool        vsync  = false;
        bool        shotOk = false;
        uint32_t    passSequenceFailures = 0; // nonzero invalidates this baseline run
    };

    // Pretty-printed JSON for docs/perf/. Never aborts on odd range names (invalid UTF-8 is replaced).
    std::string captureReportJson(const CaptureReportInfo& info, const CaptureTimingAccumulator& timings);
    // "<host>_<preset>" with anything outside [A-Za-z0-9_-] replaced by '_'.
    std::string captureFileStem(std::string_view host, std::string_view preset);

    struct CaptureFrameAction
    {
        bool applyPreset = false; // host pins camera and vision this frame
        bool firstFrame  = false; // first pinned frame: reset temporal history, set one-shot state
        bool shoot       = false; // read back this frame's back buffer
        bool record      = false; // add this frame's timings
        bool timedOut    = false; // scene never became ready
    };

    // Frame counting for one capture. k = 1 is the first frame the host reports ready.
    // Frames [max(1, N - K), N - 1] are recorded; frame N is shot (its readback stall is not timed).
    class FrameCaptureSchedule
    {
    public:
        static constexpr float kReadyTimeoutSeconds = 600.0f;

        void configure(uint32_t shotFrame, uint32_t averageCount);
        // Call once per frame before the host update. wallSeconds is any monotonic clock.
        CaptureFrameAction step(bool sceneReady, float wallSeconds);
        bool     armed() const { return m_armed; }
        bool     finished() const { return m_finished; }
        uint32_t frameInCapture() const { return m_frame; }
        uint32_t firstRecordedFrame() const;

    private:
        uint32_t m_shotFrame    = 0;
        uint32_t m_averageCount = 0;
        uint32_t m_frame        = 0;
        float    m_waitStart    = -1.0f;
        bool     m_armed        = false;
        bool     m_finished     = false;
    };

} // namespace Dark
