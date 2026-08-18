#pragma once

#include "ECS/Entity.h"
#include "Math/Quaternion.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    enum class SceneMode : uint8_t
    {
        Scene3D = 0,
        Scene2D,
    };

    inline const char* toString(SceneMode mode)
    {
        return mode == SceneMode::Scene2D ? "2d" : "3d";
    }

    inline bool tryParseSceneMode(std::string_view s, SceneMode& out)
    {
        if (s == "2d" || s == "2D")
        {
            out = SceneMode::Scene2D;
            return true;
        }
        if (s == "3d" || s == "3D" || s.empty())
        {
            out = SceneMode::Scene3D;
            return true;
        }
        return false;
    }

    // Serializable prop kind used by the level editor.
    enum class SceneObjectType : uint8_t
    {
        Cube = 0,
        Sphere,
        ParticleEmitter,
        Platform,
        Coin,
        Spawn,
        Count
    };

    inline bool isScene3DType(SceneObjectType t)
    {
        return t == SceneObjectType::Cube || t == SceneObjectType::Sphere || t == SceneObjectType::ParticleEmitter;
    }

    inline bool isScene2DType(SceneObjectType t)
    {
        return t == SceneObjectType::Platform || t == SceneObjectType::Coin || t == SceneObjectType::Spawn;
    }

    inline const char* toString(SceneObjectType t)
    {
        switch (t)
        {
        case SceneObjectType::Cube:            return "cube";
        case SceneObjectType::Sphere:          return "sphere";
        case SceneObjectType::ParticleEmitter: return "particle_emitter";
        case SceneObjectType::Platform:        return "platform";
        case SceneObjectType::Coin:            return "coin";
        case SceneObjectType::Spawn:           return "spawn";
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
        if (s == "platform")
        {
            out = SceneObjectType::Platform;
            return true;
        }
        if (s == "coin")
        {
            out = SceneObjectType::Coin;
            return true;
        }
        if (s == "spawn" || s == "player_spawn")
        {
            out = SceneObjectType::Spawn;
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
        int         renderMode    = 0; // ParticleEmitterDesc::RenderMode
        uint32_t    ribbonCount   = 1;
        float       ribbonUvScale = 1.0f;
    };

    struct SceneFileData
    {
        int         version = 1;
        std::string name    = "untitled";
        SceneMode   mode    = SceneMode::Scene3D;
        Math::Vector2f worldMin{ 0.0f, 0.0f };
        Math::Vector2f worldMax{ 96.0f, 22.0f };
        std::vector<SceneObjectData> objects;
    };

} // namespace Dark
