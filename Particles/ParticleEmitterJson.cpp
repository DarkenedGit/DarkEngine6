#include "Particles/ParticleEmitterJson.h"

#include "Core/Json.h"

namespace Dark
{
    namespace
    {
        using json = nlohmann::json;

        float numberAt(const json& value, float fallback)
        {
            if (const auto* number = value.get_ptr<const json::number_float_t*>())
                return static_cast<float>(*number);
            if (const auto* number = value.get_ptr<const json::number_integer_t*>())
                return static_cast<float>(*number);
            if (const auto* number = value.get_ptr<const json::number_unsigned_t*>())
                return static_cast<float>(*number);
            return fallback;
        }

        json colorJson(const float color[4])
        {
            return json::array({ color[0], color[1], color[2], color[3] });
        }

        json vec3Json(const Math::Vector3f& value)
        {
            return json::array({ value.x, value.y, value.z });
        }

        void readColor4(const json& value, float out[4])
        {
            if (!value.is_array())
                return;
            const int count = static_cast<int>(value.size());
            for (int i = 0; i < 4 && i < count; ++i)
                out[i] = numberAt(value[i], out[i]);
        }

        void readVec3(const json& value, Math::Vector3f& out)
        {
            if (!value.is_array() || value.size() < 2)
                return;
            out.x = numberAt(value[0], out.x);
            out.y = numberAt(value[1], out.y);
            if (value.size() >= 3)
                out.z = numberAt(value[2], out.z);
        }

        void readRange(const json& value, FloatRange& out)
        {
            if (!value.is_array() || value.size() < 2)
                return;
            out.min = numberAt(value[0], out.min);
            out.max = numberAt(value[1], out.max);
        }

        int storedChildCollision(int collision)
        {
            if (collision == static_cast<int>(ParticleCollisionReaction::SpawnSecondary))
                return static_cast<int>(ParticleCollisionReaction::Stop);
            return collision;
        }

        ParticleCollisionReaction reactionFromInt(int value, ParticleCollisionReaction fallback)
        {
            switch (value)
            {
            case static_cast<int>(ParticleCollisionReaction::None):           return ParticleCollisionReaction::None;
            case static_cast<int>(ParticleCollisionReaction::Bounce):         return ParticleCollisionReaction::Bounce;
            case static_cast<int>(ParticleCollisionReaction::Stop):           return ParticleCollisionReaction::Stop;
            case static_cast<int>(ParticleCollisionReaction::SpawnSecondary): return ParticleCollisionReaction::SpawnSecondary;
            default:                                                          return fallback;
            }
        }

    } // namespace

    json particleToJson(const ParticleEmitterDesc& desc)
    {
        json particle;
        particle["name"]                 = desc.name;
        particle["maxParticles"]         = desc.maxParticles;
        particle["emissionRate"]         = desc.emissionRate;
        particle["duration"]             = desc.duration;
        particle["looping"]              = desc.looping;
        particle["lifetime"]             = json::array({ desc.lifetime.min, desc.lifetime.max });
        particle["startSpeed"]           = json::array({ desc.startSpeed.min, desc.startSpeed.max });
        particle["startSize"]            = json::array({ desc.startSize.min, desc.startSize.max });
        particle["endSize"]              = json::array({ desc.endSize.min, desc.endSize.max });
        particle["startColor"]           = colorJson(desc.startColor);
        particle["endColor"]             = colorJson(desc.endColor);
        particle["gravity"]              = vec3Json(desc.gravity);
        particle["direction"]            = vec3Json(desc.direction);
        particle["spreadDegrees"]        = desc.spreadDegrees;
        particle["shape"]                = static_cast<int>(desc.shape);
        particle["shapeSize"]            = vec3Json(desc.shapeSize);
        particle["additiveBlend"]        = desc.additiveBlend;
        particle["simulationSpeed"]      = desc.simulationSpeed;
        particle["renderMode"]           = static_cast<int>(desc.renderMode);
        particle["ribbonCount"]          = desc.ribbonCount;
        particle["ribbonUvScale"]        = desc.ribbonUvScale;
        particle["simMode"]              = static_cast<int>(desc.simMode);
        particle["collision"]            = static_cast<int>(desc.collision);
        particle["restitution"]          = desc.restitution;
        particle["friction"]             = desc.friction;
        particle["collisionThickness"]   = desc.collisionThickness;
        particle["collisionBias"]        = desc.collisionBias;
        particle["restLifetime"]         = desc.restLifetime;
        particle["maxSecondaryPerFrame"] = desc.maxSecondaryPerFrame;
        if (desc.hasSecondary)
        {
            json secondary;
            secondary["maxParticles"]  = desc.secondary.maxParticles;
            secondary["lifetime"]      = json::array({ desc.secondary.lifetime.min, desc.secondary.lifetime.max });
            secondary["startSize"]     = json::array({ desc.secondary.startSize.min, desc.secondary.startSize.max });
            secondary["restLifetime"]  = desc.secondary.restLifetime;
            secondary["additiveBlend"] = desc.secondary.additiveBlend;
            secondary["collision"]     = storedChildCollision(static_cast<int>(desc.secondary.collision));
            particle["secondary"]      = std::move(secondary);
        }
        return particle;
    }

    void particleFromJson(const json& particle, ParticleEmitterDesc& desc, const char* where)
    {
        if (!particle.is_object())
            return;
        const char* site = where ? where : "Particle";
        jsonStringOr(particle, "name", desc.name, desc.name, site);
        int maxParticles = static_cast<int>(desc.maxParticles);
        if (jsonIntOr(particle, "maxParticles", maxParticles, maxParticles, site) && maxParticles > 0)
            desc.maxParticles = static_cast<uint32_t>(maxParticles);
        jsonFloatOr(particle, "emissionRate", desc.emissionRate, desc.emissionRate, site);
        jsonFloatOr(particle, "duration", desc.duration, desc.duration, site);
        jsonBoolOr(particle, "looping", desc.looping, desc.looping, site);
        jsonFloatOr(particle, "spreadDegrees", desc.spreadDegrees, desc.spreadDegrees, site);
        int shape = static_cast<int>(desc.shape);
        if (jsonIntOr(particle, "shape", shape, shape, site))
            desc.shape = static_cast<ParticleEmitterDesc::Shape>(shape);
        jsonBoolOr(particle, "additiveBlend", desc.additiveBlend, desc.additiveBlend, site);
        jsonFloatOr(particle, "simulationSpeed", desc.simulationSpeed, desc.simulationSpeed, site);
        int renderMode = static_cast<int>(desc.renderMode);
        if (jsonIntOr(particle, "renderMode", renderMode, renderMode, site))
            desc.renderMode = static_cast<ParticleEmitterDesc::RenderMode>(renderMode);
        int ribbonCount = static_cast<int>(desc.ribbonCount);
        if (jsonIntOr(particle, "ribbonCount", ribbonCount, ribbonCount, site) && ribbonCount > 0)
            desc.ribbonCount = static_cast<uint32_t>(ribbonCount);
        jsonFloatOr(particle, "ribbonUvScale", desc.ribbonUvScale, desc.ribbonUvScale, site);
        int simMode = static_cast<int>(desc.simMode);
        if (jsonIntOr(particle, "simMode", simMode, simMode, site))
            desc.simMode = simMode == static_cast<int>(ParticleSimMode::Gpu) ? ParticleSimMode::Gpu : ParticleSimMode::Cpu;
        int collision = static_cast<int>(desc.collision);
        if (jsonIntOr(particle, "collision", collision, collision, site))
            desc.collision = reactionFromInt(collision, ParticleCollisionReaction::None);
        jsonFloatOr(particle, "restitution", desc.restitution, desc.restitution, site);
        jsonFloatOr(particle, "friction", desc.friction, desc.friction, site);
        jsonFloatOr(particle, "collisionThickness", desc.collisionThickness, desc.collisionThickness, site);
        jsonFloatOr(particle, "collisionBias", desc.collisionBias, desc.collisionBias, site);
        jsonFloatOr(particle, "restLifetime", desc.restLifetime, desc.restLifetime, site);
        int secondaryCap = static_cast<int>(desc.maxSecondaryPerFrame);
        if (jsonIntOr(particle, "maxSecondaryPerFrame", secondaryCap, secondaryCap, site) && secondaryCap >= 0)
            desc.maxSecondaryPerFrame = static_cast<uint32_t>(secondaryCap);
        if (particle.contains("lifetime"))
            readRange(particle["lifetime"], desc.lifetime);
        if (particle.contains("startSpeed"))
            readRange(particle["startSpeed"], desc.startSpeed);
        if (particle.contains("startSize"))
            readRange(particle["startSize"], desc.startSize);
        if (particle.contains("endSize"))
            readRange(particle["endSize"], desc.endSize);
        if (particle.contains("startColor"))
            readColor4(particle["startColor"], desc.startColor);
        if (particle.contains("endColor"))
            readColor4(particle["endColor"], desc.endColor);
        if (particle.contains("gravity"))
            readVec3(particle["gravity"], desc.gravity);
        if (particle.contains("direction"))
            readVec3(particle["direction"], desc.direction);
        if (particle.contains("shapeSize"))
            readVec3(particle["shapeSize"], desc.shapeSize);
        if (particle.contains("secondary") && particle["secondary"].is_object())
        {
            const json& secondary = particle["secondary"];
            desc.hasSecondary     = true;
            int secondaryMax      = static_cast<int>(desc.secondary.maxParticles);
            if (jsonIntOr(secondary, "maxParticles", secondaryMax, secondaryMax, site) && secondaryMax > 0)
                desc.secondary.maxParticles = static_cast<uint32_t>(secondaryMax);
            jsonFloatOr(secondary, "restLifetime", desc.secondary.restLifetime, desc.secondary.restLifetime, site);
            jsonBoolOr(secondary, "additiveBlend", desc.secondary.additiveBlend, desc.secondary.additiveBlend, site);
            int secondaryCollision = static_cast<int>(desc.secondary.collision);
            if (jsonIntOr(secondary, "collision", secondaryCollision, secondaryCollision, site))
                desc.secondary.collision = reactionFromInt(storedChildCollision(secondaryCollision), ParticleCollisionReaction::Stop);
            if (secondary.contains("lifetime"))
                readRange(secondary["lifetime"], desc.secondary.lifetime);
            if (secondary.contains("startSize"))
                readRange(secondary["startSize"], desc.secondary.startSize);
        }
    }

} // namespace Dark
