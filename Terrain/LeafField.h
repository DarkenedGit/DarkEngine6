#pragma once

#include <cstdint>

namespace Dark::Terrain
{
    enum class LeafPhase : uint8_t
    {
        Free = 0,
        Airborne,
        Rest,
        Fade
    };

    struct LeafSlot
    {
        LeafPhase phase     = LeafPhase::Free;
        uint32_t  crownId   = 0;
        uint32_t  serial    = 0;
        float     x         = 0.0f;
        float     y         = 0.0f;
        float     z         = 0.0f;
        float     vx        = 0.0f;
        float     vy        = 0.0f;
        float     vz        = 0.0f;
        float     nx        = 0.0f;
        float     ny        = 1.0f;
        float     nz        = 0.0f;
        float     phaseTime = 0.0f;
    };

    // Crowns and surfaces are supplied by the host. The field does not query foliage or the GPU.
    class LeafField
    {
    public:
        static constexpr int   kPoolSize           = 256;
        static constexpr int   kMaxSpawnsPerStep   = 8;
        static constexpr float kGatherRadiusM      = 96.0f;
        static constexpr float kRestSeconds        = 1.5f;
        static constexpr float kFadeSeconds        = 1.0f;
        static constexpr float kFlightCapSeconds   = 8.0f;

        struct Crown
        {
            uint32_t id     = 0;
            float    x      = 0.0f;
            float    seatY  = 0.0f;
            float    z      = 0.0f;
            float    crownY = 0.0f;
        };

        struct Pond
        {
            float minX  = 0.0f;
            float minZ  = 0.0f;
            float maxX  = 0.0f;
            float maxZ  = 0.0f;
            float level = 0.0f;
        };

        struct Step
        {
            float    dt        = 0.0f;
            double   simTime   = 0.0;
            float    listenerX = 0.0f;
            float    listenerY = 0.0f;
            float    listenerZ = 0.0f;
            float    cameraX   = 0.0f;
            float    cameraZ   = 0.0f;
            const Crown* crowns     = nullptr;
            int          crownCount = 0;
            // Null accepts every crown inside the gather disk. A false result is outside the frustum.
            bool (*inFrustum)(float x, float y, float z, void* user) = nullptr;
            void* frustumUser = nullptr;
            // Null samples the leaf-wind field. Tests inject calm and strong winds.
            void* windUser = nullptr;
            void (*wind)(float x, float z, double simTime, float& dirX, float& dirZ, float& strength, void* user) = nullptr;
            bool (*tryHeight)(float x, float z, float& bedY, void* user) = nullptr;
            void* heightUser = nullptr;
            bool        lake      = false;
            float       lakeLevel = 0.0f;
            const Pond* ponds     = nullptr;
            int         pondCount = 0;
            float       cellMetres = 1.0f;
        };

        LeafField();

        void  clear();
        void  setShedScale(float scale);
        float shedScale() const { return m_shedScale; }

        void step(const Step& in);

        int      livingCount() const { return m_living; }
        uint32_t spawnedTotal() const { return m_spawnedTotal; }
        bool     copySlot(int index, LeafSlot& out) const;

    private:
        static constexpr int kMaxCrowns = 1024;

        struct SimLeaf
        {
            LeafPhase phase      = LeafPhase::Free;
            uint32_t  crownId    = 0;
            uint32_t  serial     = 0;
            float     x          = 0.0f;
            float     y          = 0.0f;
            float     z          = 0.0f;
            float     vx         = 0.0f;
            float     vy         = 0.0f;
            float     vz         = 0.0f;
            float     nx         = 0.0f;
            float     ny         = 1.0f;
            float     nz         = 0.0f;
            float     phaseTime  = 0.0f;
            float     flightTime = 0.0f;
        };

        struct CrownRun
        {
            uint32_t id      = 0;
            float    x       = 0.0f;
            float    z       = 0.0f;
            float    crownY  = 0.0f;
            float    carry   = 0.0f;
            int      living  = 0;
            bool     used    = false;
        };

        void resetPool();
        int  findOrAddCrown(const Crown& crown);
        void freeSlot(int index);
        int  oldestRestSlot(uint32_t crownId) const;
        bool spawnLeaf(CrownRun& crown);
        void sampleWind(const Step& in, float x, float z, float& dirX, float& dirZ, float& strength) const;
        static float restHeight(const Step& in, float x, float z, float bedY);

        float    m_shedScale    = 1.0f;
        int      m_living       = 0;
        uint32_t m_spawnedTotal = 0;
        uint32_t m_nextSerial   = 1;
        int      m_cursor       = 0;
        int      m_freeCount    = 0;
        int      m_free[kPoolSize]{};
        SimLeaf  m_slots[kPoolSize]{};
        CrownRun m_crowns[kMaxCrowns]{};
    };

} // namespace Dark::Terrain
