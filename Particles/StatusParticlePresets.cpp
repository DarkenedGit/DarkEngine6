#include "Particles/StatusParticlePresets.h"

namespace Dark
{
    using Combat::StatusParticlePreset;

    namespace
    {
        void setRgba(float* c, float r, float g, float b, float a)
        {
            c[0] = r;
            c[1] = g;
            c[2] = b;
            c[3] = a;
        }

        ParticleEmitterDesc loopingBase(const char* name, float rate)
        {
            ParticleEmitterDesc d{};
            d.name          = name;
            d.maxParticles  = 96;
            d.emissionRate  = rate;
            d.duration      = 0.0f;
            d.looping       = true;
            d.direction     = Math::Vector3f{ 0.0f, 1.0f, 0.0f };
            d.renderMode    = ParticleEmitterDesc::RenderMode::Billboard;
            d.additiveBlend = true;
            return d;
        }
    } // namespace

    ParticleEmitterDesc statusParticlePreset(StatusParticlePreset preset)
    {
        switch (preset)
        {
        case StatusParticlePreset::StunStars:
        {
            ParticleEmitterDesc d = loopingBase("StunStars", 12.0f);
            d.lifetime            = { 0.40f, 0.80f };
            d.startSpeed          = { 0.35f, 1.10f };
            d.startSize           = { 0.06f, 0.12f };
            d.endSize             = { 0.02f, 0.05f };
            setRgba(d.startColor, 1.00f, 0.95f, 0.45f, 1.00f);
            setRgba(d.endColor, 1.00f, 0.95f, 0.45f, 0.00f);
            d.gravity       = Math::Vector3f{ 0.0f, 0.0f, 0.0f };
            d.spreadDegrees = 80.0f;
            d.shape         = ParticleEmitterDesc::Shape::Point;
            d.additiveBlend = true;
            return d;
        }
        case StatusParticlePreset::PoisonCloud:
        {
            ParticleEmitterDesc d = loopingBase("PoisonCloud", 18.0f);
            d.lifetime            = { 0.80f, 1.60f };
            d.startSpeed          = { 0.15f, 0.50f };
            d.startSize           = { 0.22f, 0.40f };
            d.endSize             = { 0.30f, 0.55f };
            setRgba(d.startColor, 0.25f, 0.85f, 0.18f, 0.85f);
            setRgba(d.endColor, 0.12f, 0.45f, 0.08f, 0.00f);
            d.gravity       = Math::Vector3f{ 0.0f, 0.4f, 0.0f };
            d.spreadDegrees = 40.0f;
            d.shape         = ParticleEmitterDesc::Shape::Sphere;
            d.shapeSize     = Math::Vector3f{ 0.35f, 0.0f, 0.0f };
            d.additiveBlend = true;
            return d;
        }
        case StatusParticlePreset::BleedDrip:
        {
            ParticleEmitterDesc d = loopingBase("BleedDrip", 8.0f);
            d.lifetime            = { 0.22f, 0.50f };
            d.startSpeed          = { 1.80f, 4.20f };
            d.startSize           = { 0.07f, 0.14f };
            d.endSize             = { 0.02f, 0.05f };
            setRgba(d.startColor, 0.72f, 0.04f, 0.06f, 0.95f);
            setRgba(d.endColor, 0.28f, 0.00f, 0.01f, 0.00f);
            d.gravity       = Math::Vector3f{ 0.0f, -11.0f, 0.0f };
            d.spreadDegrees = 75.0f;
            d.shape         = ParticleEmitterDesc::Shape::Sphere;
            d.shapeSize     = Math::Vector3f{ 0.12f, 0.0f, 0.0f };
            d.additiveBlend = false;
            return d;
        }
        case StatusParticlePreset::IgniteFlames:
        {
            ParticleEmitterDesc d = loopingBase("IgniteFlames", 16.0f);
            d.lifetime            = { 0.28f, 0.65f };
            d.startSpeed          = { 0.60f, 1.80f };
            d.startSize           = { 0.10f, 0.22f };
            d.endSize             = { 0.02f, 0.08f };
            setRgba(d.startColor, 1.00f, 0.35f, 0.05f, 1.00f);
            setRgba(d.endColor, 1.00f, 0.12f, 0.02f, 0.00f);
            d.gravity       = Math::Vector3f{ 0.0f, 1.2f, 0.0f };
            d.spreadDegrees = 35.0f;
            d.shape         = ParticleEmitterDesc::Shape::Sphere;
            d.shapeSize     = Math::Vector3f{ 0.30f, 0.0f, 0.0f };
            d.additiveBlend = true;
            return d;
        }
        case StatusParticlePreset::ChillMist:
        {
            ParticleEmitterDesc d = loopingBase("ChillMist", 14.0f);
            d.lifetime            = { 0.90f, 1.60f };
            d.startSpeed          = { 0.12f, 0.40f };
            d.startSize           = { 0.25f, 0.45f };
            d.endSize             = { 0.35f, 0.60f };
            setRgba(d.startColor, 0.55f, 0.80f, 1.00f, 0.80f);
            setRgba(d.endColor, 0.35f, 0.55f, 0.90f, 0.00f);
            d.gravity       = Math::Vector3f{ 0.0f, 0.15f, 0.0f };
            d.spreadDegrees = 50.0f;
            d.shape         = ParticleEmitterDesc::Shape::Sphere;
            d.shapeSize     = Math::Vector3f{ 0.40f, 0.0f, 0.0f };
            d.additiveBlend = true;
            return d;
        }
        case StatusParticlePreset::ShockSparks:
        {
            ParticleEmitterDesc d = loopingBase("ShockSparks", 20.0f);
            d.lifetime            = { 0.12f, 0.28f };
            d.startSpeed          = { 2.50f, 6.00f };
            d.startSize           = { 0.04f, 0.08f };
            d.endSize             = { 0.01f, 0.03f };
            setRgba(d.startColor, 0.55f, 0.75f, 1.00f, 1.00f);
            setRgba(d.endColor, 0.35f, 0.55f, 1.00f, 0.00f);
            d.gravity       = Math::Vector3f{ 0.0f, 0.0f, 0.0f };
            d.spreadDegrees = 110.0f;
            d.shape         = ParticleEmitterDesc::Shape::Point;
            d.additiveBlend = true;
            return d;
        }
        case StatusParticlePreset::None:
        default:
            return ParticleEmitterDesc{};
        }
    }

} // namespace Dark
