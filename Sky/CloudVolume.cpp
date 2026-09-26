#include "Sky/CloudVolume.h"

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

    void cloudDescFromSceneData(const SceneObjectData& d, CloudVolumeDesc& out)
    {
        out                  = CloudVolumeDesc{};
        out.shape            = (d.cloudShape == 0) ? CloudShape::Box : CloudShape::Ellipsoid;
        out.density          = d.cloudDensity;
        out.coverage         = d.cloudCoverage;
        out.softness         = d.cloudSoftness;
        out.absorption       = d.cloudAbsorption;
        out.scattering       = d.cloudScattering;
        out.anisotropy       = d.cloudAnisotropy;
        out.noiseScale       = d.cloudNoiseScale;
        out.detailScale      = d.cloudDetailScale;
        out.detailStrength   = d.cloudDetailStrength;
        out.heightFalloff    = d.cloudHeightFalloff;
        out.silverLining     = d.cloudSilverLining;
        out.windSpeed        = d.cloudWindSpeed;
        out.albedo           = Vector3f(d.cloudAlbedo[0], d.cloudAlbedo[1], d.cloudAlbedo[2]);
        out.windDir          = Vector3f(d.cloudWindDir[0], d.cloudWindDir[1], d.cloudWindDir[2]);
        out.enabled          = d.cloudEnabled;
    }

    void sceneDataFromCloudDesc(const CloudVolumeDesc& src, SceneObjectData& d)
    {
        d.hasCloud             = true;
        d.cloudShape           = (src.shape == CloudShape::Box) ? 0 : 1;
        d.cloudDensity         = src.density;
        d.cloudCoverage        = src.coverage;
        d.cloudSoftness        = src.softness;
        d.cloudAbsorption      = src.absorption;
        d.cloudScattering      = src.scattering;
        d.cloudAnisotropy      = src.anisotropy;
        d.cloudNoiseScale      = src.noiseScale;
        d.cloudDetailScale     = src.detailScale;
        d.cloudDetailStrength  = src.detailStrength;
        d.cloudHeightFalloff   = src.heightFalloff;
        d.cloudSilverLining    = src.silverLining;
        d.cloudWindSpeed       = src.windSpeed;
        d.cloudAlbedo[0]       = src.albedo.x;
        d.cloudAlbedo[1]       = src.albedo.y;
        d.cloudAlbedo[2]       = src.albedo.z;
        d.cloudWindDir[0]      = src.windDir.x;
        d.cloudWindDir[1]      = src.windDir.y;
        d.cloudWindDir[2]      = src.windDir.z;
        d.cloudEnabled         = src.enabled;
    }

} // namespace Dark
