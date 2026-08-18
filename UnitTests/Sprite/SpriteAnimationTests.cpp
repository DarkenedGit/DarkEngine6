#include <gtest/gtest.h>

#include "Sprite/SpriteAnimator.h"
#include "Sprite/SpriteSheet.h"

using namespace Dark;

TEST(SpriteSheet, LayoutUvNoInset)
{
    SpriteSheetDesc desc{};
    desc.columns    = 4;
    desc.rows       = 2;
    desc.frameCount = 8;
    desc.inset      = false;

    SpriteSheet sheet;
    ASSERT_TRUE(sheet.createLayout(128, 64, desc));
    EXPECT_EQ(sheet.frameCount(), 8u);

    const SpriteUvRect a = sheet.uvForFrame(0);
    EXPECT_NEAR(a.u, 0.0f, 1.0e-5f);
    EXPECT_NEAR(a.v, 0.0f, 1.0e-5f);
    EXPECT_NEAR(a.du, 0.25f, 1.0e-5f);
    EXPECT_NEAR(a.dv, 0.5f, 1.0e-5f);

    const SpriteUvRect b = sheet.uvForFrame(5);
    EXPECT_NEAR(b.u, 0.25f, 1.0e-5f);
    EXPECT_NEAR(b.v, 0.5f, 1.0e-5f);
    EXPECT_NEAR(b.du, 0.25f, 1.0e-5f);
    EXPECT_NEAR(b.dv, 0.5f, 1.0e-5f);
}

TEST(SpriteSheet, FrameIndexClamps)
{
    SpriteSheetDesc desc{};
    desc.columns = 2;
    desc.rows    = 1;
    desc.inset   = false;
    SpriteSheet sheet;
    ASSERT_TRUE(sheet.createLayout(64, 32, desc));
    const SpriteUvRect last = sheet.uvForFrame(99);
    const SpriteUvRect one  = sheet.uvForFrame(1);
    EXPECT_NEAR(last.u, one.u, 1.0e-5f);
}

TEST(SpriteSheet, ParseJsonClips)
{
    const char* json = R"({
        "texture": "textures/hero.png",
        "columns": 8,
        "rows": 3,
        "frameCount": 24,
        "clips": [
            { "name": "idle", "start": 0, "count": 4, "fps": 6, "loop": true },
            { "name": "run", "startFrame": 8, "frameCount": 8, "fps": 12 },
            { "name": "jump", "start": 16, "count": 4, "fps": 10, "loop": false }
        ]
    })";

    SpriteSheetDesc desc{};
    std::string     tex;
    std::vector<SpriteClip> clips;
    ASSERT_TRUE(parseSpriteSheetJson(json, desc, tex, clips));
    EXPECT_EQ(tex, "textures/hero.png");
    EXPECT_EQ(desc.columns, 8u);
    EXPECT_EQ(desc.rows, 3u);
    EXPECT_EQ(desc.frameCount, 24u);
    ASSERT_EQ(clips.size(), 3u);
    EXPECT_EQ(clips[0].name, "idle");
    EXPECT_EQ(clips[0].texture, "textures/hero.png");
    EXPECT_EQ(clips[0].desc.columns, 8u);
    EXPECT_EQ(clips[1].startFrame, 8u);
    EXPECT_EQ(clips[1].texture, "textures/hero.png");
    EXPECT_FALSE(clips[2].loop);
}

TEST(SpriteSheet, ParseJsonPerClipTextures)
{
    const char* json = R"({
        "inset": true,
        "clips": [
            { "name": "idle", "texture": "textures/idle.png", "columns": 8, "rows": 1, "count": 8, "fps": 6 },
            { "name": "run",  "texture": "textures/run.png",  "columns": 6, "rows": 1, "count": 6, "fps": 12 },
            { "name": "jump", "texture": "textures/jump.png", "columns": 7, "rows": 1, "count": 7, "fps": 10, "loop": false }
        ]
    })";

    SpriteSheetDesc desc{};
    std::string     tex;
    std::vector<SpriteClip> clips;
    ASSERT_TRUE(parseSpriteSheetJson(json, desc, tex, clips));
    EXPECT_TRUE(tex.empty());
    ASSERT_EQ(clips.size(), 3u);
    EXPECT_EQ(clips[0].texture, "textures/idle.png");
    EXPECT_EQ(clips[0].desc.columns, 8u);
    EXPECT_EQ(clips[0].desc.rows, 1u);
    EXPECT_EQ(clips[0].frameCount, 8u);
    EXPECT_EQ(clips[1].texture, "textures/run.png");
    EXPECT_EQ(clips[1].desc.columns, 6u);
    EXPECT_EQ(clips[1].frameCount, 6u);
    EXPECT_EQ(clips[2].texture, "textures/jump.png");
    EXPECT_EQ(clips[2].desc.columns, 7u);
    EXPECT_FALSE(clips[2].loop);
    EXPECT_TRUE(clips[2].desc.inset);
}

TEST(SpriteSheet, ParseJsonRejectsGarbage)
{
    SpriteSheetDesc desc{};
    std::string     tex;
    std::vector<SpriteClip> clips;
    EXPECT_FALSE(parseSpriteSheetJson("{", desc, tex, clips));
    EXPECT_FALSE(parseSpriteSheetJson("", desc, tex, clips));
}

TEST(SpriteAnimator, LoopsAndHolds)
{
    SpriteSheetDesc desc{};
    desc.columns = 8;
    desc.rows    = 1;
    desc.inset   = false;
    SpriteSheet sheet;
    ASSERT_TRUE(sheet.createLayout(256, 32, desc));

    SpriteAnimator anim;
    anim.setSheet(&sheet);
    ASSERT_TRUE(anim.addClip({ "run", 0, 4, 10.0f, true }));
    ASSERT_TRUE(anim.addClip({ "jump", 4, 3, 10.0f, false }));

    ASSERT_TRUE(anim.play("run"));
    anim.update(0.0f);
    EXPECT_EQ(anim.currentFrame(), 0u);

    anim.update(0.25f); // 2.5 frames at 10 fps
    EXPECT_EQ(anim.clipFrame(), 2u);
    EXPECT_TRUE(anim.isPlaying());

    anim.update(0.25f); // 5 frames total -> wraps to 1
    EXPECT_EQ(anim.clipFrame(), 1u);
    EXPECT_FALSE(anim.finished());

    ASSERT_TRUE(anim.play("jump", true));
    anim.update(0.05f);
    EXPECT_EQ(anim.currentFrame(), 4u);
    anim.update(0.50f); // well past 3 frames
    EXPECT_EQ(anim.currentFrame(), 6u);
    EXPECT_TRUE(anim.finished());
    EXPECT_FALSE(anim.isPlaying());
}

TEST(SpriteAnimator, PlaySameClipDoesNotReset)
{
    SpriteSheetDesc desc{};
    desc.columns = 4;
    desc.rows    = 1;
    desc.inset   = false;
    SpriteSheet sheet;
    ASSERT_TRUE(sheet.createLayout(128, 32, desc));

    SpriteAnimator anim;
    anim.setSheet(&sheet);
    ASSERT_TRUE(anim.addClip({ "idle", 0, 4, 8.0f, true }));
    ASSERT_TRUE(anim.play("idle"));
    anim.update(0.25f);
    const uint32_t mid = anim.currentFrame();
    EXPECT_GT(mid, 0u);
    ASSERT_TRUE(anim.play("idle", false));
    EXPECT_EQ(anim.currentFrame(), mid);
    ASSERT_TRUE(anim.play("idle", true));
    EXPECT_EQ(anim.currentFrame(), 0u);
}

TEST(SpriteAnimator, UnknownClipFails)
{
    SpriteAnimator anim;
    EXPECT_FALSE(anim.play("missing"));
    EXPECT_FALSE(anim.addClip({ "", 0, 1, 1.0f, true }));
}

TEST(SpriteAnimator, PerClipSheets)
{
    SpriteSheetDesc idleDesc{};
    idleDesc.columns = 8;
    idleDesc.rows    = 1;
    idleDesc.inset   = false;
    SpriteSheetDesc runDesc{};
    runDesc.columns = 6;
    runDesc.rows    = 1;
    runDesc.inset   = false;

    SpriteSheet idleSheet;
    SpriteSheet runSheet;
    ASSERT_TRUE(idleSheet.createLayout(256, 32, idleDesc));
    ASSERT_TRUE(runSheet.createLayout(192, 32, runDesc));

    SpriteAnimator anim;
    ASSERT_TRUE(anim.addClip({ "idle", 0, 8, 8.0f, true }, &idleSheet));
    ASSERT_TRUE(anim.addClip({ "run", 0, 6, 12.0f, true }, &runSheet));

    ASSERT_TRUE(anim.play("idle"));
    EXPECT_EQ(anim.currentSheet(), &idleSheet);
    const SpriteUvRect idleUv = anim.currentUv();
    EXPECT_NEAR(idleUv.du, 1.0f / 8.0f, 1.0e-5f);

    ASSERT_TRUE(anim.play("run", true));
    EXPECT_EQ(anim.currentSheet(), &runSheet);
    const SpriteUvRect runUv = anim.currentUv();
    EXPECT_NEAR(runUv.du, 1.0f / 6.0f, 1.0e-5f);
}
