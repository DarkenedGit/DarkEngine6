#pragma once

#include "Character/Health.h"
#include "Math/Matrix4f.h"
#include "Math/Vector3f.h"

namespace Dark
{

    struct HealthPack
    {
        Math::Vector3f pos{};
        Math::Matrix4f prevWorld{};
        bool           havePrevWorld = false;
        bool           active        = false;
        float          respawnIn     = 0.0f;
    };

    Math::Matrix4f healthPackWorldMatrix(const Math::Vector3f& pos, float spin, float bob);

    class HealthPackSet
    {
    public:
        static constexpr int   kMax     = 4;
        static constexpr float kPickupR = 1.2f;
        static constexpr float kHeal    = 50.0f;
        static constexpr float kRespawn = 16.0f;

        void clear();
        bool tryAdd(const Math::Vector3f& pos);

        void tick(float dt);
        int  tryPickup(const Math::Vector3f& playerPos, Health& health);

        int               count() const { return m_count; }
        float             spin() const { return m_spin; }
        float             bob() const { return m_bob; }
        HealthPack&       operator[](int i) { return m_packs[i]; }
        const HealthPack& operator[](int i) const { return m_packs[i]; }

    private:
        HealthPack m_packs[kMax]{};
        int        m_count = 0;
        float      m_spin  = 0.0f;
        float      m_bob   = 0.0f;
    };

} // namespace Dark
