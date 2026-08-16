#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleRibbon.h"
#include "Math/MathDefines.h"

#include <cmath>
#include <algorithm>

namespace Dark
{
    using namespace Math;

    namespace
    {

        Vector3f rotateVec(const Quaternion& q, const Vector3f& v)
        {
            return q.Rotate(v);
        }

        Vector3f normalizeSafe(const Vector3f& v, const Vector3f& fallback)
        {
            const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
            if (lenSq < 1e-10f)
                return fallback;
            const float inv = 1.0f / std::sqrt(lenSq);
            return Vector3f(v.x * inv, v.y * inv, v.z * inv);
        }

    } // namespace

    void ParticleEmitter::setDesc(const ParticleEmitterDesc& desc)
    {
        m_desc = desc;
        if (m_desc.maxParticles < 1)
            m_desc.maxParticles = 1;
        if (m_desc.maxParticles > 100000)
            m_desc.maxParticles = 100000;
        m_desc.ribbonCount = clampRibbonCount(m_desc.ribbonCount);
        if (m_desc.ribbonUvScale <= 0.0f)
            m_desc.ribbonUvScale = 1.0f;
        ensureCapacity();
    }

    void ParticleEmitter::setTransform(const Vector3f& position, const Quaternion& rotation)
    {
        m_position = position;
        m_rotation = rotation;
    }

    void ParticleEmitter::play()
    {
        m_playing = true;
    }

    void ParticleEmitter::stop(bool clearParticles)
    {
        m_playing   = false;
        m_emitCarry = 0.0f;
        if (clearParticles)
        {
            for (Particle& p : m_particles)
                p.alive = false;
            m_aliveCount = 0;
            resetRibbonSeq();
        }
    }

    void ParticleEmitter::restart()
    {
        stop(true);
        m_age = 0.0f;
        play();
        if (m_desc.prewarm && m_desc.lifetime.max > 0.0f)
        {
            // Rough prewarm: simulate a few seconds
            const float step = 1.0f / 30.0f;
            float       t    = 0.0f;
            const float pre  = m_desc.lifetime.max;
            while (t < pre)
            {
                update(step);
                t += step;
            }
        }
    }

    void ParticleEmitter::ensureCapacity()
    {
        if (m_particles.size() == m_desc.maxParticles)
            return;
        m_particles.assign(m_desc.maxParticles, Particle{});
        m_aliveCount = 0;
        m_emitCarry  = 0.0f;
        resetRibbonSeq();
    }

    void ParticleEmitter::resetRibbonSeq()
    {
        m_nextRibbon = 0;
        for (uint32_t i = 0; i < kMaxRibbonCount; ++i)
            m_ribbonSeq[i] = 0;
    }

    float ParticleEmitter::rand01()
    {
        return std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);
    }

    Vector3f ParticleEmitter::randomUnitDirectionInCone(const Vector3f& axisIn, float spreadRad)
    {
        Vector3f axis = normalizeSafe(axisIn, Vector3f::Y_AXIS);
        if (spreadRad <= 1e-5f)
            return axis;

        // Random direction in cone around axis
        const float cosMax   = std::cos(spreadRad);
        const float u        = rand01();
        const float cosTheta = cosMax + (1.0f - cosMax) * u;
        const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
        const float phi      = rand01() * 6.28318530718f;

        // Build orthonormal basis
        Vector3f helper  = (std::fabs(axis.y) < 0.99f) ? Vector3f::Y_AXIS : Vector3f::X_AXIS;
        Vector3f tangent = normalizeSafe(Vector3f(axis.y * helper.z - axis.z * helper.y, axis.z * helper.x - axis.x * helper.z, axis.x * helper.y - axis.y * helper.x), Vector3f::X_AXIS);
        Vector3f bitangent(axis.y * tangent.z - axis.z * tangent.y, axis.z * tangent.x - axis.x * tangent.z, axis.x * tangent.y - axis.y * tangent.x);

        return normalizeSafe(Vector3f(axis.x * cosTheta + tangent.x * (sinTheta * std::cos(phi)) + bitangent.x * (sinTheta * std::sin(phi)),
                                      axis.y * cosTheta + tangent.y * (sinTheta * std::cos(phi)) + bitangent.y * (sinTheta * std::sin(phi)),
                                      axis.z * cosTheta + tangent.z * (sinTheta * std::cos(phi)) + bitangent.z * (sinTheta * std::sin(phi))),
                             axis);
    }

    void ParticleEmitter::spawnOne()
    {
        // Find free slot
        Particle* slot = nullptr;
        for (Particle& p : m_particles)
        {
            if (!p.alive)
            {
                slot = &p;
                break;
            }
        }
        if (!slot)
            return; // pool full

        Particle& p = *slot;
        p.alive     = true;
        p.maxLife   = m_desc.lifetime.sample(rand01());
        if (p.maxLife < 0.05f)
            p.maxLife = 0.05f;
        p.life = p.maxLife;

        // Spawn position
        Vector3f localPos(0, 0, 0);
        switch (m_desc.shape)
        {
        case ParticleEmitterDesc::Shape::Box:
            localPos = Vector3f((rand01() * 2.0f - 1.0f) * m_desc.shapeSize.x, (rand01() * 2.0f - 1.0f) * m_desc.shapeSize.y, (rand01() * 2.0f - 1.0f) * m_desc.shapeSize.z);
            break;
        case ParticleEmitterDesc::Shape::Sphere:
        {
            const float r  = m_desc.shapeSize.x * std::cbrt(rand01());
            const float th = rand01() * 6.28318530718f;
            const float z  = rand01() * 2.0f - 1.0f;
            const float s  = std::sqrt(std::max(0.0f, 1.0f - z * z));
            localPos       = Vector3f(r * s * std::cos(th), r * s * std::sin(th), r * z);
            break;
        }
        case ParticleEmitterDesc::Shape::Point:
        default:
            break;
        }

        p.position = m_position + rotateVec(m_rotation, localPos);

        Vector3f localDir = normalizeSafe(m_desc.direction, Vector3f::Y_AXIS);
        localDir          = randomUnitDirectionInCone(localDir, m_desc.spreadDegrees * (3.14159265f / 180.0f));
        const float speed = m_desc.startSpeed.sample(rand01());
        p.velocity        = rotateVec(m_rotation, localDir) * speed;

        p.size0 = m_desc.startSize.sample(rand01());
        p.size1 = m_desc.endSize.sample(rand01());
        for (int i = 0; i < 4; ++i)
        {
            p.color0[i] = m_desc.startColor[i];
            p.color1[i] = m_desc.endColor[i];
        }
        p.rotation = rand01() * 6.28318530718f;

        const uint32_t ribbons = clampRibbonCount(m_desc.ribbonCount);
        p.ribbonId             = m_nextRibbon % ribbons;
        p.seq                  = m_ribbonSeq[p.ribbonId]++;
        m_nextRibbon           = (m_nextRibbon + 1u) % ribbons;

        ++m_aliveCount;
    }

    void ParticleEmitter::emitBurst(uint32_t count)
    {
        ensureCapacity();
        for (uint32_t i = 0; i < count; ++i)
            spawnOne();
    }

    void ParticleEmitter::update(float dt)
    {
        ensureCapacity();
        if (dt <= 0.0f)
            return;

        dt *= m_desc.simulationSpeed;
        if (dt <= 0.0f)
            return;

        if (m_playing)
        {
            m_age += dt;
            const bool expired = (m_desc.duration > 0.0f && m_age >= m_desc.duration);
            if (expired && m_desc.looping)
            {
                m_age = 0.0f;
            }
            else if (expired && !m_desc.looping)
            {
                m_playing = false;
            }

            if (m_playing && m_desc.emissionRate > 0.0f)
            {
                m_emitCarry += m_desc.emissionRate * dt;
                while (m_emitCarry >= 1.0f)
                {
                    spawnOne();
                    m_emitCarry -= 1.0f;
                }
            }
        }

        m_aliveCount = 0;
        for (Particle& p : m_particles)
        {
            if (!p.alive)
                continue;

            p.life -= dt;
            if (p.life <= 0.0f)
            {
                p.alive = false;
                continue;
            }

            p.velocity.x += m_desc.gravity.x * dt;
            p.velocity.y += m_desc.gravity.y * dt;
            p.velocity.z += m_desc.gravity.z * dt;
            p.position.x += p.velocity.x * dt;
            p.position.y += p.velocity.y * dt;
            p.position.z += p.velocity.z * dt;
            ++m_aliveCount;
        }
    }

} // namespace Dark
