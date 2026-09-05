#include "Render/LocalLightGather.h"

#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Math/Quaternion.h"
#include "Math/Sphere3f.h"
#include "Math/Vector4f.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Dark
{
    using namespace Math;

    namespace
    {
        struct Candidate
        {
            GpuLocalLight gpu{};
            Matrix4f      world{};
            float         score    = 0.0f;
            float         tanOuter = 0.0f;
            bool          inside   = false;
            bool          spot     = false;
        };

        void resetLists(LocalLightDrawLists& out)
        {
            out.count         = 0;
            out.pointOutCount = 0;
            out.spotOutCount  = 0;
            out.insideCount   = 0;
            out.waterCount    = 0;
            for (uint32_t i = 0; i < kWaterLocalLightMax; ++i)
                out.waterIndex[i] = 0;
        }

        void warnOverflow(size_t found, uint32_t kept)
        {
            static uint32_t s_skip = 0;
            if (s_skip == 0)
            {
                DE_LOG_WARN(LogCategory::Render, "gatherLocalLights: {} in-frustum lights, keeping top {}", found, kept);
            }
            s_skip = (s_skip + 1u) % 60u;
        }

        Vector3f dirOrZ(const Vector3f& dir)
        {
            if (dir.MagnitudeSqrd() <= Epsilon)
                return Vector3f(Vector3f::Z_AXIS);
            Vector3f n = dir;
            n.Normalize();
            return n;
        }

        Matrix4f makePointVolumeWorld(const Vector3f& pos, float range)
        {
            return Matrix4f::ScaleMatrix(range) * Matrix4f::TranslationMatrix(pos.x, pos.y, pos.z);
        }

        Matrix4f makeSpotVolumeWorld(const Vector3f& pos, const Vector3f& dir, float range, float tanOuter)
        {
            const float    xy = tanOuter * range;
            const Vector3f up = (fabsf(dir.y) > 0.9f) ? Vector3f(Vector3f::X_AXIS) : Vector3f(Vector3f::Y_AXIS);
            const Matrix4f S  = Matrix4f::ScaleMatrixXYZ(xy, xy, range);
            const Matrix4f R  = Quaternion::FromLookRotation(dir, up).ToMatrix4();
            const Matrix4f T  = Matrix4f::TranslationMatrix(pos.x, pos.y, pos.z);
            return S * R * T;
        }

        bool projectCornersToScissor(const Vector3f* corners, int count, const Matrix4f& viewProj, uint32_t viewportW, uint32_t viewportH, D3D12_RECT& outRect)
        {
            if (!corners || count <= 0 || viewportW == 0 || viewportH == 0)
                return false;

            const float vw = static_cast<float>(viewportW);
            const float vh = static_cast<float>(viewportH);

            float minX =  1.0e30f;
            float minY =  1.0e30f;
            float maxX = -1.0e30f;
            float maxY = -1.0e30f;

            for (int i = 0; i < count; ++i)
            {
                const Vector4f clip = viewProj * Vector4f(corners[i], 1.0f);
                float          ndcX = 0.0f;
                float          ndcY = 0.0f;
                if (clip.w <= 0.0f)
                {
                    ndcX = clip.x >= 0.0f ? 1.0f : -1.0f;
                    ndcY = clip.y >= 0.0f ? 1.0f : -1.0f;
                }
                else
                {
                    ndcX = clip.x / clip.w;
                    ndcY = clip.y / clip.w;
                }
                const float px = (ndcX * 0.5f + 0.5f) * vw;
                const float py = (1.0f - (ndcY * 0.5f + 0.5f)) * vh;
                minX = Min(minX, px);
                minY = Min(minY, py);
                maxX = Max(maxX, px);
                maxY = Max(maxY, py);
            }

            minX = Clamp(minX, 0.0f, vw);
            maxX = Clamp(maxX, 0.0f, vw);
            minY = Clamp(minY, 0.0f, vh);
            maxY = Clamp(maxY, 0.0f, vh);

            const LONG left   = static_cast<LONG>(floorf(minX));
            const LONG top    = static_cast<LONG>(floorf(minY));
            const LONG right  = static_cast<LONG>(ceilf(maxX));
            const LONG bottom = static_cast<LONG>(ceilf(maxY));
            if (left >= right || top >= bottom)
                return false;

            const LONG area = (right - left) * (bottom - top);
            if (area < 16)
                return false;

            outRect.left   = left;
            outRect.top    = top;
            outRect.right  = right;
            outRect.bottom = bottom;
            return true;
        }

        bool computeInsideScissor(const Candidate& c, const Matrix4f& viewProj, uint32_t viewportW, uint32_t viewportH, D3D12_RECT& outRect)
        {
            const Vector3f pos(c.gpu.pos[0], c.gpu.pos[1], c.gpu.pos[2]);
            const float    range = c.gpu.range;

            if (c.spot)
            {
                const Vector3f dir = dirOrZ(Vector3f(c.gpu.dir[0], c.gpu.dir[1], c.gpu.dir[2]));
                const Vector3f up  = (fabsf(dir.y) > 0.9f) ? Vector3f(Vector3f::X_AXIS) : Vector3f(Vector3f::Y_AXIS);
                const Quaternion q = Quaternion::FromLookRotation(dir, up);
                const Vector3f right = q.Rotate(Vector3f::X_AXIS);
                const Vector3f cup   = q.Rotate(Vector3f::Y_AXIS);
                const Vector3f base  = pos + dir * range;
                const float    br    = c.tanOuter * range;
                const Vector3f corners[5] = {
                    pos,
                    base + right * br + cup * br,
                    base + right * br - cup * br,
                    base - right * br + cup * br,
                    base - right * br - cup * br,
                };
                return projectCornersToScissor(corners, 5, viewProj, viewportW, viewportH, outRect);
            }

            const Vector3f corners[8] = {
                pos + Vector3f(-range, -range, -range),
                pos + Vector3f(-range, -range,  range),
                pos + Vector3f(-range,  range, -range),
                pos + Vector3f(-range,  range,  range),
                pos + Vector3f( range, -range, -range),
                pos + Vector3f( range, -range,  range),
                pos + Vector3f( range,  range, -range),
                pos + Vector3f( range,  range,  range),
            };
            return projectCornersToScissor(corners, 8, viewProj, viewportW, viewportH, outRect);
        }

        void packGpu(GpuLocalLight& gpu, const Vector3f& pos, const Vector3f& color, float intensity, float range,
                     const Vector3f& dir, bool spot, float innerDeg, float outerDeg, float sourceRadius)
        {
            gpu.pos[0] = pos.x;
            gpu.pos[1] = pos.y;
            gpu.pos[2] = pos.z;
            gpu.range  = range;
            gpu.color[0] = color.x * intensity;
            gpu.color[1] = color.y * intensity;
            gpu.color[2] = color.z * intensity;
            const float r2 = range * range;
            gpu.invRange2 = 1.0f / Max(r2, 1.0e-8f);
            if (spot)
            {
                gpu.dir[0] = dir.x;
                gpu.dir[1] = dir.y;
                gpu.dir[2] = dir.z;
                gpu.type   = 1.0f;
            }
            else
            {
                gpu.dir[0] = 0.0f;
                gpu.dir[1] = 0.0f;
                gpu.dir[2] = 0.0f;
                gpu.type   = 0.0f;
            }
            float inner = innerDeg;
            float outer = outerDeg;
            if (inner > outer)
                inner = outer;
            gpu.innerCos     = cosf(DegreesToRadians(inner));
            gpu.outerCos     = cosf(DegreesToRadians(outer));
            gpu.sourceRadius = sourceRadius;
            gpu.pad          = 0.0f;
        }
    } // namespace

    bool gatherLocalLights(World& world, const LocalLightCullInput& in, LocalLightDrawLists& out)
    {
        resetLists(out);

        if (!in.frustum || !in.viewProj)
            return false;

        const uint32_t maxOut = Min(in.maxOut, kMaxLocalLights);
        if (maxOut == 0)
            return true;

        std::vector<Candidate> cands;
        world.each<LocalLightComponent>([&](Entity e, LocalLightComponent& light) {
            if (!light.enabled)
                return;
            const TransformComponent* xf = world.get<TransformComponent>(e);
            if (!xf)
                return;

            const float range = Min(light.range, in.maxRange);
            if (range <= 0.0f || light.intensity <= 0.0f)
                return;

            const Vector3f& pos = xf->position;
            if (!in.frustum->Intersects(Sphere3f(pos, range)))
                return;

            bool isSpot = (light.type == LocalLightType::Spot) && (light.outerConeDeg > 0.0f);
            Vector3f dir(0.0f, 0.0f, 0.0f);
            float    tanOuter = 0.0f;
            if (isSpot)
            {
                dir = dirOrZ(xf->rotation.Rotate(Vector3f::Z_AXIS));
                float outerRad = DegreesToRadians(light.outerConeDeg);
                if (outerRad >= HalfPi - 0.01f)
                    outerRad = HalfPi - 0.01f;
                tanOuter = tanf(outerRad);
            }

            const Vector3f toCam = pos - in.cameraPos;
            const float    dist  = toCam.Magnitude();
            const bool     inside = (dist - range) < in.nearZ;

            Candidate c{};
            packGpu(c.gpu, pos, light.color, light.intensity, range, dir, isSpot, light.innerConeDeg, light.outerConeDeg, light.sourceRadius);
            c.world    = isSpot ? makeSpotVolumeWorld(pos, dir, range, tanOuter) : makePointVolumeWorld(pos, range);
            c.score    = light.intensity / (dist * dist + 1.0f);
            c.tanOuter = tanOuter;
            c.inside   = inside;
            c.spot     = isSpot;
            cands.push_back(c);
        });

        if (cands.size() > maxOut)
        {
            warnOverflow(cands.size(), maxOut);
            std::partial_sort(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(maxOut), cands.end(),
                              [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
            cands.resize(maxOut);
        }

        const uint32_t n = static_cast<uint32_t>(cands.size());
        std::vector<uint32_t> waterSrc;
        waterSrc.reserve(kWaterLocalLightMax);
        {
            std::vector<uint32_t> order(n);
            for (uint32_t i = 0; i < n; ++i)
                order[i] = i;
            const uint32_t take = Min(kWaterLocalLightMax, n);
            if (take > 0)
            {
                std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(take), order.end(),
                                  [&](uint32_t a, uint32_t b) { return cands[a].score > cands[b].score; });
                waterSrc.assign(order.begin(), order.begin() + take);
            }
        }

        std::vector<int> slotOf(n, -1);

        auto packAt = [&](uint32_t src) {
            const uint32_t dst = out.count;
            out.lights[dst]      = cands[src].gpu;
            out.volumeWorld[dst] = cands[src].world;
            slotOf[src]          = static_cast<int>(dst);
            ++out.count;
        };

        for (uint32_t i = 0; i < n; ++i)
        {
            if (!cands[i].inside && !cands[i].spot)
                packAt(i);
        }
        out.pointOutCount = out.count;

        for (uint32_t i = 0; i < n; ++i)
        {
            if (!cands[i].inside && cands[i].spot)
                packAt(i);
        }
        out.spotOutCount = out.count - out.pointOutCount;

        for (uint32_t i = 0; i < n; ++i)
        {
            if (!cands[i].inside)
                continue;
            D3D12_RECT rect{};
            if (!computeInsideScissor(cands[i], *in.viewProj, in.viewportW, in.viewportH, rect))
                continue;
            out.insideScissor[out.insideCount] = rect;
            packAt(i);
            ++out.insideCount;
        }

        for (uint32_t src : waterSrc)
        {
            if (slotOf[src] < 0)
                continue;
            out.waterIndex[out.waterCount] = static_cast<uint32_t>(slotOf[src]);
            ++out.waterCount;
        }

        return true;
    }

} // namespace Dark
