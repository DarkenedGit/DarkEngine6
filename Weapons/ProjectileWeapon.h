#pragma once

#include "Weapons/Weapon.h"
#include "Audio/AudioSystem.h"
#include "Particles/ParticleEmitter.h"

#include <memory>
#include <vector>

namespace Dark
{

    inline ParticleEmitterDesc defaultProjectileImpactParticles()
    {
        ParticleEmitterDesc d{};
        d.name          = "Impact";
        d.maxParticles  = 64;
        d.emissionRate  = 0.0f;
        d.duration      = 0.0f;
        d.looping       = false;
        d.lifetime      = { 0.16f, 0.38f };
        d.startSpeed    = { 2.2f, 6.5f };
        d.startSize     = { 0.04f, 0.10f };
        d.endSize       = { 0.01f, 0.03f };
        d.startColor[0] = 1.00f;
        d.startColor[1] = 0.72f;
        d.startColor[2] = 0.22f;
        d.startColor[3] = 1.00f;
        d.endColor[0]   = 1.00f;
        d.endColor[1]   = 0.18f;
        d.endColor[2]   = 0.04f;
        d.endColor[3]   = 0.00f;
        d.gravity       = Math::Vector3f{ 0.0f, -9.0f, 0.0f };
        d.direction     = Math::Vector3f{ 0.0f, 1.0f, 0.0f };
        d.spreadDegrees = 72.0f;
        d.shape         = ParticleEmitterDesc::Shape::Point;
        d.additiveBlend = true;
        return d;
    }

    struct ProjectileWeaponDesc
    {
        const char* name     = "Rifle";
        float       damage   = 24.0f;
        float       cooldown = 0.40f;
        // Instant (hitscan) ignores speed and gravity. speed <= 0 also means instant.
        bool  instant  = false;
        float speed    = 42.0f; // metres / second when travelling over time
        float gravity  = 6.0f;  // downward m/s^2; 0 = flat flight
        float radius   = 0.10f;
        float maxRange = 90.0f;
        float maxLife  = 2.5f;
        uint32_t maxLive      = 16;
        uint32_t impactBurst  = 16;
        float    fireVolume   = 0.70f;
        float    hitVolume    = 0.80f;
        ParticleEmitterDesc impactParticles = defaultProjectileImpactParticles();
    };

    struct LiveProjectile
    {
        Math::Vector3f position{};
        Math::Vector3f prevPosition{};
        Math::Vector3f velocity{};
        float          age      = 0.0f;
        float          traveled = 0.0f;
        bool           alive    = false;
    };

    class ProjectileWeapon : public Weapon
    {
    public:
        ProjectileWeapon();
        explicit ProjectileWeapon(const ProjectileWeaponDesc& desc);

        ProjectileWeapon(const ProjectileWeapon&)            = delete;
        ProjectileWeapon& operator=(const ProjectileWeapon&) = delete;

        void                         setDesc(const ProjectileWeaponDesc& desc);
        const ProjectileWeaponDesc&  desc() const { return m_desc; }
        void                         setImpactDesc(const ParticleEmitterDesc& desc);

        void setAudio(Audio::AudioSystem* audio, std::shared_ptr<Audio::SoundClip> fireClip, std::shared_ptr<Audio::SoundClip> hitClip);

        WeaponKind  kind() const override { return WeaponKind::Projectile; }
        const char* name() const override { return m_desc.name ? m_desc.name : "Rifle"; }
        float       damage() const override { return m_desc.damage; }
        bool        canFire() const override { return m_cooldown <= 0.0f; }
        bool        isInstant() const { return m_desc.instant || m_desc.speed <= 0.0f; }
        float       speed() const { return m_desc.speed; }
        float       gravity() const { return m_desc.gravity; }

        bool fire(const WeaponFireRequest& req, const WeaponWorldQuery& world) override;
        void tick(float dt, const WeaponWorldQuery& world) override;
        void clear() override;

        const std::vector<LiveProjectile>& live() const { return m_shots; }
        ParticleEmitter&                   impactEmitter() { return m_impact; }
        const ParticleEmitter&             impactEmitter() const { return m_impact; }

    private:
        bool resolveHitscan(const Math::Vector3f& origin, const Math::Vector3f& dir, const WeaponWorldQuery& world);
        bool spawnShot(const Math::Vector3f& origin, const Math::Vector3f& velocity);
        void tickShot(LiveProjectile& shot, float dt, const WeaponWorldQuery& world);
        void applyHit(const WeaponHit& hit, const Math::Vector3f& fallbackDir);
        void playFire(const Math::Vector3f& origin);
        void playHit(const Math::Vector3f& point);

        ProjectileWeaponDesc                 m_desc{};
        std::vector<LiveProjectile>          m_shots;
        ParticleEmitter                      m_impact;
        Audio::AudioSystem*                  m_audio = nullptr;
        std::shared_ptr<Audio::SoundClip>    m_fireClip;
        std::shared_ptr<Audio::SoundClip>    m_hitClip;
    };

} // namespace Dark
