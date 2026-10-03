#pragma once

#include "ECS/World.h"
#include "Math/Matrix4f.h"
#include "Render/DecalTypes.h"
#include "Render/Frustum3f.h"

#include <cstdint>

namespace Dark
{

    class DecalPool
    {
    public:
        static constexpr uint32_t kCapacity = 256;

        DecalPool();

        void reset();
        // False only when kind is out of range or axisY is degenerate.
        // At capacity, recycles the oldest serial and returns true.
        bool spawn(const DecalSpawnDesc& desc, DecalId* outId);
        void clear();
        void tick(World& world, float dt);

        // Oldest serial first. Inside boxes skip the frustum test.
        uint32_t buildVisible(const Frustum3f& frustum, const Math::Vector3f& cameraPos, DecalGpuInstance* outside, uint32_t outsideCap, uint32_t* outsideCount, DecalGpuInstance* inside,
                              uint32_t insideCap, uint32_t* insideCount) const;

        uint32_t aliveCount() const
        {
            return m_alive;
        }
        uint32_t recycleCount() const
        {
            return m_recycleCount;
        }
        bool alive(DecalId id) const;

        // Drives the wrap-compact path. 0 is stored as 1 because serial 0 is invalid.
        void setNextSerialForTest(uint32_t serial);

    private:
        static constexpr uint16_t kNoBone = 0xFFFF;

        struct Slot
        {
            DecalDefId     defId = DecalDefId::Footmark;
            DecalSpace     space = DecalSpace::World;
            Entity         entity{};
            uint16_t       bone = kNoBone;
            Math::Matrix4f localOffset{};
            Math::Matrix4f world{};
            Math::Vector3f halfExtents{ 1.0f, 1.0f, 1.0f };
            Math::Vector3f spawnPos{ 0.0f, 0.0f, 0.0f };
            float          age               = 0.0f;
            float          lifetime          = 1.0f;
            uint32_t       serial            = 0;
            bool           alive             = false;
            bool           attachmentSampled = false;
            bool           healthWasAlive    = true;
            bool           hasLastPos        = false;
            Math::Vector3f lastEntityPos{ 0.0f, 0.0f, 0.0f };
        };

        void     freeSlot(uint32_t index);
        uint32_t allocateSerial();
        void     compactSerials();
        uint32_t findOldest() const;

        Slot     m_slots[kCapacity]{};
        uint32_t m_free[kCapacity]{};
        uint32_t m_freeCount     = 0;
        uint32_t m_alive         = 0;
        uint32_t m_nextSerial    = 1;
        uint32_t m_recycleCount  = 0;
        bool     m_warnedRecycle = false;
        bool     m_warnedWrap    = false;
    };

} // namespace Dark
