#include <gtest/gtest.h>

#include "Render/RenderTargetPool.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// RenderTargetPool (render-graph plan Phase 5): descriptor resolution and hashing, registration,
// resize-after-drain (only changed extents, transactional) and lifetime, against a fake backend.

using namespace Dark::Rg;

namespace
{
    constexpr uint32_t kRgba16f = 10;
    constexpr uint32_t kR8      = 61;

    struct FakeStorage : TargetStorage
    {
        TargetExtent extent;
        std::string  name;
        int*         live = nullptr;
        FakeStorage(TargetExtent e, const char* n, int* liveCount) :
            extent(e),
            name(n),
            live(liveCount)
        {
            ++*live;
        }
        ~FakeStorage() override
        {
            --*live;
        }
    };

    class FakeBackend : public TargetBackend
    {
    public:
        std::unique_ptr<TargetStorage> create(const TargetDesc& desc, TargetExtent extent, const char* name) override
        {
            ++creates;
            if (failAt != 0 && creates == failAt)
                return nullptr;
            auto storage    = std::make_unique<FakeStorage>(extent, name, &live);
            storage->native = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(0x1000 + creates * 0x10));
            storage->srv    = desc.srv ? 0x100u + static_cast<uintptr_t>(creates) : 0;
            storage->rtv    = desc.rtv ? 0x200u + static_cast<uintptr_t>(creates) : 0;
            storage->dsv    = desc.dsv ? 0x300u + static_cast<uintptr_t>(creates) : 0;
            return storage;
        }
        void retire(std::unique_ptr<TargetStorage> storage) override
        {
            ++retires;
            retired.push_back(std::move(storage)); // kept alive until the "fence" (clearRetired)
        }
        void clearRetired()
        {
            retired.clear();
        }

        int                                         creates = 0;
        int                                         retires = 0;
        int                                         failAt  = 0; // 1-based create() call that fails; 0 = never
        int                                         live    = 0; // storages not yet destroyed
        std::vector<std::unique_ptr<TargetStorage>> retired;
    };

    TargetDesc color(TargetSize size = TargetSize::OutputFloor, uint32_t divisor = 1, uint32_t format = kRgba16f)
    {
        TargetDesc d;
        d.size    = size;
        d.divisor = divisor;
        d.format  = format;
        d.rtv     = true;
        return d;
    }

    TargetDesc absolute(uint32_t w, uint32_t h)
    {
        TargetDesc d = color(TargetSize::Absolute);
        d.width      = w;
        d.height     = h;
        return d;
    }

    TargetExtent extentOf(const TargetDesc& desc, uint32_t w, uint32_t h)
    {
        TargetExtent out;
        EXPECT_TRUE(resolveTargetExtent(desc, w, h, out));
        return out;
    }
} // namespace

// ---------------------------------------------------------------- resolve / hash

TEST(RenderTargetPool, ResolvesFloorCeilAndAbsoluteExtents)
{
    EXPECT_EQ(extentOf(color(), 1280, 720), (TargetExtent{ 1280, 720 }));
    EXPECT_EQ(extentOf(color(TargetSize::OutputFloor, 2), 1281, 721), (TargetExtent{ 640, 360 }));
    EXPECT_EQ(extentOf(color(TargetSize::OutputCeil, 2), 1281, 721), (TargetExtent{ 641, 361 }));
    EXPECT_EQ(extentOf(color(TargetSize::OutputCeil, 2), 1280, 720), (TargetExtent{ 640, 360 }));
    // Each dimension is at least 1 (the bloom / meter chains go to 1x1).
    EXPECT_EQ(extentOf(color(TargetSize::OutputFloor, 64), 40, 30), (TargetExtent{ 1, 1 }));
    EXPECT_EQ(extentOf(absolute(64, 32), 1280, 720), (TargetExtent{ 64, 32 }));
    // Absolute ignores the output (it resolves even while minimized).
    EXPECT_EQ(extentOf(absolute(64, 32), 0, 0), (TargetExtent{ 64, 32 }));
    // Ceil division does not overflow at the top of the range.
    const uint32_t big = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(extentOf(color(TargetSize::OutputCeil, 2), big, big), (TargetExtent{ big / 2 + 1, big / 2 + 1 }));
}

TEST(RenderTargetPool, RejectsInvalidDescsAndMinimizedOutput)
{
    TargetExtent out{ 7, 7 };
    EXPECT_FALSE(resolveTargetExtent(color(), 0, 720, out));
    EXPECT_EQ(out, (TargetExtent{}));
    EXPECT_FALSE(resolveTargetExtent(color(), 1280, 0, out));
    EXPECT_FALSE(resolveTargetExtent(color(TargetSize::OutputFloor, 0), 1280, 720, out));
    EXPECT_FALSE(resolveTargetExtent(absolute(0, 32), 1280, 720, out));

    TargetDesc noMips = color();
    noMips.mips       = 0;
    EXPECT_FALSE(resolveTargetExtent(noMips, 1280, 720, out));
    TargetDesc noFormat = color();
    noFormat.format     = 0;
    EXPECT_FALSE(resolveTargetExtent(noFormat, 1280, 720, out));
    TargetDesc rtvAndDsv = color();
    rtvAndDsv.dsv        = true;
    EXPECT_FALSE(resolveTargetExtent(rtvAndDsv, 1280, 720, out));
    TargetDesc nanClear = color();
    nanClear.clear      = true;
    nanClear.color[2]   = std::nanf("");
    EXPECT_FALSE(resolveTargetExtent(nanClear, 1280, 720, out));
    TargetDesc infDepth = color();
    infDepth.depth      = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(resolveTargetExtent(infDepth, 1280, 720, out));
}

TEST(RenderTargetPool, HashFollowsEveryFieldAndEqualDescsHashEqually)
{
    const TargetDesc base = color();
    EXPECT_EQ(hashTargetDesc(base), hashTargetDesc(color()));

    std::vector<TargetDesc> variants;
    TargetDesc              d = base;
    d.size                    = TargetSize::OutputCeil;
    variants.push_back(d);
    d         = base;
    d.divisor = 2;
    variants.push_back(d);
    d        = base;
    d.format = kR8;
    variants.push_back(d);
    d           = base;
    d.srvFormat = kR8;
    variants.push_back(d);
    d              = base;
    d.targetFormat = kR8;
    variants.push_back(d);
    d       = base;
    d.flags = 4;
    variants.push_back(d);
    d              = base;
    d.initialState = 0x80;
    variants.push_back(d);
    d     = base;
    d.srv = false;
    variants.push_back(d);
    d     = base;
    d.rtv = false;
    variants.push_back(d);
    d       = base;
    d.clear = true;
    variants.push_back(d);
    d          = base;
    d.color[3] = 1.0f;
    variants.push_back(d);
    d       = base;
    d.depth = 1.0f;
    variants.push_back(d);
    d      = base;
    d.mips = 2;
    variants.push_back(d);
    d           = base;
    d.arraySize = 2;
    variants.push_back(d);
    for (size_t i = 0; i < variants.size(); ++i)
    {
        EXPECT_NE(hashTargetDesc(variants[i]), hashTargetDesc(base)) << i;
        EXPECT_FALSE(variants[i] == base) << i;
    }

    // +0 and -0 compare equal, so they must hash equally.
    TargetDesc plus  = base;
    TargetDesc minus = base;
    plus.color[0]    = 0.0f;
    minus.color[0]   = -0.0f;
    EXPECT_TRUE(plus == minus);
    EXPECT_EQ(hashTargetDesc(plus), hashTargetDesc(minus));
}

// ---------------------------------------------------------------- registration

TEST(RenderTargetPool, RegisterIsIdempotentAndRejectsConflicts)
{
    FakeBackend      backend;
    RenderTargetPool pool(backend);
    ASSERT_TRUE(pool.resizeAfterDrain(1280, 720));
    EXPECT_EQ(backend.creates, 0); // no targets yet: only the output size

    const TargetId hdr = pool.registerTexture("DE.HdrColor", color());
    ASSERT_TRUE(hdr.valid());
    EXPECT_EQ(pool.count(), 1u);
    EXPECT_EQ(backend.creates, 1);
    const TargetInfo info = pool.get(hdr);
    ASSERT_NE(info.storage, nullptr);
    EXPECT_EQ(info.extent, (TargetExtent{ 1280, 720 }));
    EXPECT_EQ(info.generation, 1u);
    EXPECT_NE(info.storage->rtv, 0u);
    EXPECT_NE(info.storage->srv, 0u);
    EXPECT_EQ(static_cast<const FakeStorage*>(info.storage)->name, "DE.HdrColor"); // the PIX name reaches the backend

    // Same name + desc: the existing target, nothing created.
    EXPECT_EQ(pool.registerTexture("DE.HdrColor", color()), hdr);
    EXPECT_EQ(backend.creates, 1);
    EXPECT_EQ(pool.find("DE.HdrColor"), hdr);

    // Same name, different desc: rejected, the original stays.
    EXPECT_FALSE(pool.registerTexture("DE.HdrColor", color(TargetSize::OutputFloor, 2)).valid());
    EXPECT_EQ(pool.get(hdr).extent, (TargetExtent{ 1280, 720 }));
    EXPECT_EQ(pool.count(), 1u);

    EXPECT_FALSE(pool.registerTexture("", color()).valid());
    EXPECT_FALSE(pool.registerTexture(nullptr, color()).valid());
    EXPECT_FALSE(pool.find("DE.Missing").valid());
    EXPECT_FALSE(pool.find(nullptr).valid());
}

TEST(RenderTargetPool, RegistrationFailsWhileMinimizedOrOnBackendFailure)
{
    FakeBackend      backend;
    RenderTargetPool pool(backend);
    EXPECT_FALSE(pool.registerTexture("DE.Rel", color()).valid()); // no output size yet
    EXPECT_TRUE(pool.registerTexture("DE.Abs", absolute(16, 16)).valid());

    ASSERT_TRUE(pool.resizeAfterDrain(320, 200));
    backend.failAt = backend.creates + 1;
    EXPECT_FALSE(pool.registerTexture("DE.Rel", color()).valid());
    EXPECT_EQ(pool.count(), 1u);
    backend.failAt = 0;
    EXPECT_TRUE(pool.registerTexture("DE.Rel", color()).valid());
    EXPECT_EQ(pool.count(), 2u);
}

// ---------------------------------------------------------------- resize

TEST(RenderTargetPool, ResizeRecreatesOnlyChangedExtents)
{
    FakeBackend      backend;
    RenderTargetPool pool(backend);
    ASSERT_TRUE(pool.resizeAfterDrain(1280, 720));
    const TargetId fullId  = pool.registerTexture("DE.Full", color());
    const TargetId floorId = pool.registerTexture("DE.HalfFloor", color(TargetSize::OutputFloor, 2));
    const TargetId ceilId  = pool.registerTexture("DE.HalfCeil", color(TargetSize::OutputCeil, 2));
    const TargetId absId   = pool.registerTexture("DE.Abs", absolute(64, 64));
    ASSERT_EQ(backend.creates, 4);
    const TargetStorage* floorStorage = pool.get(floorId).storage;
    const TargetStorage* absStorage   = pool.get(absId).storage;

    // Same size: nothing happens.
    ASSERT_TRUE(pool.resizeAfterDrain(1280, 720));
    EXPECT_EQ(backend.creates, 4);

    // +1 pixel: full and ceil change; floor (640x360) and the absolute target do not.
    ASSERT_TRUE(pool.resizeAfterDrain(1281, 721));
    EXPECT_EQ(backend.creates, 6);
    EXPECT_EQ(backend.retires, 2);
    EXPECT_EQ(pool.get(fullId).generation, 2u);
    EXPECT_EQ(pool.get(fullId).extent, (TargetExtent{ 1281, 721 }));
    EXPECT_EQ(pool.get(ceilId).generation, 2u);
    EXPECT_EQ(pool.get(ceilId).extent, (TargetExtent{ 641, 361 }));
    EXPECT_EQ(pool.get(floorId).generation, 1u);
    EXPECT_EQ(pool.get(floorId).storage, floorStorage);
    EXPECT_EQ(pool.get(absId).generation, 1u);
    EXPECT_EQ(pool.get(absId).storage, absStorage);
    // IDs survive a resize.
    EXPECT_EQ(pool.find("DE.Full"), fullId);
    EXPECT_EQ(pool.outputWidth(), 1281u);
    EXPECT_EQ(pool.outputHeight(), 721u);
}

TEST(RenderTargetPool, MinimizedResizeRetainsEverything)
{
    FakeBackend      backend;
    RenderTargetPool pool(backend);
    ASSERT_TRUE(pool.resizeAfterDrain(800, 600));
    const TargetId id = pool.registerTexture("DE.Full", color());
    ASSERT_TRUE(pool.resizeAfterDrain(0, 0));
    ASSERT_TRUE(pool.resizeAfterDrain(0, 600));
    EXPECT_EQ(backend.creates, 1);
    EXPECT_EQ(pool.get(id).extent, (TargetExtent{ 800, 600 }));
    EXPECT_EQ(pool.outputWidth(), 800u);
    // Restoring the old size is a no-op.
    ASSERT_TRUE(pool.resizeAfterDrain(800, 600));
    EXPECT_EQ(backend.creates, 1);
}

TEST(RenderTargetPool, FailedResizeKeepsAllOldTargetsAndCanBeRetried)
{
    FakeBackend      backend;
    RenderTargetPool pool(backend);
    ASSERT_TRUE(pool.resizeAfterDrain(1280, 720));
    const TargetId       a          = pool.registerTexture("DE.A", color());
    const TargetId       b          = pool.registerTexture("DE.B", color(TargetSize::OutputFloor, 2));
    const TargetId       c          = pool.registerTexture("DE.C", color(TargetSize::OutputFloor, 4));
    const TargetStorage* sa         = pool.get(a).storage;
    const TargetStorage* sb         = pool.get(b).storage;
    const TargetStorage* sc         = pool.get(c).storage;
    const int            liveBefore = backend.live;

    backend.failAt = backend.creates + 2; // the second replacement fails
    EXPECT_FALSE(pool.resizeAfterDrain(1920, 1080));
    EXPECT_EQ(pool.get(a).storage, sa);
    EXPECT_EQ(pool.get(b).storage, sb);
    EXPECT_EQ(pool.get(c).storage, sc);
    EXPECT_EQ(pool.get(a).generation, 1u);
    EXPECT_EQ(pool.get(a).extent, (TargetExtent{ 1280, 720 }));
    EXPECT_EQ(pool.outputWidth(), 1280u);
    EXPECT_EQ(pool.outputHeight(), 720u);
    EXPECT_EQ(backend.retires, 0);       // nothing old was retired
    EXPECT_EQ(backend.live, liveBefore); // the one temporary replacement was freed at once

    backend.failAt = 0;
    ASSERT_TRUE(pool.resizeAfterDrain(1920, 1080));
    EXPECT_EQ(pool.get(a).extent, (TargetExtent{ 1920, 1080 }));
    EXPECT_EQ(pool.get(b).extent, (TargetExtent{ 960, 540 }));
    EXPECT_EQ(pool.get(c).extent, (TargetExtent{ 480, 270 }));
    EXPECT_EQ(pool.get(a).generation, 2u);
    EXPECT_EQ(backend.retires, 3);
}

// ---------------------------------------------------------------- lifetime

TEST(RenderTargetPool, UnregisterRetiresAndStaleIdsNeverSeeAReusedSlot)
{
    FakeBackend      backend;
    RenderTargetPool pool(backend);
    ASSERT_TRUE(pool.resizeAfterDrain(640, 480));
    const TargetId old = pool.registerTexture("DE.Old", color());
    ASSERT_TRUE(pool.unregisterTexture(old));
    EXPECT_EQ(backend.retires, 1);
    EXPECT_EQ(pool.count(), 0u);
    EXPECT_FALSE(pool.unregisterTexture(old)); // twice is a no-op
    EXPECT_EQ(backend.retires, 1);

    // The retired storage stays alive until the backend's fence says otherwise.
    EXPECT_EQ(backend.live, 1);
    backend.clearRetired();
    EXPECT_EQ(backend.live, 0);

    const TargetId reused = pool.registerTexture("DE.New", color());
    ASSERT_TRUE(reused.valid());
    EXPECT_EQ(reused.index, old.index); // the slot is reused ...
    EXPECT_NE(reused.serial, old.serial);
    EXPECT_EQ(pool.get(old).storage, nullptr); // ... but the stale ID cannot see it
    EXPECT_FALSE(pool.unregisterTexture(old));
    EXPECT_NE(pool.get(reused).storage, nullptr);
    EXPECT_FALSE(pool.get(TargetId{}).storage);
    EXPECT_FALSE(pool.get(TargetId{ 99, 1 }).storage);
}

TEST(RenderTargetPool, ClearAndDestructionRetireEverythingAndOldIdsStayStale)
{
    FakeBackend backend;
    {
        RenderTargetPool pool(backend);
        ASSERT_TRUE(pool.resizeAfterDrain(640, 480));
        const TargetId a = pool.registerTexture("DE.A", color());
        pool.registerTexture("DE.B", color(TargetSize::OutputFloor, 2));
        pool.clear();
        EXPECT_EQ(pool.count(), 0u);
        EXPECT_EQ(backend.retires, 2);
        const TargetId again = pool.registerTexture("DE.A", color());
        ASSERT_TRUE(again.valid());
        EXPECT_NE(again, a); // serials are not reset by clear()
        EXPECT_EQ(pool.get(a).storage, nullptr);
        pool.registerTexture("DE.C", color());
    }
    EXPECT_EQ(backend.retires, 4); // the destructor retired the two left
    backend.clearRetired();
    EXPECT_EQ(backend.live, 0);
}
