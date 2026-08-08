#pragma once

#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    // Serializable prop kind used by the level editor.
    enum class SceneObjectType : uint8_t
    {
        Cube = 0,
        Sphere,
        ParticleEmitter,
        Count
    };

    inline const char* toString(SceneObjectType t)
    {
        switch (t)
        {
        case SceneObjectType::Cube:            return "cube";
        case SceneObjectType::Sphere:          return "sphere";
        case SceneObjectType::ParticleEmitter: return "particle_emitter";
        default:                               return "unknown";
        }
    }

    inline bool tryParseSceneObjectType(std::string_view s, SceneObjectType& out)
    {
        if (s == "cube")
        {
            out = SceneObjectType::Cube;
            return true;
        }
        if (s == "sphere")
        {
            out = SceneObjectType::Sphere;
            return true;
        }
        if (s == "particle_emitter" || s == "emitter" || s == "particle")
        {
            out = SceneObjectType::ParticleEmitter;
            return true;
        }
        return false;
    }

    // Runtime editor object: entity handle + authored fields that go into JSON.
    struct SceneObject
    {
        Entity          entity{};
        SceneObjectType type = SceneObjectType::Cube;
        float           color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
        // Index into EditorApp emitter list when type == ParticleEmitter; -1 otherwise.
        int             emitterIndex = -1;
    };

    // Plain data blob used for serialization (no live Entity).
    struct SceneObjectData
    {
        SceneObjectType  type = SceneObjectType::Cube;
        Math::Vector3f   position{ 0, 0, 0 };
        Math::Quaternion rotation{ 1, 0, 0, 0 };
        Math::Vector3f   scale{ 1, 1, 1 };
        float            color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };

        // Optional particle payload (only when type == ParticleEmitter).
        bool        hasParticle = false;
        std::string particleName = "Emitter";
        uint32_t    maxParticles = 512;
        float       emissionRate = 40.0f;
        float       duration     = 0.0f;
        bool        looping      = true;
        float       lifetimeMin  = 0.8f;
        float       lifetimeMax  = 1.6f;
        float       startSpeedMin = 1.0f;
        float       startSpeedMax = 3.0f;
        float       startSizeMin  = 0.15f;
        float       startSizeMax  = 0.35f;
        float       endSizeMin    = 0.02f;
        float       endSizeMax    = 0.10f;
        float       startColor[4]{ 1.0f, 0.7f, 0.25f, 1.0f };
        float       endColor[4]{ 1.0f, 0.15f, 0.05f, 0.0f };
        Math::Vector3f gravity{ 0.0f, -2.0f, 0.0f };
        Math::Vector3f direction{ 0.0f, 1.0f, 0.0f };
        float       spreadDegrees = 25.0f;
        int         shape         = 0; // ParticleEmitterDesc::Shape
        Math::Vector3f shapeSize{ 0.25f, 0.0f, 0.25f };
        bool        additiveBlend = true;
        float       simulationSpeed = 1.0f;
    };

    struct SceneFileData
    {
        int         version = 1;
        std::string name    = "untitled";
        std::vector<SceneObjectData> objects;
    };

} // namespace Dark
