#include "Render/DecalPool.h"

#include "Animation/AnimGraphComponent.h"
#include "Character/HealthComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "Math/Box3f.h"
#include "Math/MathHelper.h"
#include "Math/Vector4f.h"
#include "Render/DecalBasis.h"

#include <algorithm>
#include <cstring>

namespace Dark
{
    using namespace Math;

    namespace
    {

        Matrix4f entityMatrix(const TransformComponent& xf)
        {
            const Matrix4f S = Matrix4f::ScaleMatrixXYZ(xf.scale.x, xf.scale.y, xf.scale.z);
            const Matrix4f R = xf.rotation.ToMatrix4();
            const Matrix4f T = Matrix4f::TranslationMatrix(xf.position.x, xf.position.y, xf.position.z);
            return S * R * T;
        }

        void copyMatrix(float dst[16], const Matrix4f& m)
        {
            std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
        }

        Vector3f unitBasis(const Vector3f& row, const Vector3f& fallback)
        {
            const float mag = row.Magnitude();
            if (mag <= 1.0e-8f)
                return fallback;
            return row * (1.0f / mag);
        }

        void fillGpu(const DecalDef& def, const Matrix4f& world, float age, float lifetime, DecalGpuInstance& out)
        {
            out = DecalGpuInstance{};
            copyMatrix(out.worldFromDecal, world);
            copyMatrix(out.decalFromWorld, world.Inverse());

            const float u       = (lifetime > 0.0f) ? Clamp(age / lifetime, 0.0f, 1.0f) : 1.0f;
            Vector3f    tint    = def.tint;
            float       albedoW = def.albedoWeight;
            float       normalW = def.normalWeight;
            float       roughW  = def.roughnessWeight;
            float       metalW  = def.metallicWeight;
            float       emisW   = def.emissiveWeight;
            float       emisT   = def.emissiveTarget;
            if (def.id == DecalDefId::Burn)
            {
                const DecalBurnBake bake = decalBakeBurn(u);
                tint                     = bake.tint;
                albedoW                  = bake.albedoWeight;
                normalW                  = bake.normalWeight;
                roughW                   = bake.roughnessWeight;
                metalW                   = 0.0f;
                emisW                    = bake.emissiveWeight;
                emisT                    = bake.emissiveTarget;
            }
            else
            {
                const float fade = decalFade(def.fade, u, def.fadeHold);
                albedoW *= fade;
                normalW *= fade;
                roughW *= fade;
                metalW *= fade;
                emisW *= fade;
            }

            out.tint[0]         = tint.x;
            out.tint[1]         = tint.y;
            out.tint[2]         = tint.z;
            out.albedoWeight    = albedoW;
            out.normalScale     = def.normalScale;
            out.normalWeight    = normalW;
            out.roughnessTarget = def.roughnessTarget;
            out.roughnessWeight = roughW;
            out.metallicTarget  = def.metallicTarget;
            out.metallicWeight  = metalW;
            out.emissiveTarget  = emisT;
            out.emissiveWeight  = emisW;
            out.uvScaleBias[0]  = 1.0f;
            out.uvScaleBias[1]  = 1.0f;
            out.uvScaleBias[2]  = 0.0f;
            out.uvScaleBias[3]  = 0.0f;

            const Vector3f axisY = unitBasis(world.GetBasisY(), Vector3f(0.0f, 1.0f, 0.0f));
            out.axisY[0]         = axisY.x;
            out.axisY[1]         = axisY.y;
            out.axisY[2]         = axisY.z;
            out.angleFadeStart   = kDecalAngleFadeStart;
            out.angleFadeRange   = kDecalAngleFadeRange;
            out.channelMask      = def.channelMask;
            out.clipLocalYMin    = def.clipLocalYMin;
            out.clipLocalYMax    = def.clipLocalYMax;
        }

        Box3f slotBox(const Matrix4f& world, const Vector3f& halfExtents)
        {
            const Vector3f ax = unitBasis(world.GetBasisX(), Vector3f(1.0f, 0.0f, 0.0f));
            const Vector3f ay = unitBasis(world.GetBasisY(), Vector3f(0.0f, 1.0f, 0.0f));
            const Vector3f az = unitBasis(world.GetBasisZ(), Vector3f(0.0f, 0.0f, 1.0f));
            return Box3f(world.GetTranslation(), ax, ay, az, halfExtents.x, halfExtents.y, halfExtents.z);
        }

    } // namespace

    DecalPool::DecalPool()
    {
        reset();
    }

    void DecalPool::clear()
    {
        m_freeCount = 0;
        m_alive     = 0;
        for (uint32_t i = 0; i < kCapacity; ++i)
        {
            m_slots[i].alive      = false;
            m_slots[i].serial     = 0;
            m_free[m_freeCount++] = i;
        }
    }

    void DecalPool::reset()
    {
        clear();
        m_nextSerial    = 1;
        m_recycleCount  = 0;
        m_warnedRecycle = false;
        m_warnedWrap    = false;
    }

    void DecalPool::setNextSerialForTest(uint32_t serial)
    {
        m_nextSerial = serial == 0 ? 1u : serial;
    }

    bool DecalPool::alive(DecalId id) const
    {
        if (id.serial == 0 || id.index >= kCapacity)
            return false;
        const Slot& slot = m_slots[id.index];
        return slot.alive && slot.serial == id.serial;
    }

    void DecalPool::freeSlot(uint32_t index)
    {
        Slot& slot = m_slots[index];
        if (!slot.alive)
            return;
        slot.alive  = false;
        slot.serial = 0;
        if (m_alive > 0)
            --m_alive;
        if (m_freeCount < kCapacity)
            m_free[m_freeCount++] = index;
    }

    uint32_t DecalPool::findOldest() const
    {
        uint32_t best       = 0;
        uint32_t bestSerial = 0xFFFFFFFFu;
        for (uint32_t i = 0; i < kCapacity; ++i)
        {
            if (m_slots[i].alive && m_slots[i].serial < bestSerial)
            {
                bestSerial = m_slots[i].serial;
                best       = i;
            }
        }
        return best;
    }

    void DecalPool::compactSerials()
    {
        uint32_t slots[kCapacity];
        uint32_t n = 0;
        for (uint32_t i = 0; i < kCapacity; ++i)
        {
            if (m_slots[i].alive)
                slots[n++] = i;
        }
        std::sort(slots, slots + n, [&](uint32_t a, uint32_t b) { return m_slots[a].serial < m_slots[b].serial; });
        for (uint32_t i = 0; i < n; ++i)
            m_slots[slots[i]].serial = i + 1u;
        m_nextSerial = n + 1u;
        if (!m_warnedWrap)
        {
            m_warnedWrap = true;
            DE_LOG_WARN(LogCategory::Render, "DecalPool: serial counter wrapped, compacted");
        }
    }

    uint32_t DecalPool::allocateSerial()
    {
        if (m_nextSerial == 0xFFFFFFFFu)
            compactSerials();
        return m_nextSerial++;
    }

    bool DecalPool::spawn(const DecalSpawnDesc& desc, DecalId* outId)
    {
        const DecalDefId id = decalSelectDef(desc);
        if (id == DecalDefId::Count)
        {
            DE_LOG_ERROR(LogCategory::Render, "DecalPool: kind out of range");
            return false;
        }

        Vector3f axisX;
        Vector3f axisY;
        Vector3f axisZ;
        if (!decalBuildAxes(desc.axisY, desc.axisX, axisX, axisY, axisZ))
        {
            DE_LOG_ERROR(LogCategory::Render, "DecalPool: axisY is degenerate");
            return false;
        }

        const DecalDef& def   = decalDef(id);
        const Vector3f  half  = decalResolveHalfExtents(def, desc.halfExtents);
        float           scale = desc.lifetimeScale;
        if (!(scale > 0.0f))
            scale = 0.0f;

        const bool     recycled = m_freeCount == 0;
        const uint32_t index    = recycled ? findOldest() : m_free[--m_freeCount];
        const uint32_t serial   = allocateSerial();

        if (recycled)
        {
            ++m_recycleCount;
            if (!m_warnedRecycle)
            {
                m_warnedRecycle = true;
                DE_LOG_WARN(LogCategory::Render, "DecalPool: capacity {}, recycling oldest", kCapacity);
            }
        }
        else
            ++m_alive;

        Slot& slot       = m_slots[index];
        slot             = Slot{};
        slot.defId       = id;
        slot.space       = desc.space;
        slot.entity      = desc.entity;
        slot.bone        = desc.bone < 0 ? kNoBone : static_cast<uint16_t>(desc.bone);
        slot.world       = decalWorldMatrix(desc.position, axisX, axisY, axisZ, half);
        slot.halfExtents = half;
        slot.spawnPos    = desc.position;
        slot.age         = 0.0f;
        slot.lifetime    = def.lifetime * scale;
        slot.serial      = serial;
        slot.alive       = true;

        if (outId)
        {
            outId->index  = index;
            outId->serial = serial;
        }
        return true;
    }

    void DecalPool::tick(World& world, float dt)
    {
        if (dt < 0.0f)
            dt = 0.0f;

        for (uint32_t i = 0; i < kCapacity; ++i)
        {
            Slot& slot = m_slots[i];
            if (!slot.alive)
                continue;
            slot.age += dt;
            if (slot.lifetime <= 0.0f || slot.age >= slot.lifetime)
            {
                freeSlot(i);
                continue;
            }
            if (slot.space == DecalSpace::World)
                continue;
            if (!world.alive(slot.entity))
            {
                freeSlot(i);
                continue;
            }

            TransformComponent* xf = world.get<TransformComponent>(slot.entity);
            if (!xf)
            {
                freeSlot(i);
                continue;
            }

            const Matrix4f entityWorld = entityMatrix(*xf);
            if (slot.space == DecalSpace::Bone)
            {
                AnimGraphComponent* ag          = world.get<AnimGraphComponent>(slot.entity);
                const AnimPose*     pose        = ag ? &ag->graph.player().pose() : nullptr;
                const bool          missingPose = !pose || pose->boneCount == 0;
                if (slot.bone == kNoBone)
                {
                    if (missingPose)
                    {
                        freeSlot(i);
                        continue;
                    }
                    int            best      = -1;
                    float          bestDist  = 1.0e30f;
                    const uint32_t boneCount = pose->boneCount < AnimPose::kMaxBones ? pose->boneCount : AnimPose::kMaxBones;
                    for (uint32_t b = 0; b < boneCount; ++b)
                    {
                        const Vector3f joint = (pose->jointWorld[b] * entityWorld).GetTranslation();
                        const float    dist  = (joint - slot.spawnPos).Magnitude();
                        if (dist < bestDist)
                        {
                            bestDist = dist;
                            best     = static_cast<int>(b);
                        }
                    }
                    if (best < 0 || bestDist > kDecalClosestBoneMeters)
                        slot.space = DecalSpace::Entity;
                    else
                        slot.bone = static_cast<uint16_t>(best);
                }
                else if (missingPose || slot.bone >= pose->boneCount || slot.bone >= AnimPose::kMaxBones)
                {
                    freeSlot(i);
                    continue;
                }
            }

            Matrix4f parent = entityWorld;
            if (slot.space == DecalSpace::Bone)
            {
                AnimGraphComponent* ag = world.get<AnimGraphComponent>(slot.entity);
                parent                 = ag->graph.player().pose().jointWorld[slot.bone] * entityWorld;
            }

            if (!slot.hasLastPos)
            {
                // Spawn has no prior position, so this sample is not a revive or a teleport.
                slot.localOffset    = slot.world * parent.Inverse();
                HealthComponent* hp = world.get<HealthComponent>(slot.entity);
                slot.healthWasAlive = hp ? hp->health.alive() : true;
                slot.lastEntityPos  = xf->position;
                slot.hasLastPos     = true;
                slot.world          = slot.localOffset * parent;
                continue;
            }

            HealthComponent* hp       = world.get<HealthComponent>(slot.entity);
            const bool       nowAlive = hp ? hp->health.alive() : true;
            if (hp && !slot.healthWasAlive && nowAlive)
            {
                freeSlot(i);
                continue;
            }

            const Vector3f delta = xf->position - slot.lastEntityPos;
            const float    limit = Max(slot.halfExtents.x, Max(slot.halfExtents.y, slot.halfExtents.z));
            if (delta.MagnitudeSqrd() > limit * limit)
            {
                freeSlot(i);
                continue;
            }

            slot.healthWasAlive = nowAlive;
            slot.lastEntityPos  = xf->position;
            slot.world          = slot.localOffset * parent;
        }
    }

    uint32_t DecalPool::buildVisible(const Frustum3f& frustum, const Vector3f& cameraPos, DecalGpuInstance* outside, uint32_t outsideCap, uint32_t* outsideCount, DecalGpuInstance* inside,
                                     uint32_t insideCap, uint32_t* insideCount) const
    {
        uint32_t order[kCapacity];
        uint32_t n = 0;
        for (uint32_t i = 0; i < kCapacity; ++i)
        {
            if (m_slots[i].alive)
                order[n++] = i;
        }
        std::sort(order, order + n, [&](uint32_t a, uint32_t b) { return m_slots[a].serial < m_slots[b].serial; });

        uint32_t outsideN = 0;
        uint32_t insideN  = 0;
        for (uint32_t k = 0; k < n; ++k)
        {
            const Slot&     slot = m_slots[order[k]];
            const DecalDef& def  = decalDef(slot.defId);
            const Matrix4f  inv  = slot.world.Inverse();
            if (decalClipWorld(cameraPos, inv, def.clipLocalYMin, def.clipLocalYMax))
            {
                if (inside && insideN < insideCap)
                {
                    fillGpu(def, slot.world, slot.age, slot.lifetime, inside[insideN]);
                    ++insideN;
                }
                continue;
            }
            if (!frustum.Intersects(slotBox(slot.world, slot.halfExtents)))
                continue;
            if (outside && outsideN < outsideCap)
            {
                fillGpu(def, slot.world, slot.age, slot.lifetime, outside[outsideN]);
                ++outsideN;
            }
        }

        if (outsideCount)
            *outsideCount = outsideN;
        if (insideCount)
            *insideCount = insideN;
        return outsideN + insideN;
    }

} // namespace Dark
