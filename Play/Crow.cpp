#include "Play/Crow.h"

#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Audio/AudioSystem.h"
#include "Character/HealthComponent.h"
#include "Core/EntityPins.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Quaternion.h"
#include "Render/GpuUpload.h"
#include "Render/Renderer.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/TerrainGrid.h"
#include "Weapons/HittableComponent.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace Dark
{
namespace
{

    constexpr float kScareRadius  = 10.0f;
    constexpr float kFlySpeed     = 6.5f;
    constexpr float kHopSpeed     = 1.6f;
    constexpr float kSeparateDist = 2.4f;
    constexpr float kNeighborDist = 12.0f;
    constexpr float kTreeMin      = 6.0f;
    constexpr float kTreeMax      = 28.0f;
    constexpr float kPerchLift    = 5.5f;
    // A grounded crow blocks the branch within 1.5 m, and that branch can sit
    // kTreeMax from the bird. The query has to cover both, not just the flock.
    constexpr float kNeighborQuery = 30.0f;
    constexpr float kPerchCell     = 16.0f;
    // kTreeMax plus the corner of a 16 m cell, with a little float slack.
    constexpr float kPerchGather   = 42.0f;

    uint32_t xorshift32(uint32_t& state)
    {
        uint32_t x = state == 0u ? 1u : state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x == 0u ? 1u : x;
        return state;
    }

    float unitFloat(uint32_t& state)
    {
        return static_cast<float>(xorshift32(state) >> 8) * (1.0f / 16777216.0f);
    }

    float horizLen(float x, float z)
    {
        return std::sqrt(x * x + z * z);
    }

    Math::Vector3f vec(float x, float y, float z)
    {
        return Math::Vector3f{ x, y, z };
    }

    void faceFlat(CrowBrain& brain)
    {
        const float hx = brain.velocity.x;
        const float hz = brain.velocity.z;
        if (hx * hx + hz * hz < 1.0e-4f)
            return;
        brain.yaw = std::atan2(hx, hz);
    }

    void setWing(CrowBrain& brain, bool flying, float dt)
    {
        if (flying)
        {
            brain.wingPhase += dt * 11.0f;
            brain.wingPose = std::sin(brain.wingPhase) >= 0.0f ? 2 : 1;
            brain.pitch    = Math::Clamp(-brain.velocity.y * 0.08f, -0.45f, 0.55f);
            return;
        }
        brain.wingPhase += dt * 3.0f;
        brain.wingPose = 0;
    }

    float nearestThreat(const CrowBrain& brain, const Math::Vector3f* threats, int threatCount, int* outIndex)
    {
        float best = 1.0e9f;
        int   pick = -1;
        for (int i = 0; i < threatCount; ++i)
        {
            const float dx = threats[i].x - brain.position.x;
            const float dz = threats[i].z - brain.position.z;
            const float d  = horizLen(dx, dz);
            if (d < best)
            {
                best = d;
                pick = i;
            }
        }
        if (outIndex)
            *outIndex = pick;
        return best;
    }

    bool perchFree(const CrowPerch& perch, const CrowNeighbor* neighbors, int neighborCount)
    {
        for (int i = 0; i < neighborCount; ++i)
        {
            if (neighbors[i].flying)
                continue;
            const float dx = neighbors[i].position.x - perch.x;
            const float dz = neighbors[i].position.z - perch.z;
            const float dy = neighbors[i].position.y - perch.y;
            if (dx * dx + dz * dz + dy * dy < 1.5f * 1.5f)
                return false;
        }
        return true;
    }

    // A scare tree has to be an escape, not the branch the bird is already on,
    // and not a tree that sits between the bird and the threat.
    bool pickTree(const CrowBrain& brain, const CrowNeighbor* neighbors, int neighborCount, const Math::Vector3f* threats, int threatCount, const CrowPerch* perches, int perchCount, bool requireSafer, CrowPerch& out)
    {
        const float threatD = nearestThreat(brain, threats, threatCount, nullptr);
        float       best    = 1.0e9f;
        bool        found   = false;
        for (int i = 0; i < perchCount; ++i)
        {
            const float dx = perches[i].x - brain.position.x;
            const float dz = perches[i].z - brain.position.z;
            const float d  = horizLen(dx, dz);
            if (d < kTreeMin || d > kTreeMax || d >= best)
                continue;
            if (!perchFree(perches[i], neighbors, neighborCount))
                continue;
            if (requireSafer && threatCount > 0)
            {
                float fromThreat = 1.0e9f;
                for (int t = 0; t < threatCount; ++t)
                {
                    const float tx = perches[i].x - threats[t].x;
                    const float tz = perches[i].z - threats[t].z;
                    const float td = horizLen(tx, tz);
                    if (td < fromThreat)
                        fromThreat = td;
                }
                if (fromThreat < threatD + 1.0f)
                    continue;
            }
            best  = d;
            out   = perches[i];
            found = true;
        }
        return found;
    }

    void goForage(CrowBrain& brain, float groundY)
    {
        const float ang = unitFloat(brain.rng) * Math::TwoPi;
        const float rad = 2.0f + unitFloat(brain.rng) * 3.0f;
        brain.state     = CrowState::Forage;
        brain.fleeToTree = false;
        brain.goal      = vec(brain.position.x + std::cos(ang) * rad, groundY + 0.12f, brain.position.z + std::sin(ang) * rad);
        brain.decision  = 8.0f + unitFloat(brain.rng) * 8.0f;
        brain.stateTime = 0.0f;
    }

    void goFlock(CrowBrain& brain)
    {
        brain.state      = CrowState::Flock;
        brain.fleeToTree = false;
        brain.decision   = 10.0f + unitFloat(brain.rng) * 8.0f;
        brain.stateTime  = 0.0f;
    }

    void goTree(CrowBrain& brain, const CrowPerch& perch)
    {
        brain.state      = CrowState::Flee;
        brain.fleeToTree = true;
        brain.goal       = vec(perch.x, perch.y, perch.z);
        brain.stateTime  = 0.0f;
    }

    void landPerch(CrowBrain& brain)
    {
        brain.state        = CrowState::Perch;
        brain.position     = brain.goal;
        brain.velocity     = vec(0.0f, 0.0f, 0.0f);
        brain.fleeToTree   = false;
        brain.pitch        = 0.0f;
        brain.wingPose     = 0;
        brain.decision     = 10.0f + unitFloat(brain.rng) * 10.0f;
        brain.stateTime    = 0.0f;
    }

    void clampFly(Math::Vector3f& velocity)
    {
        const float h = horizLen(velocity.x, velocity.z);
        if (h > kFlySpeed)
        {
            const float s = kFlySpeed / h;
            velocity.x *= s;
            velocity.z *= s;
        }
        velocity.y = Math::Clamp(velocity.y, -kFlySpeed, kFlySpeed);
    }

    void integrate(CrowBrain& brain, const Math::Vector3f& steer, float dt)
    {
        brain.velocity += steer * dt;
        clampFly(brain.velocity);
        brain.position += brain.velocity * dt;
        faceFlat(brain);
    }

    void tickFlock(CrowBrain& brain, float groundY, float dt, const CrowNeighbor* neighbors, int neighborCount, const CrowPerch* perches, int perchCount)
    {
        Math::Vector3f sep = vec(0.0f, 0.0f, 0.0f);
        Math::Vector3f align = vec(0.0f, 0.0f, 0.0f);
        Math::Vector3f coh = vec(0.0f, 0.0f, 0.0f);
        int            count = 0;
        for (int i = 0; i < neighborCount; ++i)
        {
            const float dx   = brain.position.x - neighbors[i].position.x;
            const float dy   = brain.position.y - neighbors[i].position.y;
            const float dz   = brain.position.z - neighbors[i].position.z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 0.05f)
                continue;
            if (dist < kSeparateDist)
            {
                const float push = (kSeparateDist - dist) / dist;
                sep.x += dx * push;
                sep.y += dy * push;
                sep.z += dz * push;
            }
            if (!neighbors[i].flying || dist > kNeighborDist)
                continue;
            align += neighbors[i].velocity;
            coh += neighbors[i].position;
            ++count;
        }

        Math::Vector3f steer = sep * 3.5f;
        if (count > 0)
        {
            const float inv = 1.0f / static_cast<float>(count);
            align = align * inv - brain.velocity;
            coh   = coh * inv - brain.position;
            steer += align * 0.7f;
            steer += coh * 0.25f;
        }

        const float homeDx = brain.homeX - brain.position.x;
        const float homeDz = brain.homeZ - brain.position.z;
        const float homeD  = horizLen(homeDx, homeDz);
        const float homeW  = homeD > 18.0f ? 1.4f : 0.25f;
        steer.x += homeDx * homeW;
        steer.z += homeDz * homeW;

        const float wantY = groundY + brain.cruise;
        steer.y += (wantY - brain.position.y) * 1.6f;
        if (brain.position.y < groundY + 4.0f)
            steer.y += 10.0f;

        integrate(brain, steer * 2.2f, dt);
        if (brain.position.y < groundY + 3.0f)
        {
            brain.position.y = groundY + 3.0f;
            if (brain.velocity.y < 1.0f)
                brain.velocity.y = 1.0f;
        }
        setWing(brain, true, dt);

        brain.decision -= dt;
        if (brain.decision > 0.0f)
            return;
        CrowPerch perch{};
        const float roll = unitFloat(brain.rng);
        if (roll < 0.4f && pickTree(brain, neighbors, neighborCount, nullptr, 0, perches, perchCount, false, perch))
            goTree(brain, perch);
        else if (roll < 0.75f)
            goForage(brain, groundY);
        else
            brain.decision = 8.0f + unitFloat(brain.rng) * 8.0f;
    }

    void tickPerch(CrowBrain& brain, float dt, const CrowNeighbor* neighbors, int neighborCount, const CrowPerch* perches, int perchCount)
    {
        brain.velocity = vec(0.0f, 0.0f, 0.0f);
        brain.position = brain.goal;
        brain.position.y += std::sin(brain.stateTime * 2.2f) * 0.015f;
        brain.pitch = 0.0f;
        setWing(brain, false, dt);
        brain.decision -= dt;
        if (brain.decision > 0.0f)
            return;
        const float roll = unitFloat(brain.rng);
        if (roll < 0.55f)
            goForage(brain, brain.goal.y - kPerchLift);
        else
        {
            CrowPerch perch{};
            if (roll > 0.8f && pickTree(brain, neighbors, neighborCount, nullptr, 0, perches, perchCount, false, perch))
                goTree(brain, perch);
            else
                goFlock(brain);
        }
    }

    void tickForage(CrowBrain& brain, float groundY, float dt, const CrowNeighbor* neighbors, int neighborCount, const CrowPerch* perches, int perchCount)
    {
        const float dx   = brain.goal.x - brain.position.x;
        const float dz   = brain.goal.z - brain.position.z;
        const float dist = horizLen(dx, dz);
        const bool  hop  = std::sin(brain.wingPhase) > 0.15f;
        if (dist > 0.35f && hop)
        {
            brain.velocity.x = dx / dist * kHopSpeed;
            brain.velocity.z = dz / dist * kHopSpeed;
            brain.velocity.y = 0.0f;
            brain.position.x += brain.velocity.x * dt;
            brain.position.z += brain.velocity.z * dt;
            faceFlat(brain);
        }
        else
        {
            brain.velocity.x = 0.0f;
            brain.velocity.z = 0.0f;
        }

        const float bob     = hop ? std::sin(brain.wingPhase) * 0.14f : 0.0f;
        const float targetY = groundY + 0.10f + bob;
        if (brain.position.y > targetY + 0.05f)
            brain.position.y = Math::Max(targetY, brain.position.y - 7.0f * dt);
        else
            brain.position.y = targetY;

        brain.pitch = dist < 0.45f ? 0.55f : 0.1f;
        setWing(brain, false, dt);

        if (dist <= 0.35f && brain.stateTime > 1.2f)
        {
            const float ang = unitFloat(brain.rng) * Math::TwoPi;
            const float rad = 1.5f + unitFloat(brain.rng) * 2.5f;
            brain.goal      = vec(brain.position.x + std::cos(ang) * rad, groundY + 0.12f, brain.position.z + std::sin(ang) * rad);
            brain.stateTime = 0.0f;
        }

        brain.decision -= dt;
        if (brain.decision > 0.0f)
            return;
        CrowPerch perch{};
        if (unitFloat(brain.rng) < 0.5f && pickTree(brain, neighbors, neighborCount, nullptr, 0, perches, perchCount, false, perch))
            goTree(brain, perch);
        else
            goFlock(brain);
    }

    void tickFlee(CrowBrain& brain, float groundY, float dt, const Math::Vector3f* threats, int threatCount)
    {
        if (brain.fleeToTree)
        {
            const float dx = brain.goal.x - brain.position.x;
            const float dy = brain.goal.y - brain.position.y;
            const float dz = brain.goal.z - brain.position.z;
            const float d  = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (d < 0.8f)
            {
                landPerch(brain);
                return;
            }
            const Math::Vector3f desired = vec(dx / d * kFlySpeed, dy / d * kFlySpeed, dz / d * kFlySpeed);
            integrate(brain, (desired - brain.velocity) * 4.0f, dt);
            setWing(brain, true, dt);
            return;
        }

        int         threatIndex = -1;
        const float threatD     = nearestThreat(brain, threats, threatCount, &threatIndex);
        Math::Vector3f away     = vec(brain.velocity.x, 0.0f, brain.velocity.z);
        if (threatIndex >= 0)
            away = vec(brain.position.x - threats[threatIndex].x, 0.0f, brain.position.z - threats[threatIndex].z);
        if (away.MagnitudeSqrd() < 1.0e-4f)
            away = vec(std::sin(brain.yaw), 0.0f, std::cos(brain.yaw));
        else
            away.Normalize();

        const float wantY = groundY + brain.cruise;
        Math::Vector3f desired = vec(away.x * kFlySpeed, (wantY - brain.position.y) * 0.9f, away.z * kFlySpeed);
        integrate(brain, (desired - brain.velocity) * 4.0f, dt);
        if (brain.position.y < groundY + 2.0f)
        {
            brain.position.y = groundY + 2.0f;
            if (brain.velocity.y < 2.0f)
                brain.velocity.y = 2.0f;
        }
        setWing(brain, true, dt);
        if (threatD > kScareRadius * 1.8f && brain.position.y > groundY + 4.0f)
            goFlock(brain);
    }

    bool noticeThreat(CrowBrain& brain, const CrowNeighbor* neighbors, int neighborCount, const Math::Vector3f* threats, int threatCount, const CrowPerch* perches, int perchCount)
    {
        if (brain.state == CrowState::Flee)
            return false;
        const float dist = nearestThreat(brain, threats, threatCount, nullptr);
        if (dist > kScareRadius * 1.45f)
            brain.alarmLatched = false;
        if (dist > kScareRadius || brain.alarmLatched)
            return false;

        brain.alarmLatched = true;
        CrowPerch perch{};
        if (pickTree(brain, neighbors, neighborCount, threats, threatCount, perches, perchCount, true, perch))
            goTree(brain, perch);
        else
        {
            brain.state      = CrowState::Flee;
            brain.fleeToTree = false;
            brain.stateTime  = 0.0f;
        }
        return true;
    }

    AssetID poseModel(const CrowModels& models, int pose)
    {
        if (pose == 2)
            return models.up;
        if (pose == 1)
            return models.down;
        return models.folded;
    }

    bool threatTag(std::string_view name)
    {
        return name == "Hunter" || name == "hunter" || name == "Player" || name == "player" || name == "PlayerPawn";
    }

    int cellIndex(float v, float size)
    {
        return static_cast<int>(std::floor(v / size));
    }

    int hashCell(int cx, int cz, int mask)
    {
        const uint32_t h = static_cast<uint32_t>(cx) * 73856093u ^ static_cast<uint32_t>(cz) * 19349663u;
        return static_cast<int>(h & static_cast<uint32_t>(mask));
    }

    struct FlockBird
    {
        Math::Vector3f position{ 0.0f, 0.0f, 0.0f };
        Math::Vector3f velocity{ 0.0f, 0.0f, 0.0f };
        int            snap    = -1;
        int            next    = -1;
        bool           flying  = false;
    };

    struct PerchBin
    {
        int cellX = 0;
        int cellZ = 0;
        int begin = 0;
        int count = 0;
    };

    const PerchBin* findPerchBin(const std::vector<PerchBin>& bins, int cellX, int cellZ)
    {
        for (const PerchBin& bin : bins)
        {
            if (bin.cellX == cellX && bin.cellZ == cellZ)
                return &bin;
        }
        return nullptr;
    }

    void collectNeighbors(const FlockBird& self, const std::vector<FlockBird>& birds, const std::vector<int>& heads, int mask, std::vector<int>& picked)
    {
        picked.clear();
        const int   cx     = cellIndex(self.position.x, kNeighborQuery);
        const int   cz     = cellIndex(self.position.z, kNeighborQuery);
        const float limit2 = kNeighborQuery * kNeighborQuery;
        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int bucket = hashCell(cx + dx, cz + dz, mask);
                for (int i = heads[static_cast<size_t>(bucket)]; i >= 0; i = birds[static_cast<size_t>(i)].next)
                {
                    const FlockBird& other = birds[static_cast<size_t>(i)];
                    if (other.snap == self.snap)
                        continue;
                    const float dxp = other.position.x - self.position.x;
                    const float dyp = other.position.y - self.position.y;
                    const float dzp = other.position.z - self.position.z;
                    if (dxp * dxp + dyp * dyp + dzp * dzp > limit2)
                        continue;
                    picked.push_back(i);
                }
            }
        }
        std::sort(picked.begin(), picked.end(), [&](int a, int b) { return birds[static_cast<size_t>(a)].snap < birds[static_cast<size_t>(b)].snap; });
        // Two cells can share a bucket, so one bird can be visited twice.
        picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
    }

    bool uploadPose(Renderer& renderer, AssetManager& assets, AssetRef<Material> bodyMat, AssetRef<Material> beakMat, AssetRef<Material> eyeMat, float lift, const char* key, AssetRef<Model>& outModel, AssetID& outId)
    {
        MeshData body;
        MeshData beak;
        MeshData eyes;
        if (!buildCrowMesh(body, beak, eyes, lift))
            return false;

        Model::Part bodyPart;
        bodyPart.mesh        = std::move(body);
        bodyPart.material    = bodyMat;
        bodyPart.localToRoot = Math::Matrix4f();
        bodyPart.name        = "Body";

        Model::Part beakPart;
        beakPart.mesh        = std::move(beak);
        beakPart.material    = beakMat;
        beakPart.localToRoot = Math::Matrix4f();
        beakPart.name        = "Beak";

        Model::Part eyePart;
        eyePart.mesh        = std::move(eyes);
        eyePart.material    = eyeMat;
        eyePart.localToRoot = Math::Matrix4f();
        eyePart.name        = "Eyes";

        auto model = std::make_shared<Model>();
        if (!model->createFromParts({ std::move(bodyPart), std::move(beakPart), std::move(eyePart) }))
            return false;
        AssetRef<Model> uploaded = registerAndUploadModel(renderer, assets, model, key);
        if (!uploaded)
            return false;
        outModel = std::move(uploaded);
        outId    = outModel->id;
        return outId != NULL_ASSET;
    }

} // namespace

    bool ensureCrowModels(Renderer& renderer, AssetManager& assets, CrowModels& models)
    {
        if (models.ready)
            return true;

        AssetRef<Material> bodyMat = internSolidMaterial(assets, 28, 28, 32, 255, "runtime:/creature/crow-body-mat");
        AssetRef<Material> beakMat = internSolidMaterial(assets, 214, 148, 42, 255, "runtime:/creature/crow-beak-mat");
        AssetRef<Material> eyeMat  = internSolidMaterial(assets, 230, 230, 220, 255, "runtime:/creature/crow-eye-mat");
        if (!bodyMat || !beakMat || !eyeMat)
        {
            DE_LOG_ERROR(LogCategory::Render, "Crow: material upload failed");
            return false;
        }

        models.foldedKeep.reset();
        models.downKeep.reset();
        models.upKeep.reset();
        models.folded = NULL_ASSET;
        models.down   = NULL_ASSET;
        models.up     = NULL_ASSET;
        const bool ok = uploadPose(renderer, assets, bodyMat, beakMat, eyeMat, -1.05f, "runtime:/creature/crow-folded", models.foldedKeep, models.folded)
            && uploadPose(renderer, assets, bodyMat, beakMat, eyeMat, -0.35f, "runtime:/creature/crow-down", models.downKeep, models.down)
            && uploadPose(renderer, assets, bodyMat, beakMat, eyeMat, 0.75f, "runtime:/creature/crow-up", models.upKeep, models.up);
        models.ready = ok;
        if (!ok)
            DE_LOG_ERROR(LogCategory::Render, "Crow: model upload failed");
        return ok;
    }

    void initCrow(CrowBrain& brain, float x, float z, float groundY, float homeX, float homeZ, uint32_t seed, int slot, const CrowPerch* perches, int perchCount)
    {
        brain           = {};
        brain.rng       = seed == 0u ? 1u : seed;
        brain.rng      ^= static_cast<uint32_t>(slot) * 0x9E3779B9u;
        if (brain.rng == 0u)
            brain.rng = 1u;
        brain.homeX  = homeX;
        brain.homeZ  = homeZ;
        brain.cruise = 7.0f + unitFloat(brain.rng) * 3.0f;
        brain.yaw    = unitFloat(brain.rng) * Math::TwoPi;

        const int variant = slot % 3;
        if (variant == 1 && perches && perchCount > 0)
        {
            int   best  = -1;
            float bestD = 1.0e9f;
            for (int i = 0; i < perchCount; ++i)
            {
                const float d = horizLen(perches[i].x - x, perches[i].z - z);
                if (d < bestD)
                {
                    bestD = d;
                    best  = i;
                }
            }
            // A tree across the level is not a perch. Fall through to the ground.
            if (best >= 0 && bestD <= 40.0f)
            {
                brain.state    = CrowState::Perch;
                brain.position = vec(perches[best].x, perches[best].y, perches[best].z);
                brain.goal     = brain.position;
                brain.decision = 12.0f + unitFloat(brain.rng) * 8.0f;
                brain.wingPose = 0;
                return;
            }
        }
        if (variant == 2)
        {
            brain.state    = CrowState::Forage;
            brain.position = vec(x, groundY + 0.12f, z);
            goForage(brain, groundY);
            brain.position = vec(x, groundY + 0.12f, z);
            return;
        }

        const float ang = unitFloat(brain.rng) * Math::TwoPi;
        brain.state     = CrowState::Flock;
        brain.position  = vec(x, groundY + brain.cruise, z);
        brain.velocity  = vec(std::cos(ang) * 2.0f, 0.0f, std::sin(ang) * 2.0f);
        brain.decision  = 12.0f + unitFloat(brain.rng) * 8.0f;
        brain.yaw       = ang;
        brain.wingPose  = 2;
    }

    bool tickCrow(CrowBrain& brain, float groundY, float dt, const CrowNeighbor* neighbors, int neighborCount, const Math::Vector3f* threats, int threatCount, const CrowPerch* perches, int perchCount)
    {
        if (dt < 0.0f)
            dt = 0.0f;
        if (!std::isfinite(groundY))
            groundY = 0.0f;
        brain.stateTime += dt;

        const bool caw = noticeThreat(brain, neighbors, neighborCount, threats, threatCount, perches, perchCount);
        if (brain.state == CrowState::Flock)
            tickFlock(brain, groundY, dt, neighbors, neighborCount, perches, perchCount);
        else if (brain.state == CrowState::Perch)
            tickPerch(brain, dt, neighbors, neighborCount, perches, perchCount);
        else if (brain.state == CrowState::Forage)
            tickForage(brain, groundY, dt, neighbors, neighborCount, perches, perchCount);
        else
            tickFlee(brain, groundY, dt, threats, threatCount);
        return caw;
    }

    void gatherCrowPerches(const Terrain::TerrainGrid& grid, float x, float z, float radius, std::vector<CrowPerch>& out)
    {
        out.clear();
        if (radius < 0.0f || !grid.valid())
            return;
        const float tileWorld = static_cast<float>(grid.tileCells()) * grid.cellSize();
        if (!(tileWorld > 0.0f))
            return;
        const float radiusSq = radius * radius;
        const Math::Vector3f origin = grid.origin();
        int minTx = static_cast<int>(std::floor((x - radius - origin.x) / tileWorld));
        int maxTx = static_cast<int>(std::floor((x + radius - origin.x) / tileWorld));
        int minTz = static_cast<int>(std::floor((z - radius - origin.z) / tileWorld));
        int maxTz = static_cast<int>(std::floor((z + radius - origin.z) / tileWorld));
        if (minTx < 0)
            minTx = 0;
        if (minTz < 0)
            minTz = 0;
        const int tilesX = static_cast<int>(grid.tilesX());
        const int tilesZ = static_cast<int>(grid.tilesZ());
        if (maxTx >= tilesX)
            maxTx = tilesX - 1;
        if (maxTz >= tilesZ)
            maxTz = tilesZ - 1;
        if (minTx > maxTx || minTz > maxTz)
            return;
        for (int tz = minTz; tz <= maxTz; ++tz)
        {
            for (int tx = minTx; tx <= maxTx; ++tx)
            {
                const std::vector<Terrain::FoliageRecord>* records = grid.residentFoliage(tx, tz);
                if (!records)
                    continue;
                for (const Terrain::FoliageRecord& rec : *records)
                {
                    if (rec.kind != static_cast<uint8_t>(Terrain::FoliageKind::Tree))
                        continue;
                    const float dx = rec.x - x;
                    const float dz = rec.z - z;
                    if (dx * dx + dz * dz > radiusSq)
                        continue;
                    const float scale = rec.scale > 0.05f ? rec.scale : 1.0f;
                    CrowPerch perch{};
                    perch.x = rec.x;
                    perch.y = rec.y + kPerchLift * scale;
                    perch.z = rec.z;
                    out.push_back(perch);
                }
            }
        }
    }

    void gatherCrowThreats(World& world, std::vector<Math::Vector3f>& out)
    {
        out.clear();
        world.each<TagComponent>([&](Entity e, TagComponent& tag) {
            if (!threatTag(tag.name))
                return;
            if (const HealthComponent* hp = world.get<HealthComponent>(e))
            {
                if (!hp->health.alive())
                    return;
            }
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            out.push_back(xf->position);
        });
    }

    void simulateCrows(World& world, const Terrain::TerrainGrid* terrain, CrowModels* models, AssetManager* assets, AssetPinTable* pins, Audio::AudioSystem* audio, float dt)
    {
        if (dt < 0.0f)
            return;

        struct Snap
        {
            Entity    entity{};
            CrowBrain brain{};
            bool      alive = false;
        };
        std::vector<Snap> snaps;
        world.each<CrowComponent>([&](Entity e, CrowComponent& crow) {
            Snap snap;
            snap.entity = e;
            snap.brain  = crow.brain;
            const HealthComponent* hp = world.get<HealthComponent>(e);
            snap.alive = !hp || hp->health.alive();
            snaps.push_back(snap);
        });
        if (snaps.empty())
            return;

        std::vector<Math::Vector3f> threats;
        gatherCrowThreats(world, threats);

        std::vector<FlockBird> birds;
        birds.reserve(snaps.size());
        std::vector<int> birdOfSnap(snaps.size(), -1);
        for (int i = 0; i < static_cast<int>(snaps.size()); ++i)
        {
            const Snap& snap = snaps[static_cast<size_t>(i)];
            if (!snap.alive)
                continue;
            FlockBird bird;
            bird.position = snap.brain.position;
            bird.velocity = snap.brain.velocity;
            bird.snap     = i;
            bird.flying   = snap.brain.state == CrowState::Flock || snap.brain.state == CrowState::Flee;
            birdOfSnap[static_cast<size_t>(i)] = static_cast<int>(birds.size());
            birds.push_back(bird);
        }

        int buckets = 16;
        const int need = birds.empty() ? 16 : static_cast<int>(birds.size()) * 2;
        while (buckets < need)
            buckets *= 2;
        std::vector<int> heads(static_cast<size_t>(buckets), -1);
        const int mask = buckets - 1;
        for (int i = 0; i < static_cast<int>(birds.size()); ++i)
        {
            FlockBird& bird = birds[static_cast<size_t>(i)];
            const int  bucket = hashCell(cellIndex(bird.position.x, kNeighborQuery), cellIndex(bird.position.z, kNeighborQuery), mask);
            bird.next = heads[static_cast<size_t>(bucket)];
            heads[static_cast<size_t>(bucket)] = i;
        }

        // One centroid hides every flock that is not standing on it. Each 16 m
        // cell keeps the trees its own birds can reach.
        std::vector<CrowPerch> perchStore;
        std::vector<PerchBin>  perchBins;
        std::vector<CrowPerch> perchScratch;
        if (terrain)
        {
            for (const FlockBird& bird : birds)
            {
                const int cellX = cellIndex(bird.position.x, kPerchCell);
                const int cellZ = cellIndex(bird.position.z, kPerchCell);
                if (findPerchBin(perchBins, cellX, cellZ))
                    continue;
                const float qx = (static_cast<float>(cellX) + 0.5f) * kPerchCell;
                const float qz = (static_cast<float>(cellZ) + 0.5f) * kPerchCell;
                gatherCrowPerches(*terrain, qx, qz, kPerchGather, perchScratch);
                PerchBin bin;
                bin.cellX = cellX;
                bin.cellZ = cellZ;
                bin.begin = static_cast<int>(perchStore.size());
                bin.count = static_cast<int>(perchScratch.size());
                perchStore.insert(perchStore.end(), perchScratch.begin(), perchScratch.end());
                perchBins.push_back(bin);
            }
        }

        AssetRef<Audio::SoundClip> cawClip;
        std::vector<int>           picked;
        std::vector<CrowNeighbor>  neighbors;
        picked.reserve(birds.size());
        neighbors.reserve(birds.size());

        for (int i = 0; i < static_cast<int>(snaps.size()); ++i)
        {
            Snap& snap = snaps[static_cast<size_t>(i)];
            TransformComponent* xf = world.get<TransformComponent>(snap.entity);
            if (!xf)
                continue;
            const float groundY = terrain ? terrain->heightAtWorld(snap.brain.position.x, snap.brain.position.z) : 0.0f;
            if (!snap.alive)
            {
                const float rest = groundY + 0.12f;
                if (xf->position.y > rest)
                {
                    xf->position.y -= 9.0f * dt;
                    if (xf->position.y < rest)
                        xf->position.y = rest;
                }
                continue;
            }

            neighbors.clear();
            const int birdIndex = birdOfSnap[static_cast<size_t>(i)];
            if (birdIndex >= 0)
            {
                collectNeighbors(birds[static_cast<size_t>(birdIndex)], birds, heads, mask, picked);
                for (int pick : picked)
                {
                    const FlockBird& other = birds[static_cast<size_t>(pick)];
                    CrowNeighbor nb;
                    nb.position = other.position;
                    nb.velocity = other.velocity;
                    nb.flying   = other.flying;
                    neighbors.push_back(nb);
                }
            }

            const CrowPerch* perchPtr = nullptr;
            int              perchCount = 0;
            if (birdIndex >= 0)
            {
                const PerchBin* bin = findPerchBin(perchBins, cellIndex(snap.brain.position.x, kPerchCell), cellIndex(snap.brain.position.z, kPerchCell));
                if (bin && bin->count > 0)
                {
                    perchPtr   = perchStore.data() + bin->begin;
                    perchCount = bin->count;
                }
            }

            const bool caw = tickCrow(snap.brain, groundY, dt, neighbors.empty() ? nullptr : neighbors.data(), static_cast<int>(neighbors.size()),
                threats.empty() ? nullptr : threats.data(), static_cast<int>(threats.size()), perchPtr, perchCount);

            if (CrowComponent* crow = world.get<CrowComponent>(snap.entity))
                crow->brain = snap.brain;

            xf->position = snap.brain.position;
            const float cp = std::cos(snap.brain.pitch);
            const float sp = std::sin(snap.brain.pitch);
            const Math::Vector3f forward{ std::sin(snap.brain.yaw) * cp, sp, std::cos(snap.brain.yaw) * cp };
            xf->rotation = Math::Quaternion::FromLookRotation(forward, Math::Vector3f{ 0.0f, 1.0f, 0.0f });

            // Writing the ID directly skips the pin. Stop Play then unpins an ID the table never recorded.
            if (models && models->ready && pins && assets)
            {
                const AssetID next = poseModel(*models, snap.brain.wingPose);
                if (ModelComponent* model = world.get<ModelComponent>(snap.entity); model && next != NULL_ASSET && model->modelAssetID != next)
                {
                    ModelComponent updated = *model;
                    updated.modelAssetID   = next;
                    setModelComponent(world, *pins, *assets, snap.entity, updated);
                }
            }
            if (caw && audio && assets)
            {
                if (!cawClip)
                    cawClip = audio->createTone(*assets, 980.0f, 0.16f, 0.35f);
                if (cawClip)
                {
                    Audio::PlayDesc desc;
                    desc.volume      = 0.8f;
                    desc.pitch       = 0.82f + static_cast<float>(snap.brain.rng & 7u) * 0.05f;
                    desc.spatial     = true;
                    desc.position    = snap.brain.position;
                    desc.minDistance = 4.0f;
                    desc.maxDistance = 56.0f;
                    audio->play(cawClip, desc);
                }
            }
        }
    }

    Entity spawnCrow(World& world, Renderer& renderer, AssetManager& assets, AssetPinTable& pins, CrowModels& models,
        const Terrain::TerrainGrid* terrain, Entity spawner, const SpawnerRequest& req, float groundY, float homeX, float homeZ)
    {
        if (!ensureCrowModels(renderer, assets, models))
        {
            DE_LOG_WARN(LogCategory::AI, "Crow: spawner skipped a crow because the model is not ready");
            return {};
        }

        std::vector<CrowPerch> perches;
        if (terrain)
            gatherCrowPerches(*terrain, homeX, homeZ, 40.0f, perches);

        Entity e = world.createEntity();
        world.emplace<TagComponent>(e, "Crow");

        CrowComponent crow;
        const uint32_t seed = req.seed == 0u ? 1u : req.seed;
        initCrow(crow.brain, req.x, req.z, groundY, homeX, homeZ, seed, req.slot, perches.empty() ? nullptr : perches.data(), static_cast<int>(perches.size()));
        world.emplace<CrowComponent>(e, crow);

        TransformComponent xf;
        xf.position = crow.brain.position;
        xf.scale    = Math::Vector3f{ 1.0f, 1.0f, 1.0f };
        const float cp = std::cos(crow.brain.pitch);
        const float sp = std::sin(crow.brain.pitch);
        xf.rotation = Math::Quaternion::FromLookRotation(
            Math::Vector3f{ std::sin(crow.brain.yaw) * cp, sp, std::cos(crow.brain.yaw) * cp },
            Math::Vector3f{ 0.0f, 1.0f, 0.0f });
        world.emplace<TransformComponent>(e, xf);

        ModelComponent model;
        model.modelAssetID = poseModel(models, crow.brain.wingPose);
        model.castShadow   = true;
        model.visible      = true;
        setModelComponent(world, pins, assets, e, model);

        HealthSettings settings;
        settings.maxHp       = 12.0f;
        settings.regenPerSec = 0.0f;
        HealthComponent health;
        health.health = Health(settings);
        world.emplace<HealthComponent>(e, health);

        HittableComponent hittable;
        hittable.halfExtents = Math::Vector3f{ 0.28f, 0.2f, 0.34f };
        world.emplace<HittableComponent>(e, hittable);

        SpawnedUnitComponent mark;
        mark.spawner = spawner;
        mark.slot    = req.slot;
        world.emplace<SpawnedUnitComponent>(e, mark);
        return e;
    }

} // namespace Dark
