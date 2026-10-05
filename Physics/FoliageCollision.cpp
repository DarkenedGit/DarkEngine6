#include "Physics/FoliageCollision.h"

#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"
#include "Physics/PhysicsWorld.h"
#include "Terrain/TerrainGrid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Dark::Physics
{
    namespace
    {
        constexpr float kRadiusSq  = kFoliageBodyRadiusM * kFoliageBodyRadiusM;
        constexpr float kRebuildSq = kFoliageBodyRebuildM * kFoliageBodyRebuildM;

        struct FoliageCand
        {
            float    distSq = 0.0f;
            uint32_t index  = 0;
        };

        bool nearerCand(const FoliageCand& a, const FoliageCand& b)
        {
            if (a.distSq < b.distSq)
                return true;
            if (b.distSq < a.distSq)
                return false;
            return a.index < b.index;
        }

        uint64_t mixStamp(uint64_t hash, uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
            return hash;
        }

        Math::Vector3f normalFromPitch(float pitch, float tiltYaw)
        {
            const float sp = sinf(pitch);
            const float cp = cosf(pitch);
            Math::Vector3f n(sp * sinf(tiltYaw), cp, sp * cosf(tiltYaw));
            if (n.MagnitudeSqrd() <= 1.0e-8f)
                return Math::Vector3f::Y_AXIS;
            n.Normalize();
            return n;
        }

        // Yaw while upright, then tilt, so model +Y lands on the ground normal.
        Math::Quaternion rockRotation(float pitch, float tiltYaw, float yaw)
        {
            const Math::Vector3f n = normalFromPitch(pitch, tiltYaw);
            const Math::Vector3f up(Math::Vector3f::Y_AXIS);
            const Math::Quaternion spin = Math::Quaternion::FromAxisAngle(up, yaw);
            const Math::Vector3f axis = up.Cross(n);
            Math::Quaternion align = Math::Quaternion::IDENTITY;
            if (axis.MagnitudeSqrd() <= 1.0e-8f)
            {
                if (n.y < 0.0f)
                    align = Math::Quaternion::FromAxisAngle(Math::Vector3f::X_AXIS, Math::Pi);
            }
            else
            {
                Math::Vector3f a = axis;
                a.Normalize();
                align = Math::Quaternion::FromAxisAngle(a, acosf(Math::Clamp(n.y, -1.0f, 1.0f)));
            }
            return align * spin;
        }

        bool fillFoliageBox(const Terrain::FoliageRecord& rec, PhysicsBoxDesc& desc)
        {
            if (!(rec.scale > 0.0f))
                return false;

            const float scale = rec.scale;
            desc             = {};
            desc.dynamic     = false;
            desc.scale       = Math::Vector3f(1.0f, 1.0f, 1.0f);

            if (rec.kind == static_cast<uint8_t>(Terrain::FoliageKind::Tree))
            {
                // Birch LOD0 is about 12 m tall. This box is the trunk, not the crown.
                desc.halfExtents = Math::Vector3f(0.40f * scale, 6.0f * scale, 0.40f * scale);
                desc.position    = Math::Vector3f(rec.x, rec.y + 6.0f * scale, rec.z);
                desc.rotation    = Math::Quaternion::FromAxisAngle(Math::Vector3f(Math::Vector3f::Y_AXIS), rec.yaw);
                return true;
            }
            if (rec.kind != static_cast<uint8_t>(Terrain::FoliageKind::Rock))
                return false;

            // Moss boulder seated on Y=0: about 1.82 m x 1.21 m x 3.00 m. Long axis is model Z.
            const Math::Vector3f n = normalFromPitch(rec.pitch, rec.tiltYaw);
            const float          halfY = 0.60f * scale;
            desc.halfExtents           = Math::Vector3f(0.91f * scale, halfY, 1.50f * scale);
            desc.position              = Math::Vector3f(rec.x, rec.y, rec.z) + (n * halfY);
            desc.rotation          = rockRotation(rec.pitch, rec.tiltYaw, rec.yaw);
            return true;
        }

        bool insideRadius(float x, float z, float playerX, float playerZ)
        {
            const float dx = x - playerX;
            const float dz = z - playerZ;
            return (dx * dx + dz * dz) <= kRadiusSq;
        }

        // worldToTile is private. Same floor-and-clamp so an edge record stays on the border tile.
        bool editorRecordResident(const Terrain::TerrainGrid& grid, float x, float z)
        {
            if (!grid.valid())
                return false;
            const float tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
            if (!(tileWorld > 0.0f))
                return false;
            const int maxX = static_cast<int>(grid.tilesX()) - 1;
            const int maxZ = static_cast<int>(grid.tilesZ()) - 1;
            if (maxX < 0 || maxZ < 0)
                return false;

            const Math::Vector3f& origin = grid.origin();
            int tx = static_cast<int>(floorf((x - origin.x) / tileWorld));
            int tz = static_cast<int>(floorf((z - origin.z) / tileWorld));
            if (tx < 0)
                tx = 0;
            if (tz < 0)
                tz = 0;
            if (tx > maxX)
                tx = maxX;
            if (tz > maxZ)
                tz = maxZ;
            return grid.isResident(tx, tz);
        }

        bool tileHitsDisk(const Terrain::TerrainGrid& grid, int tx, int tz, float playerX, float playerZ)
        {
            const float tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
            if (!(tileWorld > 0.0f))
                return true;
            const Math::Vector3f& origin = grid.origin();
            const float minX = origin.x + static_cast<float>(tx) * tileWorld;
            const float maxX = minX + tileWorld;
            const float minZ = origin.z + static_cast<float>(tz) * tileWorld;
            const float maxZ = minZ + tileWorld;
            const float cx = playerX < minX ? minX : (playerX > maxX ? maxX : playerX);
            const float cz = playerZ < minZ ? minZ : (playerZ > maxZ ? maxZ : playerZ);
            const float dx = playerX - cx;
            const float dz = playerZ - cz;
            return (dx * dx + dz * dz) <= kRadiusSq;
        }

        uint64_t editorStamp(const Terrain::TerrainGrid& grid, const std::vector<Terrain::FoliageRecord>& foliage)
        {
            uint64_t hash = mixStamp(14695981039346656037ull, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(foliage.data())));
            hash          = mixStamp(hash, static_cast<uint64_t>(foliage.size()));
            if (!grid.valid())
                return hash;
            const int tilesX = static_cast<int>(grid.tilesX());
            const int tilesZ = static_cast<int>(grid.tilesZ());
            for (int tz = 0; tz < tilesZ; ++tz)
            {
                for (int tx = 0; tx < tilesX; ++tx)
                {
                    if (!grid.isResident(tx, tz))
                        continue;
                    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(tz)) << 32) | static_cast<uint32_t>(tx);
                    hash               = mixStamp(hash, key);
                }
            }
            return hash;
        }

        uint64_t sandboxStamp(const Terrain::TerrainGrid& grid)
        {
            uint64_t hash = 14695981039346656037ull;
            if (!grid.valid())
                return hash;
            const int tilesX = static_cast<int>(grid.tilesX());
            const int tilesZ = static_cast<int>(grid.tilesZ());
            for (int tz = 0; tz < tilesZ; ++tz)
            {
                for (int tx = 0; tx < tilesX; ++tx)
                {
                    const std::vector<Terrain::FoliageRecord>* tile = grid.residentFoliage(tx, tz);
                    if (!tile)
                        continue;
                    hash = mixStamp(hash, static_cast<uint64_t>(static_cast<uint32_t>(tx)));
                    hash = mixStamp(hash, static_cast<uint64_t>(static_cast<uint32_t>(tz)));
                    hash = mixStamp(hash, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(tile->data())));
                    hash = mixStamp(hash, static_cast<uint64_t>(tile->size()));
                }
            }
            return hash;
        }

        void gatherEditor(
            const Terrain::TerrainGrid& grid,
            const std::vector<Terrain::FoliageRecord>& foliage,
            float playerX,
            float playerZ,
            std::vector<Terrain::FoliageRecord>& out)
        {
            for (const Terrain::FoliageRecord& rec : foliage)
            {
                if (!insideRadius(rec.x, rec.z, playerX, playerZ))
                    continue;
                if (!editorRecordResident(grid, rec.x, rec.z))
                    continue;
                out.push_back(rec);
            }
        }

        void gatherSandbox(
            const Terrain::TerrainGrid& grid,
            float playerX,
            float playerZ,
            std::vector<Terrain::FoliageRecord>& out)
        {
            if (!grid.valid())
                return;
            const int tilesX = static_cast<int>(grid.tilesX());
            const int tilesZ = static_cast<int>(grid.tilesZ());
            for (int tz = 0; tz < tilesZ; ++tz)
            {
                for (int tx = 0; tx < tilesX; ++tx)
                {
                    const std::vector<Terrain::FoliageRecord>* tile = grid.residentFoliage(tx, tz);
                    if (!tile || tile->empty())
                        continue;
                    if (!tileHitsDisk(grid, tx, tz, playerX, playerZ))
                        continue;
                    for (const Terrain::FoliageRecord& rec : *tile)
                    {
                        if (!insideRadius(rec.x, rec.z, playerX, playerZ))
                            continue;
                        out.push_back(rec);
                    }
                }
            }
        }
    } // namespace

    uint32_t selectFoliageBodies(
        const Terrain::FoliageRecord* records,
        uint32_t count,
        float playerX,
        float playerZ,
        uint32_t* outIndices,
        uint32_t outCapacity,
        uint32_t* qualified)
    {
        if (qualified)
            *qualified = 0;
        if (!records || count == 0)
            return 0;

        std::vector<FoliageCand> cands;
        cands.reserve(static_cast<size_t>(count) < 4096u ? count : 4096u);
        for (uint32_t i = 0; i < count; ++i)
        {
            const Terrain::FoliageRecord& rec = records[i];
            const uint8_t kind                = rec.kind;
            if (kind != static_cast<uint8_t>(Terrain::FoliageKind::Tree) && kind != static_cast<uint8_t>(Terrain::FoliageKind::Rock))
                continue;
            const float dx = rec.x - playerX;
            const float dz = rec.z - playerZ;
            const float distSq = dx * dx + dz * dz;
            if (!(distSq <= kRadiusSq))
                continue;
            FoliageCand cand;
            cand.distSq = distSq;
            cand.index  = i;
            cands.push_back(cand);
        }

        if (qualified)
            *qualified = static_cast<uint32_t>(cands.size());

        const uint32_t limit = outCapacity < kMaxLiveFoliageBodies ? outCapacity : kMaxLiveFoliageBodies;
        uint32_t kept        = static_cast<uint32_t>(cands.size());
        if (kept > limit)
        {
            std::nth_element(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(limit), cands.end(), nearerCand);
            kept = limit;
        }
        if (kept > 1)
            std::sort(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(kept), nearerCand);

        if (outIndices)
        {
            for (uint32_t i = 0; i < kept; ++i)
                outIndices[i] = cands[static_cast<size_t>(i)].index;
        }
        return kept;
    }

    void destroyFoliageCollision(PhysicsWorld& world, FoliageCollisionSet& set)
    {
        if (world.valid())
        {
            for (PhysicsBodyId id : set.bodies)
                world.destroyBody(id);
        }
        set.bodies.clear();
        set.built = false;
    }

    void syncFoliageCollision(
        PhysicsWorld& world,
        FoliageCollisionSet& set,
        const Terrain::TerrainGrid& grid,
        const std::vector<Terrain::FoliageRecord>* editorFoliage,
        float playerX,
        float playerZ)
    {
        if (!world.valid())
        {
            set.bodies.clear();
            set.built = false;
            if (!set.invalidLogged)
            {
                DE_LOG_WARN(LogCategory::Collision, "Foliage collision: physics world is not valid");
                set.invalidLogged = true;
            }
            return;
        }

        const uint64_t stamp = editorFoliage ? editorStamp(grid, *editorFoliage) : sandboxStamp(grid);
        if (set.built && set.sourceStamp == stamp)
        {
            const float dx = playerX - set.anchorX;
            const float dz = playerZ - set.anchorZ;
            if (!((dx * dx + dz * dz) >= kRebuildSq))
                return;
        }

        std::vector<Terrain::FoliageRecord> gathered;
        if (editorFoliage)
            gatherEditor(grid, *editorFoliage, playerX, playerZ, gathered);
        else
            gatherSandbox(grid, playerX, playerZ, gathered);

        for (PhysicsBodyId id : set.bodies)
            world.destroyBody(id);
        set.bodies.clear();

        uint32_t qualified = 0;
        std::vector<uint32_t> indices(static_cast<size_t>(kMaxLiveFoliageBodies));
        const uint32_t gatheredCount = gathered.size() > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(gathered.size());
        const uint32_t kept = selectFoliageBodies(
            gathered.empty() ? nullptr : gathered.data(),
            gatheredCount,
            playerX,
            playerZ,
            indices.empty() ? nullptr : indices.data(),
            kMaxLiveFoliageBodies,
            &qualified);
        if (qualified > kept)
        {
            DE_LOG_WARN(LogCategory::Collision, "Foliage collision: {} qualifying trees and rocks, keeping {}", qualified, kept);
        }

        set.bodies.reserve(kept);
        for (uint32_t i = 0; i < kept; ++i)
        {
            PhysicsBoxDesc box;
            if (!fillFoliageBox(gathered[indices[static_cast<size_t>(i)]], box))
                continue;
            const PhysicsBodyId id = world.createBox(box);
            if (id != kNullPhysicsBody)
                set.bodies.push_back(id);
        }

        set.anchorX     = playerX;
        set.anchorZ     = playerZ;
        set.sourceStamp = stamp;
        set.built       = true;
    }

} // namespace Dark::Physics
