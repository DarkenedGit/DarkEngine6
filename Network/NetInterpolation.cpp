// NetInterpolation.cpp — interp pose tracks and client writeback (NetworkSystem::)
#include "Network/NetworkSystem.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cmath>

namespace Dark
{
    void NetworkSystem::pushInterpSlot(NetId id, uint32_t tick, float px, float py, float pz, float qw, float qx, float qy, float qz)
    {
        InterpTrack& tr = m_interp[id];
        for (uint8_t i = 0; i < tr.count; ++i)
        {
            if (tr.slots[i].tick == tick)
            {
                tr.slots[i] = InterpPose{ tick, px, py, pz, qw, qx, qy, qz };
                if (!tr.hasLatest || static_cast<int32_t>(tick - tr.latestTick) >= 0)
                {
                    tr.latestTick = tick;
                    tr.hasLatest  = true;
                }
                return;
            }
        }

        InterpPose pose{ tick, px, py, pz, qw, qx, qy, qz };
        if (tr.count < kInterpSlots)
        {
            tr.slots[tr.count++] = pose;
        }
        else
        {
            uint8_t oldest = 0;
            for (uint8_t i = 1; i < kInterpSlots; ++i)
            {
                if (static_cast<int32_t>(tick - tr.slots[i].tick) > static_cast<int32_t>(tick - tr.slots[oldest].tick))
                    oldest = i;
            }
            tr.slots[oldest] = pose;
        }

        if (!tr.hasLatest || static_cast<int32_t>(tick - tr.latestTick) >= 0)
        {
            tr.latestTick = tick;
            tr.hasLatest  = true;
        }
    }

    bool NetworkSystem::sampleInterp(const InterpTrack& tr, uint32_t latestRecv, InterpPose& out) const
    {
        if (tr.count == 0)
            return false;
        if (tr.count == 1)
        {
            out = tr.slots[0];
            return true;
        }

        const InterpPose* latest = &tr.slots[0];
        for (uint8_t i = 1; i < tr.count; ++i)
        {
            if (static_cast<int32_t>(tr.slots[i].tick - latest->tick) > 0)
                latest = &tr.slots[i];
        }

        const int32_t     delay = static_cast<int32_t>(kNetInterpDelayTicks);
        const InterpPose* older = nullptr;
        const InterpPose* newer = nullptr;
        int32_t           olderDelta = 0;
        int32_t           newerDelta = 0;
        for (uint8_t i = 0; i < tr.count; ++i)
        {
            const int32_t d = static_cast<int32_t>(latestRecv - tr.slots[i].tick);
            if (d >= delay)
            {
                if (!older || d < olderDelta)
                {
                    older      = &tr.slots[i];
                    olderDelta = d;
                }
            }
            if (d <= delay)
            {
                if (!newer || d > newerDelta)
                {
                    newer      = &tr.slots[i];
                    newerDelta = d;
                }
            }
        }

        if (!older)
        {
            out = *latest;
            return true;
        }
        if (!newer || older == newer)
        {
            out = *older;
            return true;
        }

        const int32_t span = static_cast<int32_t>(newer->tick - older->tick);
        if (span <= 0)
        {
            out = *older;
            return true;
        }

        float t = static_cast<float>(olderDelta - delay) / static_cast<float>(span);
        if (t < 0.f)
            t = 0.f;
        if (t > 1.f)
            t = 1.f;

        const Math::Vector3f a(older->px, older->py, older->pz);
        const Math::Vector3f b(newer->px, newer->py, newer->pz);
        const Math::Vector3f p = a + (b - a) * t;
        Math::Quaternion     qa(older->qw, older->qx, older->qy, older->qz);
        Math::Quaternion     qb(newer->qw, newer->qx, newer->qy, newer->qz);
        Math::Quaternion     qr = Math::Quaternion::Slerp(qa, qb, t);
        qr.Normalize();

        out.tick = older->tick;
        out.px   = p.x;
        out.py   = p.y;
        out.pz   = p.z;
        out.qw   = qr.w;
        out.qx   = qr.x;
        out.qy   = qr.y;
        out.qz   = qr.z;
        return true;
    }

    void NetworkSystem::writeInterp(World& world)
    {
        if (m_role != NetRole::Client || !m_hasRecvTick)
            return;

        for (const auto& kv : m_netToEntity)
        {
            const NetId id = kv.first;
            Entity      e{ kv.second };
            NetworkedComponent* nc = world.get<NetworkedComponent>(e);
            if (!nc || !nc->replicateTransform)
                continue;
            if (nc->owner == m_localId)
                continue;
            TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                continue;
            const auto it = m_interp.find(id);
            if (it == m_interp.end() || it->second.count == 0)
                continue;

            InterpPose pose{};
            if (!sampleInterp(it->second, m_latestRecvTick, pose))
                continue;
            xf->position = Math::Vector3f(pose.px, pose.py, pose.pz);
            xf->rotation = Math::Quaternion(pose.qw, pose.qx, pose.qy, pose.qz);
            xf->rotation.Normalize();
        }
    }

} // namespace Dark
