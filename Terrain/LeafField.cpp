#include "Terrain/LeafField.h"

#include "Terrain/LeafWind.h"

#include <algorithm>
#include <cmath>

namespace Dark::Terrain
{
    namespace
    {
        // Owned flight. Tests compare calm and windy landings, so the numbers stay relational.
        constexpr float kLeafSpeedMps   = 8.0f;
        constexpr float kDownAccel      = 6.0f;
        constexpr float kHorizResponse  = 8.0f;
        constexpr float kCalmShedPerSec = 0.40f;
        constexpr float kWindShedPerSec = 2.2f;

        float clampf(float v, float lo, float hi)
        {
            return std::max(lo, std::min(hi, v));
        }
    } // namespace

    LeafField::LeafField()
    {
        resetPool();
    }

    void LeafField::resetPool()
    {
        m_living       = 0;
        m_spawnedTotal = 0;
        m_nextSerial   = 1;
        m_cursor       = 0;
        m_freeCount    = kPoolSize;
        for (int i = 0; i < kPoolSize; ++i)
        {
            m_slots[i] = SimLeaf{};
            m_free[i]  = kPoolSize - 1 - i;
        }
        for (int i = 0; i < kMaxCrowns; ++i)
            m_crowns[i] = CrownRun{};
    }

    void LeafField::clear()
    {
        m_shedScale = 1.0f;
        resetPool();
    }

    void LeafField::setShedScale(float scale)
    {
        m_shedScale = clampf(scale, 0.0f, 1.0f);
    }

    bool LeafField::copySlot(int index, LeafSlot& out) const
    {
        if (index < 0 || index >= kPoolSize)
            return false;
        const SimLeaf& slot = m_slots[index];
        out.phase           = slot.phase;
        out.crownId         = slot.crownId;
        out.serial          = slot.serial;
        out.x               = slot.x;
        out.y               = slot.y;
        out.z               = slot.z;
        out.vx              = slot.vx;
        out.vy              = slot.vy;
        out.vz              = slot.vz;
        out.nx              = slot.nx;
        out.ny              = slot.ny;
        out.nz              = slot.nz;
        out.phaseTime       = slot.phaseTime;
        return true;
    }

    int LeafField::findOrAddCrown(const Crown& crown)
    {
        int freeIndex = -1;
        for (int i = 0; i < kMaxCrowns; ++i)
        {
            if (m_crowns[i].used && m_crowns[i].id == crown.id)
            {
                m_crowns[i].x      = crown.x;
                m_crowns[i].z      = crown.z;
                m_crowns[i].crownY = crown.crownY;
                return i;
            }
            if (!m_crowns[i].used && freeIndex < 0)
                freeIndex = i;
        }
        if (freeIndex < 0)
            return -1;
        CrownRun& run = m_crowns[freeIndex];
        run           = CrownRun{};
        run.used      = true;
        run.id        = crown.id;
        run.x         = crown.x;
        run.z         = crown.z;
        run.crownY    = crown.crownY;
        return freeIndex;
    }

    void LeafField::freeSlot(int index)
    {
        SimLeaf& slot = m_slots[index];
        if (slot.phase == LeafPhase::Free)
            return;
        for (int i = 0; i < kMaxCrowns; ++i)
        {
            if (m_crowns[i].used && m_crowns[i].id == slot.crownId && m_crowns[i].living > 0)
            {
                --m_crowns[i].living;
                break;
            }
        }
        slot.phase = LeafPhase::Free;
        if (m_living > 0)
            --m_living;
        if (m_freeCount < kPoolSize)
            m_free[m_freeCount++] = index;
    }

    int LeafField::oldestRestSlot(uint32_t crownId) const
    {
        int      oldest = -1;
        uint32_t serial = 0;
        for (int i = 0; i < kPoolSize; ++i)
        {
            const SimLeaf& slot = m_slots[i];
            if (slot.phase != LeafPhase::Rest || slot.crownId != crownId)
                continue;
            if (oldest < 0 || slot.serial < serial)
            {
                oldest = i;
                serial = slot.serial;
            }
        }
        return oldest;
    }

    bool LeafField::spawnLeaf(CrownRun& crown)
    {
        if (m_freeCount <= 0)
            return false;
        const int index = m_free[--m_freeCount];
        SimLeaf&  slot  = m_slots[index];
        slot            = SimLeaf{};
        slot.phase      = LeafPhase::Airborne;
        slot.crownId    = crown.id;
        slot.serial     = m_nextSerial++;
        slot.x          = crown.x;
        slot.y          = crown.crownY;
        slot.z          = crown.z;
        slot.ny         = 1.0f;
        ++crown.living;
        ++m_living;
        ++m_spawnedTotal;
        return true;
    }

    void LeafField::sampleWind(const Step& in, float x, float z, float& dirX, float& dirZ, float& strength) const
    {
        if (in.wind)
        {
            in.wind(x, z, in.simTime, dirX, dirZ, strength, in.windUser);
            return;
        }
        const LeafWindSample sample = sampleLeafWind(x, z, in.simTime);
        dirX                         = sample.dirX;
        dirZ                         = sample.dirZ;
        strength                     = sample.strength;
    }

    float LeafField::restHeight(const Step& in, float x, float z, float bedY)
    {
        float rest = bedY;
        if (in.lake && bedY < in.lakeLevel)
            rest = std::max(rest, in.lakeLevel);
        for (int i = 0; i < in.pondCount; ++i)
        {
            const Pond& pond = in.ponds[i];
            const bool  inside = x >= pond.minX && x <= pond.maxX && z >= pond.minZ && z <= pond.maxZ;
            if (inside && bedY < pond.level)
                rest = std::max(rest, pond.level);
        }
        return rest;
    }

    void LeafField::step(const Step& in)
    {
        if (!(in.dt > 0.0f))
            return;

        (void)in.listenerX;
        (void)in.listenerY;
        (void)in.listenerZ;

        for (int i = 0; i < kPoolSize; ++i)
        {
            SimLeaf& slot = m_slots[i];
            if (slot.phase != LeafPhase::Rest && slot.phase != LeafPhase::Fade)
                continue;
            slot.phaseTime += in.dt;
            if (slot.phase == LeafPhase::Rest && slot.phaseTime >= kRestSeconds)
            {
                slot.phase     = LeafPhase::Fade;
                slot.phaseTime = 0.0f;
            }
            else if (slot.phase == LeafPhase::Fade && slot.phaseTime >= kFadeSeconds)
            {
                freeSlot(i);
            }
        }

        const float cell = in.cellMetres > 0.0f ? in.cellMetres : 1.0f;
        for (int i = 0; i < kPoolSize; ++i)
        {
            SimLeaf& slot = m_slots[i];
            if (slot.phase != LeafPhase::Airborne)
                continue;

            float remain = in.dt;
            while (remain > 1.0e-6f && slot.phase == LeafPhase::Airborne)
            {
                float dirX     = 0.0f;
                float dirZ     = 1.0f;
                float strength = 0.0f;
                sampleWind(in, slot.x, slot.z, dirX, dirZ, strength);
                const float targetX = dirX * strength * kLeafSpeedMps;
                const float targetZ = dirZ * strength * kLeafSpeedMps;

                const float horiz = std::sqrt(slot.vx * slot.vx + slot.vz * slot.vz);
                float       sub   = remain;
                if (horiz > 1.0e-3f && horiz * remain > cell)
                    sub = cell / horiz;

                const float damp = 1.0f - std::exp(-kHorizResponse * sub);
                slot.vx += (targetX - slot.vx) * damp;
                slot.vz += (targetZ - slot.vz) * damp;
                slot.vy -= kDownAccel * sub;

                slot.x += slot.vx * sub;
                slot.y += slot.vy * sub;
                slot.z += slot.vz * sub;
                slot.flightTime += sub;
                remain -= sub;

                float bedY = 0.0f;
                const bool hit = in.tryHeight && in.tryHeight(slot.x, slot.z, bedY, in.heightUser);
                if (!hit)
                {
                    if (slot.flightTime >= kFlightCapSeconds)
                        freeSlot(i);
                    continue;
                }

                const float surface = restHeight(in, slot.x, slot.z, bedY);
                if (slot.y <= surface)
                {
                    // Higher ground and a spawn already under the bed have no downward crossing.
                    float nx = 0.0f;
                    float ny = 1.0f;
                    float nz = 0.0f;
                    if (surface <= bedY && in.sampleNormal)
                    {
                        in.sampleNormal(slot.x, slot.z, nx, ny, nz, in.normalUser);
                    }
                    slot.y         = surface;
                    slot.vx        = 0.0f;
                    slot.vy        = 0.0f;
                    slot.vz        = 0.0f;
                    slot.nx        = nx;
                    slot.ny        = ny;
                    slot.nz        = nz;
                    slot.phase     = LeafPhase::Rest;
                    slot.phaseTime = 0.0f;
                }
                else if (slot.flightTime >= kFlightCapSeconds)
                {
                    freeSlot(i);
                }
            }
        }

        const int crownCount = in.crowns ? in.crownCount : 0;
        // Input presence keeps shed carry across a frustum miss. Idle runs that left the list are released.
        bool listed[kMaxCrowns] = {};
        for (int i = 0; i < crownCount; ++i)
        {
            const uint32_t id = in.crowns[i].id;
            for (int r = 0; r < kMaxCrowns; ++r)
            {
                if (!m_crowns[r].used || m_crowns[r].id != id)
                    continue;
                listed[r] = true;
                break;
            }
        }
        for (int r = 0; r < kMaxCrowns; ++r)
        {
            CrownRun& run = m_crowns[r];
            if (!run.used || listed[r] || run.living > 0)
                continue;
            run = CrownRun{};
        }

        int candidates[kMaxCrowns];
        int candidateCount = 0;
        const float radius2 = kGatherRadiusM * kGatherRadiusM;
        for (int i = 0; i < crownCount && candidateCount < kMaxCrowns; ++i)
        {
            const Crown& crown = in.crowns[i];
            const float  dx    = crown.x - in.cameraX;
            const float  dz    = crown.z - in.cameraZ;
            if (dx * dx + dz * dz > radius2)
                continue;
            if (in.inFrustum && !in.inFrustum(crown.x, crown.crownY, crown.z, in.frustumUser))
                continue;
            const int index = findOrAddCrown(crown);
            if (index < 0)
                continue;
            candidates[candidateCount++] = index;
        }

        const int cap = candidateCount > 0 ? std::max(1, (2 * kPoolSize) / candidateCount) : 0;
        if (cap > 0)
        {
            for (int c = 0; c < candidateCount; ++c)
            {
                CrownRun& crown = m_crowns[candidates[c]];
                while (crown.living > cap)
                {
                    const int oldest = oldestRestSlot(crown.id);
                    if (oldest < 0)
                        break;
                    freeSlot(oldest);
                }
            }
        }

        for (int c = 0; c < candidateCount; ++c)
        {
            CrownRun& crown    = m_crowns[candidates[c]];
            float     dirX     = 0.0f;
            float     dirZ     = 1.0f;
            float     strength = 0.0f;
            sampleWind(in, crown.x, crown.z, dirX, dirZ, strength);
            const float rate = m_shedScale * (kCalmShedPerSec + kWindShedPerSec * clampf(strength, 0.0f, 1.0f));
            crown.carry      = std::min(1.0f, crown.carry + rate * in.dt);
        }

        int spawned = 0;
        if (candidateCount > 0 && cap > 0)
        {
            const int start = m_cursor % candidateCount;
            int       visited = 0;
            for (int i = 0; i < candidateCount && spawned < kMaxSpawnsPerStep; ++i)
            {
                CrownRun& crown = m_crowns[candidates[(start + i) % candidateCount]];
                ++visited;
                if (crown.carry < 1.0f || crown.living >= cap)
                    continue;
                if (!spawnLeaf(crown))
                    continue;
                crown.carry -= 1.0f;
                if (crown.carry < 0.0f)
                    crown.carry = 0.0f;
                ++spawned;
            }
            if (spawned == 0)
                m_cursor = start + kMaxSpawnsPerStep;
            else
                m_cursor = start + visited;
        }
    }

} // namespace Dark::Terrain
