#include <gtest/gtest.h>

#include "Core/FrameCapture.h"

#include <string>
#include <vector>

using namespace Dark;

TEST(FrameCaptureArgs, AbsentMeansDisabled)
{
    FrameCaptureArgs args;
    EXPECT_TRUE(parseFrameCaptureArgs("-no-splash -host", args));
    EXPECT_FALSE(args.enabled);
    EXPECT_TRUE(parseFrameCaptureArgs(nullptr, args));
    EXPECT_FALSE(args.enabled);
}

TEST(FrameCaptureArgs, FullSet)
{
    FrameCaptureArgs args;
    ASSERT_TRUE(parseFrameCaptureArgs("-no-vsync -capture-frame 600 -camera infrared_half -capture-avg 120 -capture-out \"D:\\perf runs\\fxc\" -capture-stay", args));
    EXPECT_TRUE(args.enabled);
    EXPECT_EQ(args.frame, 600u);
    EXPECT_EQ(args.averageCount, 120u);
    EXPECT_EQ(args.camera, "infrared_half");
    EXPECT_EQ(args.outDir, "D:\\perf runs\\fxc");
    EXPECT_TRUE(args.stay);
}

TEST(FrameCaptureArgs, DefaultsAndCase)
{
    FrameCaptureArgs args;
    ASSERT_TRUE(parseFrameCaptureArgs("-Capture-Frame 2 -CAMERA default", args));
    EXPECT_TRUE(args.enabled);
    EXPECT_EQ(args.frame, 2u);
    EXPECT_EQ(args.averageCount, 300u);
    EXPECT_TRUE(args.outDir.empty());
    EXPECT_FALSE(args.stay);
}

TEST(FrameCaptureArgs, Rejects)
{
    const char* bad[] = {
        "-capture-frame",                       // no value
        "-capture-frame 1 -camera a",           // N < 2
        "-capture-frame 12x -camera a",         // not a number
        "-capture-frame 600",                   // no camera
        "-camera a",                            // camera without frame
        "-capture-frame 600 -camera -no-menu",  // flag as value
        "-capture-frame 600 -camera a -capture-avg 0",
        "-capture-frame 99999999999 -camera a", // overflow
    };
    for (const char* line : bad)
    {
        FrameCaptureArgs args;
        args.enabled = true;
        std::string error;
        EXPECT_FALSE(parseFrameCaptureArgs(line, args, &error)) << line;
        EXPECT_FALSE(error.empty()) << line;
    }
}

TEST(CapturePresets, ParsesFieldsAndDefaults)
{
    const char* json = R"({ "presets": [
        { "name": "default", "host": "sandbox", "anchor": "player", "eye": [0, 2.5, -6], "target": [0, 1, 6] },
        { "name": "uv_half", "host": "sandbox", "eye": [1, 2, 3], "target": [1, 2, 4], "vision": "ultraviolet", "visionAmount": 0.5,
          "fovYDegrees": 70, "camouflage": true, "underwater": true, "echoAgeSeconds": 0.25, "note": "n", "extra": 5 },
        { "name": "default", "host": "editor", "eye": [8, 6, -10], "target": [0, 0, 0] }
    ] })";
    std::vector<CapturePreset> presets;
    std::string                error;
    ASSERT_TRUE(parseCapturePresets(json, presets, &error)) << error;
    ASSERT_EQ(presets.size(), 3u);
    EXPECT_EQ(presets[0].anchor, CaptureAnchor::Player);
    EXPECT_EQ(presets[0].vision, CaptureVision::None);
    EXPECT_FLOAT_EQ(presets[0].visionAmount, 1.0f);
    EXPECT_FLOAT_EQ(presets[0].fovYDegrees, 0.0f);
    EXPECT_FLOAT_EQ(presets[0].eye[1], 2.5f);
    EXPECT_EQ(presets[1].anchor, CaptureAnchor::World);
    EXPECT_EQ(presets[1].vision, CaptureVision::Ultraviolet);
    EXPECT_FLOAT_EQ(presets[1].visionAmount, 0.5f);
    EXPECT_FLOAT_EQ(presets[1].fovYDegrees, 70.0f);
    EXPECT_TRUE(presets[1].camouflage);
    EXPECT_TRUE(presets[1].underwater);
    EXPECT_FLOAT_EQ(presets[1].echoAgeSeconds, 0.25f);

    const CapturePreset* sandbox = findCapturePreset(presets, "default", "sandbox");
    const CapturePreset* editor  = findCapturePreset(presets, "default", "editor");
    ASSERT_NE(sandbox, nullptr);
    ASSERT_NE(editor, nullptr);
    EXPECT_EQ(sandbox->host, "sandbox");
    EXPECT_EQ(editor->host, "editor");
    EXPECT_EQ(findCapturePreset(presets, "uv_half", "editor"), nullptr);
    EXPECT_EQ(findCapturePreset(presets, "missing", "sandbox"), nullptr);
}

TEST(CapturePresets, AnyHostPresetMatchesEveryHost)
{
    std::vector<CapturePreset> presets;
    ASSERT_TRUE(parseCapturePresets(R"({"presets":[{"name":"a","eye":[0,0,0],"target":[0,0,1]}]})", presets));
    EXPECT_NE(findCapturePreset(presets, "a", "sandbox"), nullptr);
    EXPECT_NE(findCapturePreset(presets, "a", "editor"), nullptr);
}

TEST(CapturePresets, RejectsBadFiles)
{
    const char* bad[] = {
        "not json",
        "[]",
        R"({"presets": {}})",
        R"({"presets": [ 5 ]})",
        R"({"presets": [ {"eye":[0,0,0],"target":[0,0,1]} ]})",
        R"({"presets": [ {"name":"a","target":[0,0,1]} ]})",
        R"({"presets": [ {"name":"a","eye":[0,0],"target":[0,0,1]} ]})",
        R"({"presets": [ {"name":"a","eye":[0,"x",0],"target":[0,0,1]} ]})",
        R"({"presets": [ {"name":"a","eye":[0,0,0],"target":[0,0,0]} ]})",
        R"({"presets": [ {"name":"a","eye":[0,0,0],"target":[0,0,1],"vision":"xray"} ]})",
        R"({"presets": [ {"name":"a","eye":[0,0,0],"target":[0,0,1],"anchor":"moon"} ]})",
        R"({"presets": [ {"name":"a","eye":[0,0,0],"target":[0,0,1],"visionAmount":1.5} ]})",
        R"({"presets": [ {"name":"a","eye":[0,0,0],"target":[0,0,1],"fovYDegrees":"wide"} ]})",
        R"({"presets": [ {"name":"a","eye":[0,0,0],"target":[0,0,1]}, {"name":"a","eye":[0,0,0],"target":[0,0,1]} ]})",
    };
    for (const char* text : bad)
    {
        std::vector<CapturePreset> presets{ CapturePreset{} };
        std::string                error;
        EXPECT_FALSE(parseCapturePresets(text, presets, &error)) << text;
        EXPECT_FALSE(error.empty()) << text;
        EXPECT_EQ(presets.size(), 1u) << "output untouched on failure: " << text;
    }
}

TEST(CapturePresets, ShippedFileParses)
{
    // The real content file, read the way Application reads it.
    const char* candidates[] = { "content/perf/capture_cameras.json", "../content/perf/capture_cameras.json", "../../content/perf/capture_cameras.json",
                                 "../../../content/perf/capture_cameras.json" };
    for (const char* path : candidates)
    {
        FILE* f = nullptr;
#if defined(_MSC_VER)
        if (fopen_s(&f, path, "rb") != 0)
            f = nullptr;
#else
        f = std::fopen(path, "rb");
#endif
        if (!f)
            continue;
        std::string text;
        char        buf[4096];
        size_t      n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
            text.append(buf, n);
        std::fclose(f);
        std::vector<CapturePreset> presets;
        std::string                error;
        ASSERT_TRUE(parseCapturePresets(text, presets, &error)) << path << ": " << error;
        EXPECT_NE(findCapturePreset(presets, "default", "sandbox"), nullptr);
        EXPECT_NE(findCapturePreset(presets, "default", "editor"), nullptr);
        for (const char* mode : { "night_sight", "infrared", "ultraviolet", "echo", "camouflage", "underwater" })
            EXPECT_NE(findCapturePreset(presets, mode, "sandbox"), nullptr) << mode;
        return;
    }
    GTEST_SKIP() << "content/perf/capture_cameras.json not found from the test working directory";
}

TEST(CaptureStat, NearestRank)
{
    const CaptureStat empty = captureStat({});
    EXPECT_EQ(empty.count, 0u);
    EXPECT_FLOAT_EQ(empty.mean, 0.0f);

    std::vector<float> v;
    for (int i = 100; i >= 1; --i)
        v.push_back(static_cast<float>(i));
    const CaptureStat s = captureStat(v);
    EXPECT_EQ(s.count, 100u);
    EXPECT_FLOAT_EQ(s.mean, 50.5f);
    EXPECT_FLOAT_EQ(s.min, 1.0f);
    EXPECT_FLOAT_EQ(s.max, 100.0f);
    EXPECT_FLOAT_EQ(s.p50, 50.0f);
    EXPECT_FLOAT_EQ(s.p95, 95.0f);

    const CaptureStat one = captureStat({ 3.0f });
    EXPECT_FLOAT_EQ(one.p50, 3.0f);
    EXPECT_FLOAT_EQ(one.p95, 3.0f);
}

TEST(CaptureTimingAccumulator, KeysRangesByNameAndDepth)
{
    CaptureTimingAccumulator acc;
    CaptureFrameSample       a;
    a.frameMs  = 16.0f;
    a.cpuMs    = 4.0f;
    a.gpuMs    = 10.0f;
    a.gpuValid = true;
    a.ranges   = { { "GBuffer", 0, 1.0f, 2.0f }, { "Terrain", 1, 0.5f, 1.0f }, { "ECS Update", 0, 0.7f, -1.0f } };
    CaptureFrameSample b = a;
    b.gpuValid = false; // first frames before timestamps resolve
    b.ranges   = { { "Terrain", 2, 0.25f, 0.5f }, { "GBuffer", 0, 3.0f, 4.0f } };
    acc.add(a);
    acc.add(b);

    EXPECT_EQ(acc.frames(), 2u);
    EXPECT_EQ(acc.gpuStat().count, 1u);
    EXPECT_EQ(acc.cpuStat().count, 2u);
    ASSERT_EQ(acc.ranges().size(), 4u);
    EXPECT_EQ(acc.ranges()[0].name, "GBuffer");
    EXPECT_EQ(acc.ranges()[0].cpuMs.size(), 2u);
    EXPECT_FLOAT_EQ(captureStat(acc.ranges()[0].gpuMs).mean, 3.0f);
    EXPECT_EQ(acc.ranges()[2].name, "ECS Update");
    EXPECT_TRUE(acc.ranges()[2].gpuMs.empty());
    EXPECT_EQ(acc.ranges()[3].depth, 2);

    acc.clear();
    EXPECT_EQ(acc.frames(), 0u);
    EXPECT_TRUE(acc.ranges().empty());
}

TEST(CaptureReport, JsonHasTheBaselineFields)
{
    CaptureTimingAccumulator acc;
    CaptureFrameSample       s;
    s.frameMs  = 16.6f;
    s.cpuMs    = 5.0f;
    s.gpuMs    = 9.0f;
    s.gpuValid = true;
    s.ranges   = { { std::string("Bad\xff" "Name"), 0, 1.0f, 2.0f } };
    acc.add(s);
    CaptureReportInfo info;
    info.host           = "sandbox";
    info.preset         = "default";
    info.png            = "sandbox_default.png";
    info.shotOk         = true;
    info.shaderCompiler = "FXC";
    info.frame          = 600;
    const std::string json = captureReportJson(info, acc);
    for (const char* key : { "\"host\": \"sandbox\"", "\"preset\": \"default\"", "\"shaderCompiler\": \"FXC\"", "\"framesAveraged\": 1", "\"gpuMs\"",
                             "\"ranges\"", "\"baselineValid\": true", "\"passSequenceFailures\": 0" })
        EXPECT_NE(json.find(key), std::string::npos) << key;

    info.passSequenceFailures = 3;
    EXPECT_NE(captureReportJson(info, acc).find("\"baselineValid\": false"), std::string::npos);
    info.passSequenceFailures = 0;
    info.shotOk               = false;
    EXPECT_NE(captureReportJson(info, acc).find("\"baselineValid\": false"), std::string::npos);
}

TEST(CaptureReport, FileStemIsFileSystemSafe)
{
    EXPECT_EQ(captureFileStem("sandbox", "infrared_half"), "sandbox_infrared_half");
    EXPECT_EQ(captureFileStem("editor", "a b/c:d"), "editor_a_b_c_d");
    EXPECT_EQ(captureFileStem("", ""), "app_preset");
}

TEST(FrameCaptureSchedule, WaitsForReadyThenRecordsAndShoots)
{
    FrameCaptureSchedule sched;
    sched.configure(6, 3);
    EXPECT_EQ(sched.firstRecordedFrame(), 3u);

    // Not ready: nothing happens and nothing is counted.
    for (int i = 0; i < 5; ++i)
    {
        const CaptureFrameAction a = sched.step(false, static_cast<float>(i));
        EXPECT_FALSE(a.applyPreset);
        EXPECT_FALSE(a.timedOut);
    }
    EXPECT_FALSE(sched.armed());

    std::vector<CaptureFrameAction> got;
    for (int i = 0; i < 6; ++i)
        got.push_back(sched.step(true, 10.0f));
    EXPECT_TRUE(got[0].firstFrame);
    for (size_t i = 1; i < got.size(); ++i)
        EXPECT_FALSE(got[i].firstFrame);
    for (const CaptureFrameAction& a : got)
        EXPECT_TRUE(a.applyPreset);
    // Frames 1..2 warm up, 3..5 are timed, 6 is shot and not timed.
    EXPECT_FALSE(got[0].record);
    EXPECT_FALSE(got[1].record);
    EXPECT_TRUE(got[2].record);
    EXPECT_TRUE(got[3].record);
    EXPECT_TRUE(got[4].record);
    EXPECT_FALSE(got[5].record);
    EXPECT_TRUE(got[5].shoot);
    EXPECT_TRUE(sched.finished());

    const CaptureFrameAction after = sched.step(true, 11.0f);
    EXPECT_FALSE(after.applyPreset);
    EXPECT_FALSE(after.shoot);
}

TEST(FrameCaptureSchedule, ShortRunRecordsFromFrameOne)
{
    FrameCaptureSchedule sched;
    sched.configure(3, 300);
    EXPECT_EQ(sched.firstRecordedFrame(), 1u);
    EXPECT_TRUE(sched.step(true, 0.0f).record);
    EXPECT_TRUE(sched.step(true, 0.0f).record);
    const CaptureFrameAction shot = sched.step(true, 0.0f);
    EXPECT_TRUE(shot.shoot);
    EXPECT_FALSE(shot.record);
}

TEST(FrameCaptureSchedule, TimesOutWhenNeverReady)
{
    FrameCaptureSchedule sched;
    sched.configure(10, 5);
    EXPECT_FALSE(sched.step(false, 100.0f).timedOut);
    EXPECT_FALSE(sched.step(false, 100.0f + FrameCaptureSchedule::kReadyTimeoutSeconds - 1.0f).timedOut);
    EXPECT_TRUE(sched.step(false, 100.0f + FrameCaptureSchedule::kReadyTimeoutSeconds + 1.0f).timedOut);
    EXPECT_TRUE(sched.finished());
    EXPECT_FALSE(sched.step(true, 1000.0f).applyPreset);
}
