#include <gtest/gtest.h>

#include "Render/Camera3D.h"
#include "Render/PackedSrvHeap.h"
#include "Render/ShadowCascades.h"
#include "Terrain/HeightMap.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainMaterial.h"
#include "Terrain/TerrainTileFile.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace Dark;
using namespace Dark::Math;
using namespace Dark::Terrain;

namespace
{

constexpr int   kTestTileCells = 8;
constexpr float kTestCell      = 1.0f;
constexpr float kCoarseY       = 7.0f;

float FineRaw(int wx, int wz)
{
    return 3.0f + 0.01f * static_cast<float>(wx) + 0.02f * static_cast<float>(wz);
}

std::filesystem::path MakeTempDir(const char* name)
{
    const auto dir = std::filesystem::temp_directory_path() / name;
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void RemoveTempDir(const std::filesystem::path& dir)
{
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

bool WriteFineTile(const std::filesystem::path& tileDir, int tx, int tz, const Vector3f& origin, float heightScale)
{
    HeightMap tile;
    const uint32_t samples = static_cast<uint32_t>(kTestTileCells + 1);
    if (!tile.create(samples, samples, kTestCell, heightScale))
        return false;
    const Vector3f tileOrigin{
        origin.x + static_cast<float>(tx * kTestTileCells) * kTestCell,
        origin.y,
        origin.z + static_cast<float>(tz * kTestTileCells) * kTestCell};
    tile.setOrigin(tileOrigin);
    for (int z = 0; z <= kTestTileCells; ++z)
    {
        for (int x = 0; x <= kTestTileCells; ++x)
            tile.setHeight(x, z, FineRaw(tx * kTestTileCells + x, tz * kTestTileCells + z));
    }
    return tile.saveBinary(tileHeightPath(tileDir, tx, tz));
}

bool WriteCoarse(const std::filesystem::path& path, uint32_t tilesX, uint32_t tilesZ, const Vector3f& origin, float heightScale)
{
    const float    worldExtent = static_cast<float>(tilesX * static_cast<uint32_t>(kTestTileCells)) * kTestCell;
    const uint32_t samples     = 17;
    HeightMap      coarse;
    if (!coarse.create(samples, samples, worldExtent / static_cast<float>(samples - 1u), heightScale))
        return false;
    coarse.setOrigin(origin);
    for (int z = 0; z < static_cast<int>(samples); ++z)
    {
        for (int x = 0; x < static_cast<int>(samples); ++x)
            coarse.setHeight(x, z, kCoarseY);
    }
    return coarse.saveBinary(path);
}

bool WriteTestWorld(
    const std::filesystem::path& dir,
    uint32_t tilesX,
    uint32_t tilesZ,
    const Vector3f& origin,
    bool writeFine)
{
    const auto coarsePath = dir / "coarse.height.bin";
    const auto tileDir    = dir / "tiles";
    if (!WriteCoarse(coarsePath, tilesX, tilesZ, origin, 1.0f))
        return false;
    if (!writeFine)
        return true;
    for (uint32_t tz = 0; tz < tilesZ; ++tz)
    {
        for (uint32_t tx = 0; tx < tilesX; ++tx)
        {
            if (!WriteFineTile(tileDir, static_cast<int>(tx), static_cast<int>(tz), origin, 1.0f))
                return false;
        }
    }
    return true;
}

TerrainGridDesc MakeDesc(const std::filesystem::path& dir, uint32_t tilesX, uint32_t tilesZ, const Vector3f& origin, int ring)
{
    TerrainGridDesc desc{};
    desc.tilesX       = tilesX;
    desc.tilesZ       = tilesZ;
    desc.tileCells    = static_cast<uint32_t>(kTestTileCells);
    desc.cellSize     = kTestCell;
    desc.heightScale  = 1.0f;
    desc.origin       = origin;
    desc.coarseFile   = dir / "coarse.height.bin";
    desc.tileDir      = dir / "tiles";
    desc.residentRing = ring;
    return desc;
}

Vector3f TileCenter(const Vector3f& origin, int tx, int tz)
{
    const float tileWorld = static_cast<float>(kTestTileCells) * kTestCell;
    return Vector3f(
        origin.x + (static_cast<float>(tx) + 0.5f) * tileWorld,
        20.0f,
        origin.z + (static_cast<float>(tz) + 0.5f) * tileWorld);
}

} // namespace

TEST(TerrainGrid, MissingCoarse_CreateFails)
{
    const auto dir = MakeTempDir("darkengine6_grid_missing_coarse_ut");
    TerrainGridDesc desc = MakeDesc(dir, 2, 2, Vector3f(0.0f, 0.0f, 0.0f), 5);
    TerrainGrid grid;
    EXPECT_FALSE(grid.create(desc));
    EXPECT_FALSE(grid.valid());
    float y = 123.0f;
    EXPECT_FALSE(grid.tryHeightAtWorld(1.0f, 1.0f, y));
    EXPECT_NEAR(grid.heightAtWorld(1.0f, 1.0f), 0.0f, 1.0e-5f);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, Query_UnloadedUsesCoarse)
{
    const auto      dir    = MakeTempDir("darkengine6_grid_unloaded_coarse_ut");
    const Vector3f  origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 2, 2, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 2, 2, origin, 5)));
    EXPECT_EQ(grid.residentCount(), 0);

    const float x = 3.0f;
    const float z = 5.0f;
    float       y = 0.0f;
    ASSERT_TRUE(grid.containsXZ(x, z));
    ASSERT_TRUE(grid.tryHeightAtWorld(x, z, y));
    EXPECT_NEAR(y, grid.coarse().heightAtWorld(x, z), 1.0e-4f);
    EXPECT_NEAR(y, kCoarseY, 1.0e-4f);
    EXPECT_NE(y, 0.0f);
    EXPECT_NEAR(grid.heightAtWorld(x, z), kCoarseY, 1.0e-4f);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, Query_OutsideAabb)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_outside_aabb_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 2, 2, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 2, 2, origin, 5)));

    const float worldMax = static_cast<float>(2 * kTestTileCells) * kTestCell;
    float       y        = 0.0f;
    EXPECT_FALSE(grid.containsXZ(-0.1f, 1.0f));
    EXPECT_FALSE(grid.tryHeightAtWorld(-0.1f, 1.0f, y));
    EXPECT_FALSE(grid.containsXZ(1.0f, worldMax + 0.1f));
    EXPECT_FALSE(grid.tryHeightAtWorld(1.0f, worldMax + 0.1f, y));
    EXPECT_TRUE(grid.containsXZ(0.0f, 0.0f));
    EXPECT_TRUE(grid.containsXZ(worldMax, worldMax));
    EXPECT_TRUE(grid.tryHeightAtWorld(worldMax, worldMax, y));
    RemoveTempDir(dir);
}

TEST(TerrainGrid, SharedEdge_Equal)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_shared_edge_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 2, 2, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 2, 2, origin, 5)));
    grid.updateStreaming(TileCenter(origin, 0, 0), nullptr);

    ASSERT_TRUE(grid.isResident(0, 0));
    ASSERT_TRUE(grid.isResident(1, 0));
    const HeightMap* a = grid.residentHeight(0, 0);
    const HeightMap* b = grid.residentHeight(1, 0);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    for (int z = 0; z <= kTestTileCells; ++z)
        EXPECT_FLOAT_EQ(a->height(kTestTileCells, z), b->height(0, z));

    const float edgeX = origin.x + static_cast<float>(kTestTileCells) * kTestCell;
    EXPECT_NEAR(grid.heightAtWorld(edgeX, 2.0f), FineRaw(kTestTileCells, 2), 1.0e-4f);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, ResidentBounds_NotWorld)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_resident_bounds_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 8, 8, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 8, 8, origin, 3)));
    grid.updateStreaming(TileCenter(origin, 4, 4), nullptr);

    EXPECT_EQ(grid.residentCount(), 9);
    const AABox3f world    = grid.bounds();
    const AABox3f resident = grid.residentBounds();
    ASSERT_TRUE(world.IsValid());
    ASSERT_TRUE(resident.IsValid());
    EXPECT_LT(resident.Size().x, world.Size().x * 0.5f);
    EXPECT_LT(resident.Size().z, world.Size().z * 0.5f);
    EXPECT_GT(world.Size().x, resident.Size().x + 1.0f);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, ShadowBounds_CameraCentered)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_shadow_bounds_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 8, 8, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 8, 8, origin, 3)));

    Camera3D camera;
    camera.SetPosition(TileCenter(origin, 4, 4));
    grid.updateStreaming(camera.GetPosition(), nullptr);

    const AABox3f shadow   = grid.shadowBounds(camera);
    const AABox3f resident = grid.residentBounds();
    ASSERT_TRUE(shadow.IsValid());
    ASSERT_TRUE(resident.IsValid());
    EXPECT_NE(shadow.Size().x, resident.Size().x);

    const ShadowSettings settings{};
    const float          maxHalf = settings.maxDistance + settings.casterMargin;
    EXPECT_LE(shadow.Extents().x, maxHalf + 1.0e-3f);
    EXPECT_LE(shadow.Extents().z, maxHalf + 1.0e-3f);
    EXPECT_NEAR(shadow.Center().x, camera.GetPosition().x, 1.0e-3f);
    EXPECT_NEAR(shadow.Center().z, camera.GetPosition().z, 1.0e-3f);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, StreamRing_LoadsAndEvicts)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_stream_ring_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 8, 8, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 8, 8, origin, kResidentRingDefault)));

    grid.updateStreaming(TileCenter(origin, 4, 4), nullptr);
    EXPECT_EQ(grid.residentCount(), 25);
    EXPECT_TRUE(grid.isResident(4, 4));
    EXPECT_TRUE(grid.isResident(2, 2));
    EXPECT_TRUE(grid.isResident(6, 6));
    EXPECT_FALSE(grid.isResident(0, 0));
    EXPECT_NEAR(grid.heightAtWorld(TileCenter(origin, 4, 4).x, TileCenter(origin, 4, 4).z), FineRaw(4 * kTestTileCells + kTestTileCells / 2, 4 * kTestTileCells + kTestTileCells / 2), 1.5e-3f);

    grid.updateStreaming(TileCenter(origin, 0, 0), nullptr);
    EXPECT_EQ(grid.residentCount(), 9);
    EXPECT_TRUE(grid.isResident(0, 0));
    EXPECT_TRUE(grid.isResident(2, 2));
    EXPECT_FALSE(grid.isResident(4, 4));
    EXPECT_FALSE(grid.isResident(6, 6));
    RemoveTempDir(dir);
}

TEST(TerrainGrid, MissingFine_UsesCoarse)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_missing_fine_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 2, 2, origin, true));
    std::error_code ec;
    std::filesystem::remove(tileHeightPath(dir / "tiles", 1, 1), ec);

    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 2, 2, origin, 5)));
    grid.updateStreaming(TileCenter(origin, 1, 1), nullptr);
    EXPECT_TRUE(grid.isResident(0, 0) || grid.isResident(1, 0) || grid.isResident(0, 1));
    EXPECT_FALSE(grid.isResident(1, 1));

    const Vector3f p = TileCenter(origin, 1, 1);
    float          y = 0.0f;
    ASSERT_TRUE(grid.tryHeightAtWorld(p.x, p.z, y));
    EXPECT_NEAR(y, kCoarseY, 1.0e-4f);
    EXPECT_NE(y, 0.0f);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, DeltaLod_AtMostOne)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_delta_lod_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 2, 2, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 2, 2, origin, 5)));

    const float dist[] = { 2.0f, 4.0f, 8.0f, 16.0f };
    grid.setLodDistances(dist, 4);
    grid.updateStreaming(TileCenter(origin, 0, 0), nullptr);

    EXPECT_EQ(grid.residentCount(), 4);
    const TerrainWorld* w00 = grid.residentWorld(0, 0);
    ASSERT_NE(w00, nullptr);
    EXPECT_FALSE(w00->uploadsHeightTexture());
    EXPECT_FALSE(w00->heightTexture().valid());

    int maxDelta = 0;
    for (int tz = 0; tz < 2; ++tz)
    {
        for (int tx = 0; tx < 2; ++tx)
        {
            const TerrainWorld* a = grid.residentWorld(tx, tz);
            ASSERT_NE(a, nullptr);
            ASSERT_NE(a->chunk(0, 0), nullptr);
            const int lod = a->chunk(0, 0)->lod;
            if (tx + 1 < 2)
            {
                const TerrainWorld* b = grid.residentWorld(tx + 1, tz);
                ASSERT_NE(b, nullptr);
                maxDelta = std::max(maxDelta, std::abs(lod - b->chunk(0, 0)->lod));
            }
            if (tz + 1 < 2)
            {
                const TerrainWorld* b = grid.residentWorld(tx, tz + 1);
                ASSERT_NE(b, nullptr);
                maxDelta = std::max(maxDelta, std::abs(lod - b->chunk(0, 0)->lod));
            }
        }
    }
    EXPECT_LE(maxDelta, 1);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, UnloadedEdge_NoWeld)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_unloaded_weld_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 2, 2, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 2, 2, origin, 1)));
    grid.updateStreaming(TileCenter(origin, 0, 0), nullptr);

    EXPECT_TRUE(grid.isResident(0, 0));
    EXPECT_FALSE(grid.isResident(1, 0));
    EXPECT_FALSE(grid.isResident(0, 1));
    const TerrainWorld* w = grid.residentWorld(0, 0);
    ASSERT_NE(w, nullptr);
    ASSERT_NE(w->chunk(0, 0), nullptr);
    EXPECT_FALSE(w->chunk(0, 0)->edges.east());
    EXPECT_FALSE(w->chunk(0, 0)->edges.north());
    RemoveTempDir(dir);
}

TEST(TerrainGrid, TileHeap_SplatSlot12)
{
    const auto     dir    = MakeTempDir("darkengine6_grid_tile_heap_ut");
    const Vector3f origin{ 0.0f, 0.0f, 0.0f };
    ASSERT_TRUE(WriteTestWorld(dir, 2, 2, origin, true));
    TerrainGrid grid;
    ASSERT_TRUE(grid.create(MakeDesc(dir, 2, 2, origin, 5)));
    grid.updateStreaming(TileCenter(origin, 0, 0), nullptr);
    ASSERT_TRUE(grid.isResident(0, 0));
    ASSERT_TRUE(grid.isResident(1, 0));

    D3D12_CPU_DESCRIPTOR_HANDLE splat0{};
    D3D12_CPU_DESCRIPTOR_HANDLE splat1{};
    D3D12_CPU_DESCRIPTOR_HANDLE splat1b{};
    splat0.ptr  = 0x1000;
    splat1.ptr  = 0x2000;
    splat1b.ptr = 0x3000;

    ASSERT_TRUE(grid.packTileSplat(0, 0, splat0));
    ASSERT_TRUE(grid.packTileSplat(1, 0, splat1));

    const PackedSrvHeap* h0 = grid.residentPackedHeap(0, 0);
    const PackedSrvHeap* h1 = grid.residentPackedHeap(1, 0);
    ASSERT_NE(h0, nullptr);
    ASSERT_NE(h1, nullptr);
    ASSERT_NE(h0, h1);

    EXPECT_EQ(h0->splatSlot, TerrainMaterial::kSplatSlot);
    EXPECT_EQ(h0->shadowSlot, TerrainMaterial::kShadowSlot);
    EXPECT_EQ(h0->srvCount, TerrainMaterial::kSrvCount);
    EXPECT_EQ(h1->splatSlot, 12u);
    EXPECT_EQ(h1->shadowSlot, 13u);
    EXPECT_EQ(h1->srvCount, 14u);
    EXPECT_EQ(h0->splatCpu.ptr, splat0.ptr);
    EXPECT_EQ(h1->splatCpu.ptr, splat1.ptr);

    ASSERT_TRUE(grid.packTileSplat(1, 0, splat1b));
    const PackedSrvHeap* h0After = grid.residentPackedHeap(0, 0);
    const PackedSrvHeap* h1After = grid.residentPackedHeap(1, 0);
    ASSERT_NE(h0After, nullptr);
    ASSERT_NE(h1After, nullptr);
    EXPECT_EQ(h0After->splatCpu.ptr, splat0.ptr);
    EXPECT_EQ(h1After->splatCpu.ptr, splat1b.ptr);
    EXPECT_NE(h0After->splatCpu.ptr, h1After->splatCpu.ptr);
    RemoveTempDir(dir);
}

TEST(TerrainGrid, CreateFromHeightMap_Queries)
{
    HeightMap hm;
    ASSERT_TRUE(hm.create(17, 17, 1.0f, 1.0f));
    hm.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
    for (int z = 0; z < 17; ++z)
    {
        for (int x = 0; x < 17; ++x)
            hm.setHeight(x, z, 4.0f);
    }
    TerrainGrid grid;
    ASSERT_TRUE(grid.createFromHeightMap(std::move(hm), 16));
    EXPECT_TRUE(grid.valid());
    EXPECT_EQ(grid.tilesX(), 1u);
    EXPECT_EQ(grid.tileCells(), 16);
    EXPECT_EQ(grid.chunkCells(), 16);
    float y = 0.0f;
    ASSERT_TRUE(grid.tryHeightAtWorld(3.0f, 5.0f, y));
    EXPECT_NEAR(y, 4.0f, 1.0e-4f);
    EXPECT_FALSE(grid.tryHeightAtWorld(-0.1f, 1.0f, y));
}

TEST(TerrainGrid, WorkingSlice_SharedEdge)
{
    HeightMap working;
    ASSERT_TRUE(working.create(17, 17, 1.0f, 1.0f));
    working.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
    for (int z = 0; z <= 16; ++z)
    {
        for (int x = 0; x <= 16; ++x)
            working.setHeight(x, z, FineRaw(x, z));
    }
    HeightMap coarse = working;
    TerrainGridDesc desc{};
    desc.tilesX       = 2;
    desc.tilesZ       = 2;
    desc.tileCells    = 8;
    desc.cellSize     = 1.0f;
    desc.origin       = Vector3f{ 0.0f, 0.0f, 0.0f };
    desc.residentRing = 5;
    TerrainGrid grid;
    ASSERT_TRUE(grid.createFromCoarse(desc, std::move(coarse)));
    ASSERT_TRUE(grid.setWorking(std::move(working), SplatMap{}));
    grid.updateStreaming(TileCenter(Vector3f{ 0.0f, 0.0f, 0.0f }, 0, 0), nullptr);
    EXPECT_EQ(grid.chunkCells(), kTestTileCells);
    const TerrainWorld* w00 = grid.residentWorld(0, 0);
    ASSERT_NE(w00, nullptr);
    EXPECT_EQ(w00->chunksX(), 1);
    EXPECT_EQ(w00->chunksZ(), 1);

    const HeightMap* a = grid.residentHeight(0, 0);
    const HeightMap* b = grid.residentHeight(1, 0);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    for (int z = 0; z <= 8; ++z)
        EXPECT_NEAR(a->height(8, z), b->height(0, z), 1.0e-5f);
}

TEST(TerrainGrid, PinKeepsTileOutsideCameraRing)
{
    HeightMap working;
    ASSERT_TRUE(working.create(25, 25, 1.0f, 1.0f));
    working.setOrigin(Vector3f{ 0.0f, 0.0f, 0.0f });
    HeightMap coarse = working;
    TerrainGridDesc desc{};
    desc.tilesX       = 3;
    desc.tilesZ       = 3;
    desc.tileCells    = 8;
    desc.cellSize     = 1.0f;
    desc.origin       = Vector3f{ 0.0f, 0.0f, 0.0f };
    desc.residentRing = 1;
    TerrainGrid grid;
    ASSERT_TRUE(grid.createFromCoarse(desc, std::move(coarse)));
    ASSERT_TRUE(grid.setWorking(std::move(working), SplatMap{}));
    grid.pinWorldXZ(4.0f, 4.0f, 3);
    grid.updateStreaming(TileCenter(Vector3f{ 0.0f, 0.0f, 0.0f }, 2, 2), nullptr);
    EXPECT_TRUE(grid.isResident(0, 0));
    EXPECT_TRUE(grid.isResident(2, 2));
}
