#include "Particles/ParticleRibbon.h"
#include "Math/MathHelper.h"

#include <algorithm>
#include <cmath>

namespace Dark
{
    using namespace Math;

    namespace
    {

        Vector3f normalizeSafe(const Vector3f& v, const Vector3f& fallback)
        {
            const float lenSq = v.MagnitudeSqrd();
            if (lenSq < 1.0e-10f)
                return fallback;
            const float inv = 1.0f / std::sqrt(lenSq);
            return Vector3f(v.x * inv, v.y * inv, v.z * inv);
        }

        void lerpColor(const float a[4], const float b[4], float t, float out[4])
        {
            out[0] = a[0] + (b[0] - a[0]) * t;
            out[1] = a[1] + (b[1] - a[1]) * t;
            out[2] = a[2] + (b[2] - a[2]) * t;
            out[3] = a[3] + (b[3] - a[3]) * t;
        }

        void pushVert(std::vector<ParticleVertex>& out, const Vector3f& pos, float u, float v, const float col[4])
        {
            ParticleVertex vert{};
            vert.px = pos.x;
            vert.py = pos.y;
            vert.pz = pos.z;
            vert.u  = u;
            vert.v  = v;
            vert.r  = col[0];
            vert.g  = col[1];
            vert.b  = col[2];
            vert.a  = col[3];
            out.push_back(vert);
        }

        Vector3f sideFromTangent(const Vector3f& tangent, const Vector3f& toCam, const Vector3f& prevSide)
        {
            Vector3f side = tangent.Cross(toCam);
            if (side.MagnitudeSqrd() < 1.0e-10f)
                side = tangent.Cross(Vector3f::Y_AXIS);
            if (side.MagnitudeSqrd() < 1.0e-10f)
                side = tangent.Cross(Vector3f::X_AXIS);
            side = normalizeSafe(side, Vector3f::X_AXIS);
            if (prevSide.MagnitudeSqrd() > 1.0e-10f && side.Dot(prevSide) < 0.0f)
                side = -side;
            return side;
        }

    } // namespace

    uint32_t clampRibbonCount(uint32_t count)
    {
        if (count < 1)
            return 1;
        if (count > kMaxRibbonCount)
            return kMaxRibbonCount;
        return count;
    }

    void collectRibbonNodes(const std::vector<Particle>& particles, uint32_t ribbonId, std::vector<RibbonNode>& out)
    {
        out.clear();

        std::vector<uint32_t> order;
        order.reserve(particles.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(particles.size()); ++i)
        {
            const Particle& p = particles[i];
            if (!p.alive || p.ribbonId != ribbonId)
                continue;
            order.push_back(i);
        }
        if (order.size() < 2)
            return;

        std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
            return particles[a].seq < particles[b].seq;
        });

        out.resize(order.size());
        for (size_t i = 0; i < order.size(); ++i)
        {
            const Particle& p = particles[order[i]];
            const float     t = (p.maxLife > 1.0e-5f) ? Clamp(1.0f - (p.life / p.maxLife), 0.0f, 1.0f) : 1.0f;
            RibbonNode&     n = out[i];
            n.position        = p.position;
            n.size            = p.size0 + (p.size1 - p.size0) * t;
            lerpColor(p.color0, p.color1, t, n.color);
        }
    }

    uint32_t appendCameraFacingRibbon(
        const RibbonNode* nodes,
        uint32_t count,
        const Vector3f& cameraPos,
        float uvScale,
        std::vector<ParticleVertex>& out)
    {
        if (!nodes || count < 2)
            return 0;
        if (uvScale <= 0.0f)
            uvScale = 1.0f;

        std::vector<float> dist(count, 0.0f);
        for (uint32_t i = 1; i < count; ++i)
        {
            const Vector3f d = nodes[i].position - nodes[i - 1].position;
            dist[i]          = dist[i - 1] + d.Magnitude();
        }
        const float total = (dist.back() > 1.0e-5f) ? dist.back() : 1.0f;

        std::vector<Vector3f> left(count);
        std::vector<Vector3f> right(count);
        Vector3f              prevSide(0.0f, 0.0f, 0.0f);
        Vector3f              lastTangent(0.0f, 0.0f, 1.0f);

        for (uint32_t i = 0; i < count; ++i)
        {
            Vector3f tangent;
            if (i + 1 < count && i > 0)
                tangent = nodes[i + 1].position - nodes[i - 1].position;
            else if (i + 1 < count)
                tangent = nodes[i + 1].position - nodes[i].position;
            else
                tangent = nodes[i].position - nodes[i - 1].position;

            if (tangent.MagnitudeSqrd() < 1.0e-10f)
                tangent = lastTangent;
            else
                lastTangent = tangent;

            const Vector3f toCam = cameraPos - nodes[i].position;
            const Vector3f side  = sideFromTangent(tangent, toCam, prevSide);
            prevSide             = side;

            const float    half = nodes[i].size * 0.5f;
            const Vector3f off  = side * half;
            left[i]             = nodes[i].position - off;
            right[i]            = nodes[i].position + off;
        }

        const uint32_t before = static_cast<uint32_t>(out.size());
        for (uint32_t i = 0; i + 1 < count; ++i)
        {
            const float u0 = (dist[i] / total) * uvScale;
            const float u1 = (dist[i + 1] / total) * uvScale;
            pushVert(out, left[i], u0, 0.0f, nodes[i].color);
            pushVert(out, right[i], u0, 1.0f, nodes[i].color);
            pushVert(out, right[i + 1], u1, 1.0f, nodes[i + 1].color);
            pushVert(out, left[i], u0, 0.0f, nodes[i].color);
            pushVert(out, right[i + 1], u1, 1.0f, nodes[i + 1].color);
            pushVert(out, left[i + 1], u1, 0.0f, nodes[i + 1].color);
        }
        return static_cast<uint32_t>(out.size()) - before;
    }

} // namespace Dark
