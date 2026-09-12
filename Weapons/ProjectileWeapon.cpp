#include "Weapons/ProjectileWeapon.h"

#include "Math/MathDefines.h"
#include "Math/MathHelper.h"

namespace Dark
{
    using Math::Vector3f;

    namespace
    {
        Vector3f normalizeOr(const Vector3f& v, const Vector3f& fallback)
        {
            if (v.MagnitudeSqrd() < 1.0e-8f)
                return fallback;
            Vector3f n = v;
            n.Normalize();
            return n;
        }
    } // namespace

    ProjectileWeapon::ProjectileWeapon()
        : ProjectileWeapon(ProjectileWeaponDesc{})
    {
    }

    ProjectileWeapon::ProjectileWeapon(const ProjectileWeaponDesc& desc)
    {
        setDesc(desc);
    }

    void ProjectileWeapon::setDesc(const ProjectileWeaponDesc& desc)
    {
        m_desc = desc;
        if (m_desc.maxLive < 1)
            m_desc.maxLive = 1;
        if (m_desc.radius < 0.01f)
            m_desc.radius = 0.01f;
        if (m_desc.maxRange < 1.0f)
            m_desc.maxRange = 1.0f;
        if (m_desc.recoilPitchDeg < 0.0f)
            m_desc.recoilPitchDeg = 0.0f;
        if (m_desc.recoilYawDeg < 0.0f)
            m_desc.recoilYawDeg = 0.0f;
        m_pendingRecoil = {};
        m_shots.resize(m_desc.maxLive);
        for (LiveProjectile& s : m_shots)
            s.alive = false;
        m_impact.setDesc(m_desc.impactParticles);
        m_impact.stop(true);
    }

    void ProjectileWeapon::setImpactDesc(const ParticleEmitterDesc& desc)
    {
        m_desc.impactParticles = desc;
        m_impact.setDesc(desc);
        m_impact.stop(false);
    }

    void ProjectileWeapon::setAudio(Audio::AudioSystem* audio, std::shared_ptr<Audio::SoundClip> fireClip, std::shared_ptr<Audio::SoundClip> hitClip)
    {
        m_audio    = audio;
        m_fireClip = std::move(fireClip);
        m_hitClip  = std::move(hitClip);
    }

    void ProjectileWeapon::playFire(const Vector3f& origin)
    {
        if (!m_audio || !m_fireClip)
            return;
        m_audio->play3D(m_fireClip, origin, m_desc.fireVolume);
    }

    void ProjectileWeapon::playHit(const Vector3f& point)
    {
        if (!m_audio || !m_hitClip)
            return;
        m_audio->play3D(m_hitClip, point, m_desc.hitVolume);
    }

    void ProjectileWeapon::applyHit(const WeaponHit& hit, const Vector3f& fallbackDir)
    {
        WeaponHit out = hit;
        out.damage    = m_desc.damage;
        out.weapon    = WeaponKind::Projectile;
        out.direction = normalizeOr(fallbackDir, Vector3f{ 0.0f, 0.0f, 1.0f });
        if (out.normal.MagnitudeSqrd() < 1.0e-8f)
            out.normal = normalizeOr(-fallbackDir, Vector3f{ 0.0f, 1.0f, 0.0f });

        m_impact.desc().direction = out.normal;
        m_impact.setTransform(out.point);
        m_impact.emitBurst(m_desc.impactBurst);
        playHit(out.point);
        emitHit(out);
    }

    bool ProjectileWeapon::resolveHitscan(const Vector3f& origin, const Vector3f& dir, const WeaponWorldQuery& world)
    {
        const float range = m_desc.maxRange > 0.0f ? m_desc.maxRange : world.maxRange;
        WeaponHit   hit{};
        if (!weaponRaycastClosest(world, Math::Ray3f{ origin, dir }, range, hit))
            return false;
        applyHit(hit, dir);
        return true;
    }

    bool ProjectileWeapon::spawnShot(const Vector3f& origin, const Vector3f& velocity)
    {
        int free = -1;
        int oldest = 0;
        float oldestAge = -1.0f;
        for (int i = 0; i < static_cast<int>(m_shots.size()); ++i)
        {
            LiveProjectile& s = m_shots[static_cast<size_t>(i)];
            if (!s.alive)
            {
                free = i;
                break;
            }
            if (s.age > oldestAge)
            {
                oldestAge = s.age;
                oldest    = i;
            }
        }
        const int idx = free >= 0 ? free : oldest;
        LiveProjectile& s = m_shots[static_cast<size_t>(idx)];
        s.position     = origin;
        s.prevPosition = origin;
        s.velocity     = velocity;
        s.age          = 0.0f;
        s.traveled     = 0.0f;
        s.alive        = true;
        return true;
    }

    bool ProjectileWeapon::fire(const WeaponFireRequest& req, const WeaponWorldQuery& world)
    {
        if (m_cooldown > 0.0f)
            return false;
        const Vector3f dir = normalizeOr(req.direction, Vector3f{ 0.0f, 0.0f, 1.0f });
        m_cooldown         = m_desc.cooldown > 0.0f ? m_desc.cooldown : 0.0f;
        playFire(req.origin);
        punchRecoil();

        if (isInstant())
        {
            resolveHitscan(req.origin, dir, world);
            return true;
        }

        spawnShot(req.origin, dir * m_desc.speed);
        return true;
    }

    void ProjectileWeapon::setRecoilDegrees(float pitchDeg, float yawDeg)
    {
        m_desc.recoilPitchDeg = pitchDeg < 0.0f ? 0.0f : pitchDeg;
        m_desc.recoilYawDeg   = yawDeg < 0.0f ? 0.0f : yawDeg;
    }

    RecoilKick ProjectileWeapon::takeRecoil()
    {
        const RecoilKick k = m_pendingRecoil;
        m_pendingRecoil    = {};
        return k;
    }

    void ProjectileWeapon::punchRecoil()
    {
        m_pendingRecoil.pitch = m_desc.recoilPitchDeg * Math::DegToRad;
        if (m_desc.recoilYawDeg > 0.0f)
            m_pendingRecoil.yaw = Math::RandF(-m_desc.recoilYawDeg, m_desc.recoilYawDeg) * Math::DegToRad;
        else
            m_pendingRecoil.yaw = 0.0f;
    }

    void ProjectileWeapon::tickShot(LiveProjectile& shot, float dt, const WeaponWorldQuery& world)
    {
        if (!shot.alive || dt <= 0.0f)
            return;

        shot.prevPosition = shot.position;
        shot.velocity.y -= m_desc.gravity * dt;
        const Vector3f delta = shot.velocity * dt;

        WeaponHit hit{};
        if (weaponSweepClosest(world, shot.position, delta, m_desc.radius, hit))
        {
            shot.alive = false;
            applyHit(hit, shot.velocity);
            return;
        }

        shot.position += delta;
        shot.age += dt;
        shot.traveled += delta.Magnitude();
        if (shot.age >= m_desc.maxLife || shot.traveled >= m_desc.maxRange)
            shot.alive = false;
    }

    void ProjectileWeapon::tick(float dt, const WeaponWorldQuery& world)
    {
        if (m_cooldown > 0.0f)
            m_cooldown -= dt;
        for (LiveProjectile& s : m_shots)
        {
            if (s.alive)
                tickShot(s, dt, world);
        }
        m_impact.update(dt);
    }

    void ProjectileWeapon::clear()
    {
        m_cooldown = 0.0f;
        m_pendingRecoil = {};
        for (LiveProjectile& s : m_shots)
            s.alive = false;
        m_impact.stop(true);
    }

} // namespace Dark
