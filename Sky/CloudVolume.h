#pragma once

#include "ECS/Components.h"
#include "ECS/World.h"
#include "Math/Quaternion.h"
#include "Math/Ray3f.h"
#include "Math/Vector3f.h"

#include <cstdint>
#include <string_view>

namespace Dark
{

    class Frustum3f;

    enum class CloudShape : uint8_t
    {
        Box       = 0,
        Ellipsoid = 1,
    };

    inline const char* toString(CloudShape shape)
    {
        return shape == CloudShape::Box ? "box" : "ellipsoid";
    }

    inline bool tryParseCloudShape(std::string_view s, CloudShape& out)
    {
        if (s == "box" || s == "aabb")
        {
            out = CloudShape::Box;
            return true;
        }
        if (s == "ellipsoid" || s == "sphere" || s == "ellipse")
        {
            out = CloudShape::Ellipsoid;
            return true;
        }
        return false;
    }

    struct CloudVolumeDesc
    {
        CloudShape     shape           = CloudShape::Ellipsoid;
        float          density         = 0.90f;
        float          coverage        = 0.58f;
        float          softness        = 0.42f;
        float          absorption      = 1.15f;
        float          scattering      = 1.00f;
        float          anisotropy      = 0.45f;
        float          noiseScale      = 0.055f;
        float          detailScale     = 3.40f;
        float          detailStrength  = 0.38f;
        float          heightFalloff   = 0.55f;
        float          silverLining    = 0.75f;
        float          windSpeed       = 1.20f;
        Math::Vector3f albedo{ 0.90f, 0.93f, 1.00f };
        Math::Vector3f windDir{ 1.00f, 0.02f, 0.25f };
        bool           enabled = true;
    };

    struct CloudVolumeComponent
    {
        static constexpr const char* kTypeName = "CloudVolume";

        CloudVolumeDesc desc{};
    };

    static constexpr uint32_t kMaxCloudVolumes = 16;

    struct GpuCloudVolume
    {
        float center[3];
        float density;
        float halfExtents[3];
        float coverage;
        float invRotation[4]; // xyzw
        float albedo[3];
        float softness;
        float absorption;
        float scattering;
        float anisotropy;
        float noiseScale;
        float detailScale;
        float detailStrength;
        float heightFalloff;
        float silverLining;
        float shape;
        float windSpeed;
        float pad0;
        float pad1;
        float windDir[3];
        float pad2;
    };
    static_assert(sizeof(GpuCloudVolume) == 128, "GpuCloudVolume stride");

    struct CloudVolumeDrawList
    {
        GpuCloudVolume volumes[kMaxCloudVolumes];
        uint32_t       count = 0;
    };

    Math::Vector3f cloudHalfExtents(const TransformComponent& xf);
    bool           packGpuCloudVolume(const TransformComponent& xf, const CloudVolumeDesc& desc, GpuCloudVolume& out);
    bool           gatherCloudVolumes(World& world, const Frustum3f* frustum, CloudVolumeDrawList& out);

    // Ray vs volume. On hit, tEnter is 0 when the origin is inside.
    bool intersectCloudVolume(const Math::Ray3f& ray, const TransformComponent& xf, CloudShape shape, float& tEnter, float& tExit);

    // Shape mask in [0,1] at a world point (no noise). Used by tests and CPU helpers.
    float cloudShapeMask(const Math::Vector3f& worldPos, const TransformComponent& xf, const CloudVolumeDesc& desc);

    struct SceneObjectData;
    void cloudDescFromSceneData(const SceneObjectData& d, CloudVolumeDesc& out);
    void sceneDataFromCloudDesc(const CloudVolumeDesc& src, SceneObjectData& d);

} // namespace Dark
