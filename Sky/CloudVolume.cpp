#include "Content/SceneObjectSpawn.h"
#include "Core/Json.h"
#include "Sky/CloudVolume.h"

#include "third_party/nlohmann/json.hpp"

#include "Math/MathHelper.h"
#include "Math/Sphere3f.h"
#include "Render/Frustum3f.h"
#include "Scene/SceneTypes.h"

#include <cmath>

namespace Dark
{
    using Math::Clamp;
    using Math::Max;
    using Math::Min;
    using Math::Vector3f;

    namespace
    {
        Vector3f absVec(const Vector3f& v)
        {
            return Vector3f(fabsf(v.x), fabsf(v.y), fabsf(v.z));
        }

        Math::Quaternion unitInverse(Math::Quaternion q)
        {
            q.Normalize();
            return q.Inverse();
        }

        Vector3f toLocal(const Vector3f& worldPos, const TransformComponent& xf, const Math::Quaternion& invRot)
        {
            return invRot.Rotate(worldPos - xf.position);
        }
    } // namespace

    Vector3f cloudHalfExtents(const TransformComponent& xf)
    {
        return Vector3f(Max(fabsf(xf.scale.x) * 0.5f, 0.05f), Max(fabsf(xf.scale.y) * 0.5f, 0.05f), Max(fabsf(xf.scale.z) * 0.5f, 0.05f));
    }

    bool packGpuCloudVolume(const TransformComponent& xf, const CloudVolumeDesc& desc, GpuCloudVolume& out)
    {
        if (!desc.enabled || desc.density <= 1.0e-5f)
            return false;

        const Vector3f half = cloudHalfExtents(xf);
        Math::Quaternion inv = unitInverse(xf.rotation);
        Vector3f wind        = desc.windDir;
        if (wind.MagnitudeSqrd() > 1.0e-8f)
            wind.Normalize();
        else
            wind = Vector3f(1.0f, 0.0f, 0.0f);

        out            = {};
        out.center[0]  = xf.position.x;
        out.center[1]  = xf.position.y;
        out.center[2]  = xf.position.z;
        out.density    = Max(desc.density, 0.0f);
        out.halfExtents[0] = half.x;
        out.halfExtents[1] = half.y;
        out.halfExtents[2] = half.z;
        out.coverage   = Clamp(desc.coverage, 0.0f, 1.0f);
        out.invRotation[0] = inv.x;
        out.invRotation[1] = inv.y;
        out.invRotation[2] = inv.z;
        out.invRotation[3] = inv.w;
        out.albedo[0]  = Clamp(desc.albedo.x, 0.0f, 4.0f);
        out.albedo[1]  = Clamp(desc.albedo.y, 0.0f, 4.0f);
        out.albedo[2]  = Clamp(desc.albedo.z, 0.0f, 4.0f);
        out.softness   = Clamp(desc.softness, 0.02f, 1.0f);
        out.absorption = Max(desc.absorption, 0.0f);
        out.scattering = Max(desc.scattering, 0.0f);
        out.anisotropy = Clamp(desc.anisotropy, -0.95f, 0.95f);
        out.noiseScale = Max(desc.noiseScale, 1.0e-4f);
        out.detailScale     = Max(desc.detailScale, 1.0f);
        out.detailStrength  = Clamp(desc.detailStrength, 0.0f, 1.0f);
        out.heightFalloff   = Clamp(desc.heightFalloff, 0.0f, 1.0f);
        out.silverLining    = Clamp(desc.silverLining, 0.0f, 4.0f);
        out.shape           = (desc.shape == CloudShape::Box) ? 0.0f : 1.0f;
        out.windSpeed       = Max(desc.windSpeed, 0.0f);
        out.windDir[0]      = wind.x;
        out.windDir[1]      = wind.y;
        out.windDir[2]      = wind.z;
        return true;
    }

    bool gatherCloudVolumes(World& world, const Frustum3f* frustum, CloudVolumeDrawList& out)
    {
        out.count = 0;
        world.each<CloudVolumeComponent>([&](Entity e, CloudVolumeComponent& cloud) {
            if (out.count >= kMaxCloudVolumes)
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;
            GpuCloudVolume packed{};
            if (!packGpuCloudVolume(*xf, cloud.desc, packed))
                return;
            if (frustum)
            {
                const Vector3f half = cloudHalfExtents(*xf);
                const float    r    = half.Magnitude();
                if (!frustum->Intersects(Math::Sphere3f(xf->position, r)))
                    return;
            }
            out.volumes[out.count++] = packed;
        });
        return true;
    }

    bool intersectCloudVolume(const Math::Ray3f& ray, const TransformComponent& xf, CloudShape shape, float& tEnter, float& tExit)
    {
        tEnter = 0.0f;
        tExit  = 0.0f;
        const Vector3f half     = cloudHalfExtents(xf);
        Math::Quaternion invRot = unitInverse(xf.rotation);
        Vector3f dir            = ray.Direction;
        if (dir.MagnitudeSqrd() < 1.0e-12f)
            return false;
        dir.Normalize();

        const Vector3f localO = toLocal(ray.Origin, xf, invRot);
        const Vector3f localD = invRot.Rotate(dir);

        if (shape == CloudShape::Ellipsoid)
        {
            const Vector3f o(localO.x / half.x, localO.y / half.y, localO.z / half.z);
            const Vector3f d(localD.x / half.x, localD.y / half.y, localD.z / half.z);
            const float    a = d.Dot(d);
            if (a < 1.0e-12f)
                return false;
            const float b = o.Dot(d);
            const float c = o.Dot(o) - 1.0f;
            const float disc = b * b - a * c;
            if (disc < 0.0f)
                return false;
            const float s = sqrtf(disc);
            float       t0 = (-b - s) / a;
            float       t1 = (-b + s) / a;
            if (t0 > t1)
            {
                const float tmp = t0;
                t0              = t1;
                t1              = tmp;
            }
            if (t1 < 0.0f)
                return false;
            tEnter = Max(t0, 0.0f);
            tExit  = t1;
            return tExit > tEnter;
        }

        Math::Ray3f localRay(localO, localD);
        const Math::AABox3f box = Math::AABox3f::FromCenterExtents(Vector3f(0.0f, 0.0f, 0.0f), half);
        float               tMin = 0.0f;
        float               tMax = 0.0f;
        if (!localRay.IntersectAabb(box, tMin, tMax) || tMax < 0.0f)
            return false;
        tEnter = Max(tMin, 0.0f);
        tExit  = tMax;
        return tExit > tEnter;
    }

    float cloudShapeMask(const Vector3f& worldPos, const TransformComponent& xf, const CloudVolumeDesc& desc)
    {
        const Vector3f half     = cloudHalfExtents(xf);
        Math::Quaternion invRot = unitInverse(xf.rotation);
        const Vector3f   p      = toLocal(worldPos, xf, invRot);
        const float      soft   = Clamp(desc.softness, 0.02f, 1.0f);

        if (desc.shape == CloudShape::Ellipsoid)
        {
            const Vector3f n(p.x / half.x, p.y / half.y, p.z / half.z);
            const float    e = n.Magnitude();
            return Clamp((1.0f + soft * 0.15f - e) / Max(soft, 0.02f), 0.0f, 1.0f);
        }

        const Vector3f q = absVec(p) - half;
        const float outside = Vector3f(Max(q.x, 0.0f), Max(q.y, 0.0f), Max(q.z, 0.0f)).Magnitude();
        const float inside  = Max(Max(q.x, q.y), q.z);
        const float sdf     = outside + Min(inside, 0.0f);
        const float r       = soft * Min(Min(half.x, half.y), half.z);
        return Clamp(1.0f - sdf / Max(r, 0.01f), 0.0f, 1.0f);
    }

    float cloudJsonNumber(const nlohmann::json& item, float fallback)
    {
        if (const auto* number = item.get_ptr<const nlohmann::json::number_float_t*>())
            return static_cast<float>(*number);
        if (const auto* number = item.get_ptr<const nlohmann::json::number_integer_t*>())
            return static_cast<float>(*number);
        if (const auto* number = item.get_ptr<const nlohmann::json::number_unsigned_t*>())
            return static_cast<float>(*number);
        return fallback;
    }

    void readCloudVec(const nlohmann::json& value, Vector3f& out)
    {
        if (!value.is_array() || value.size() < 3)
            return;
        out = Vector3f(cloudJsonNumber(value[0], out.x), cloudJsonNumber(value[1], out.y), cloudJsonNumber(value[2], out.z));
    }

    void cloudDescFromSceneData(const SceneObjectData& d, CloudVolumeDesc& out)
    {
        out = CloudVolumeDesc{};
        const nlohmann::json cloud = nlohmann::json::parse(sceneMergedComponent(d, "CloudVolume"), nullptr, false);
        if (cloud.is_discarded() || !cloud.is_object())
            return;
        if (cloud.contains("shape"))
        {
            if (cloud["shape"].is_string())
            {
                const auto* text = cloud["shape"].get_ptr<const nlohmann::json::string_t*>();
                CloudShape shape = out.shape;
                if (text && tryParseCloudShape(*text, shape))
                    out.shape = shape;
            }
            else if (cloud["shape"].is_number_integer())
            {
                int shape = 1;
                jsonIntOr(cloud, "shape", shape, 1, "CloudVolume");
                out.shape = shape == 0 ? CloudShape::Box : CloudShape::Ellipsoid;
            }
        }
        jsonFloatOr(cloud, "density", out.density, out.density, "CloudVolume");
        jsonFloatOr(cloud, "coverage", out.coverage, out.coverage, "CloudVolume");
        jsonFloatOr(cloud, "softness", out.softness, out.softness, "CloudVolume");
        jsonFloatOr(cloud, "absorption", out.absorption, out.absorption, "CloudVolume");
        jsonFloatOr(cloud, "scattering", out.scattering, out.scattering, "CloudVolume");
        jsonFloatOr(cloud, "anisotropy", out.anisotropy, out.anisotropy, "CloudVolume");
        jsonFloatOr(cloud, "noiseScale", out.noiseScale, out.noiseScale, "CloudVolume");
        jsonFloatOr(cloud, "detailScale", out.detailScale, out.detailScale, "CloudVolume");
        jsonFloatOr(cloud, "detailStrength", out.detailStrength, out.detailStrength, "CloudVolume");
        jsonFloatOr(cloud, "heightFalloff", out.heightFalloff, out.heightFalloff, "CloudVolume");
        jsonFloatOr(cloud, "silverLining", out.silverLining, out.silverLining, "CloudVolume");
        jsonFloatOr(cloud, "windSpeed", out.windSpeed, out.windSpeed, "CloudVolume");
        jsonBoolOr(cloud, "enabled", out.enabled, out.enabled, "CloudVolume");
        if (cloud.contains("albedo"))
            readCloudVec(cloud["albedo"], out.albedo);
        if (cloud.contains("windDir"))
            readCloudVec(cloud["windDir"], out.windDir);
    }

    static nlohmann::json cloudVolumeJson(const CloudVolumeDesc& src)
    {
        nlohmann::json cloud;
        cloud["shape"]          = toString(src.shape);
        cloud["density"]        = src.density;
        cloud["coverage"]       = src.coverage;
        cloud["softness"]       = src.softness;
        cloud["absorption"]     = src.absorption;
        cloud["scattering"]     = src.scattering;
        cloud["anisotropy"]     = src.anisotropy;
        cloud["noiseScale"]     = src.noiseScale;
        cloud["detailScale"]    = src.detailScale;
        cloud["detailStrength"] = src.detailStrength;
        cloud["heightFalloff"]  = src.heightFalloff;
        cloud["silverLining"]   = src.silverLining;
        cloud["windSpeed"]      = src.windSpeed;
        cloud["albedo"]         = nlohmann::json::array({ src.albedo.x, src.albedo.y, src.albedo.z });
        cloud["windDir"]        = nlohmann::json::array({ src.windDir.x, src.windDir.y, src.windDir.z });
        cloud["enabled"]        = src.enabled;
        return cloud;
    }

    void sceneDataFromCloudDesc(const CloudVolumeDesc& src, SceneObjectData& d)
    {
        sceneSetComponentJson(d, "CloudVolume", cloudVolumeJson(src).dump());
    }

    void sceneWriteCloudDiff(SceneObjectData& object, const CloudVolumeDesc& src)
    {
        const nlohmann::json live = cloudVolumeJson(src);
        nlohmann::json baseline   = cloudVolumeJson(CloudVolumeDesc{});
        const nlohmann::json archetype = nlohmann::json::parse(sceneArchetypeComponentJson(object, "CloudVolume"), nullptr, false);
        if (!archetype.is_discarded() && archetype.is_object())
        {
            for (auto it = archetype.begin(); it != archetype.end(); ++it)
                baseline[it.key()] = it.value();
        }
        sceneWriteComponentDiff(object, "CloudVolume", live.dump(), baseline.dump());
    }

} // namespace Dark
