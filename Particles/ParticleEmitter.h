#pragma once

#include "Particles/ParticleTypes.h"
#include "Math/Vector3f.h"
#include "Math/Quaternion.h"

#include <cstdint>
#include <random>
#include <vector>

namespace Dark
{

    // CPU particle emitter: spawn + integrate. Rendering is separate (ParticleRenderer).
    class ParticleEmitter
    {
    public:
        ParticleEmitter() = default;

        void                       setDesc(const ParticleEmitterDesc& desc);
        const ParticleEmitterDesc& desc() const
        {
            return m_desc;
        }
        ParticleEmitterDesc& desc()
        {
            return m_desc;
        }

        void                  setTransform(const Math::Vector3f& position, const Math::Quaternion& rotation = Math::Quaternion::IDENTITY);
        const Math::Vector3f& position() const
        {
            return m_position;
        }
        const Math::Quaternion& rotation() const
        {
            return m_rotation;
        }

        void play();
        void stop(bool clearParticles = false);
        void restart();

        bool isPlaying() const
        {
            return m_playing;
        }

        // Advance simulation by dt seconds.
        void update(float dt);

        uint32_t aliveCount() const
        {
            return m_aliveCount;
        }
        uint32_t capacity() const
        {
            return static_cast<uint32_t>(m_particles.size());
        }

        const std::vector<Particle>& particles() const
        {
            return m_particles;
        }

        // Burst helper (editor / events)
        void emitBurst(uint32_t count);

    private:
        void           ensureCapacity();
        void           resetRibbonSeq();
        void           spawnOne();
        float          rand01();
        Math::Vector3f randomUnitDirectionInCone(const Math::Vector3f& axis, float spreadRad);

        ParticleEmitterDesc   m_desc{};
        std::vector<Particle> m_particles;

        Math::Vector3f   m_position{ 0, 0, 0 };
        Math::Quaternion m_rotation = Math::Quaternion::IDENTITY;

        bool     m_playing    = true;
        float    m_emitCarry  = 0.0f;
        float    m_age        = 0.0f;
        uint32_t m_aliveCount = 0;
        uint32_t m_nextRibbon = 0;
        uint32_t m_ribbonSeq[kMaxRibbonCount]{};

        std::mt19937 m_rng{ std::random_device{}() };
    };

} // namespace Dark
