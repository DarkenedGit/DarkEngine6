#pragma once

#include "Render/Camera3D.h"
#include "Math/MathHelper.h"
#include "Math/Matrix4f.h"
#include "Math/Ray3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector3f.h"
#include "Math/Vector4f.h"

#include <cmath>
#include <cstdint>

namespace Dark::EditorDetail
{
    // Maya-style move tool: RGB arrows, plane squares, and a center handle.
    enum class TranslateGizmoAxis : uint8_t
    {
        None = 0,
        X,
        Y,
        Z,
        XY,
        XZ,
        YZ,
        View
    };

    struct TranslateGizmoStyle
    {
        float pixelLength = 92.0f;
        float hitPixels   = 10.0f;
        bool  selected    = false;
        TranslateGizmoAxis highlight = TranslateGizmoAxis::None;
    };

    inline bool gizmoMovesX(TranslateGizmoAxis axis)
    {
        return axis == TranslateGizmoAxis::X || axis == TranslateGizmoAxis::XY || axis == TranslateGizmoAxis::XZ || axis == TranslateGizmoAxis::View;
    }

    inline bool gizmoMovesY(TranslateGizmoAxis axis)
    {
        return axis == TranslateGizmoAxis::Y || axis == TranslateGizmoAxis::XY || axis == TranslateGizmoAxis::YZ || axis == TranslateGizmoAxis::View;
    }

    inline bool gizmoMovesZ(TranslateGizmoAxis axis)
    {
        return axis == TranslateGizmoAxis::Z || axis == TranslateGizmoAxis::XZ || axis == TranslateGizmoAxis::YZ || axis == TranslateGizmoAxis::View;
    }

    inline float snapToGrid(float v, float grid)
    {
        if (grid <= 0.0f)
            return v;
        return std::floor(v / grid + 0.5f) * grid;
    }

    inline float gizmoWorldLength(const Camera3D& cam, const Math::Vector3f& origin, float viewportH, float pixelLength)
    {
        const float h = Math::Max(viewportH, 1.0f);
        const float px = Math::Max(pixelLength, 1.0f);
        float dist = (origin - cam.GetPosition()).Magnitude();
        dist       = Math::Max(dist, cam.GetNearZ());
        const float worldPerPixel = (2.0f * dist * std::tan(cam.GetFovY() * 0.5f)) / h;
        return worldPerPixel * px;
    }

    inline bool projectToScreen(const Camera3D& cam, const Math::Vector3f& world, float vw, float vh, Math::Vector2f& out)
    {
        const Math::Vector4f clip = cam.GetViewProj() * Math::Vector4f(world, 1.0f);
        if (clip.w <= 1.0e-4f)
            return false;
        const float invW = 1.0f / clip.w;
        const float ndcZ = clip.z * invW;
        if (ndcZ < 0.0f || ndcZ > 1.0f)
            return false;
        const float ndcX = clip.x * invW;
        const float ndcY = clip.y * invW;
        out.x = (ndcX * 0.5f + 0.5f) * vw;
        out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * vh;
        return true;
    }

    inline float pointSegmentDistanceSq(const Math::Vector2f& p, const Math::Vector2f& a, const Math::Vector2f& b)
    {
        const Math::Vector2f ab = b - a;
        const float          lenSq = ab.MagnitudeSqrd();
        if (lenSq <= Math::Epsilon)
            return (p - a).MagnitudeSqrd();
        const float t = Math::Clamp((p - a).Dot(ab) / lenSq, 0.0f, 1.0f);
        const Math::Vector2f c(a.x + ab.x * t, a.y + ab.y * t);
        return (p - c).MagnitudeSqrd();
    }

    inline bool pointInTriangle(const Math::Vector2f& p, const Math::Vector2f& a, const Math::Vector2f& b, const Math::Vector2f& c)
    {
        const auto sign = [](const Math::Vector2f& p1, const Math::Vector2f& p2, const Math::Vector2f& p3) {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        };
        const float d1 = sign(p, a, b);
        const float d2 = sign(p, b, c);
        const float d3 = sign(p, c, a);
        const bool  hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
        const bool  hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
        return !(hasNeg && hasPos);
    }

    inline bool pointInQuad(const Math::Vector2f& p, const Math::Vector2f& a, const Math::Vector2f& b, const Math::Vector2f& c, const Math::Vector2f& d)
    {
        return pointInTriangle(p, a, b, c) || pointInTriangle(p, a, c, d);
    }

    inline bool gizmoPlaneFacingCamera(const Math::Vector3f& planeNormal, const Math::Vector3f& camLook)
    {
        return std::fabs(planeNormal.Dot(camLook)) > 0.25f;
    }

    inline bool intersectPlane(const Math::Ray3f& ray, const Math::Vector3f& point, const Math::Vector3f& normal, Math::Vector3f& out)
    {
        const float denom = ray.Direction.Dot(normal);
        if (std::fabs(denom) < 1.0e-5f)
            return false;
        const float t = (point - ray.Origin).Dot(normal) / denom;
        if (t < 0.0f)
            return false;
        out = ray.PointAt(t);
        return true;
    }

    inline bool translateDragPoint(TranslateGizmoAxis axis, const Math::Ray3f& ray, const Math::Vector3f& origin, const Math::Vector3f& camLook, Math::Vector3f& outPoint)
    {
        if (axis == TranslateGizmoAxis::None)
            return false;

        if (axis == TranslateGizmoAxis::View)
        {
            Math::Vector3f n = camLook;
            if (n.MagnitudeSqrd() <= Math::Epsilon)
                return false;
            n.Normalize();
            return intersectPlane(ray, origin, n, outPoint);
        }

        if (axis == TranslateGizmoAxis::XY)
            return intersectPlane(ray, origin, Math::Vector3f(0.0f, 0.0f, 1.0f), outPoint);
        if (axis == TranslateGizmoAxis::XZ)
            return intersectPlane(ray, origin, Math::Vector3f(0.0f, 1.0f, 0.0f), outPoint);
        if (axis == TranslateGizmoAxis::YZ)
            return intersectPlane(ray, origin, Math::Vector3f(1.0f, 0.0f, 0.0f), outPoint);

        Math::Vector3f unitAxis(0.0f, 0.0f, 0.0f);
        if (axis == TranslateGizmoAxis::X)
            unitAxis = Math::Vector3f(1.0f, 0.0f, 0.0f);
        else if (axis == TranslateGizmoAxis::Y)
            unitAxis = Math::Vector3f(0.0f, 1.0f, 0.0f);
        else
            unitAxis = Math::Vector3f(0.0f, 0.0f, 1.0f);

        Math::Vector3f n = unitAxis.Cross(camLook);
        n = n.Cross(unitAxis);
        if (n.MagnitudeSqrd() < 1.0e-8f)
        {
            n = unitAxis.Cross(Math::Vector3f(0.0f, 1.0f, 0.0f));
            if (n.MagnitudeSqrd() < 1.0e-8f)
                n = unitAxis.Cross(Math::Vector3f(1.0f, 0.0f, 0.0f));
            n = n.Cross(unitAxis);
        }
        if (n.MagnitudeSqrd() < 1.0e-8f)
            return false;
        n.Normalize();

        Math::Vector3f hit{};
        if (!intersectPlane(ray, origin, n, hit))
            return false;
        outPoint = origin + unitAxis * (hit - origin).Dot(unitAxis);
        return true;
    }

    inline Math::Vector3f applyTranslateDrag(TranslateGizmoAxis axis, const Math::Vector3f& startPos, const Math::Vector3f& grabPoint, const Math::Vector3f& nowPoint, float snap)
    {
        Math::Vector3f pos = startPos + (nowPoint - grabPoint);
        if (axis != TranslateGizmoAxis::View)
        {
            if (!gizmoMovesX(axis))
                pos.x = startPos.x;
            if (!gizmoMovesY(axis))
                pos.y = startPos.y;
            if (!gizmoMovesZ(axis))
                pos.z = startPos.z;
        }
        if (snap > 0.0f)
        {
            if (gizmoMovesX(axis))
                pos.x = snapToGrid(pos.x, snap);
            if (gizmoMovesY(axis))
                pos.y = snapToGrid(pos.y, snap);
            if (gizmoMovesZ(axis))
                pos.z = snapToGrid(pos.z, snap);
        }
        return pos;
    }

    inline TranslateGizmoAxis pickTranslateGizmo(const Camera3D& cam, const Math::Vector3f& origin, const Math::Vector2f& mouse, float vw, float vh, const TranslateGizmoStyle& style)
    {
        Math::Vector2f originS{};
        if (!projectToScreen(cam, origin, vw, vh, originS))
            return TranslateGizmoAxis::None;

        const float size = gizmoWorldLength(cam, origin, vh, style.selected ? style.pixelLength : style.pixelLength * 0.62f);
        const float hitPx = Math::Max(style.hitPixels, 4.0f);
        const float hitSq = hitPx * hitPx;

        constexpr float kAxis0 = 0.18f;
        constexpr float kAxis1 = 1.00f;
        constexpr float kPlane0 = 0.22f;
        constexpr float kPlane1 = 0.48f;

        const Math::Vector3f axes[3] = {
            Math::Vector3f(1.0f, 0.0f, 0.0f),
            Math::Vector3f(0.0f, 1.0f, 0.0f),
            Math::Vector3f(0.0f, 0.0f, 1.0f)
        };
        const TranslateGizmoAxis axisId[3] = { TranslateGizmoAxis::X, TranslateGizmoAxis::Y, TranslateGizmoAxis::Z };
        const TranslateGizmoAxis planeId[3] = { TranslateGizmoAxis::YZ, TranslateGizmoAxis::XZ, TranslateGizmoAxis::XY };

        if (style.selected)
        {
            const Math::Vector3f look = cam.GetLook();
            for (int i = 0; i < 3; ++i)
            {
                if (!gizmoPlaneFacingCamera(axes[i], look))
                    continue;
                const Math::Vector3f u = axes[(i + 1) % 3];
                const Math::Vector3f v = axes[(i + 2) % 3];
                Math::Vector2f a{}, b{}, c{}, d{};
                if (!projectToScreen(cam, origin + u * (size * kPlane0) + v * (size * kPlane0), vw, vh, a))
                    continue;
                if (!projectToScreen(cam, origin + u * (size * kPlane1) + v * (size * kPlane0), vw, vh, b))
                    continue;
                if (!projectToScreen(cam, origin + u * (size * kPlane1) + v * (size * kPlane1), vw, vh, c))
                    continue;
                if (!projectToScreen(cam, origin + u * (size * kPlane0) + v * (size * kPlane1), vw, vh, d))
                    continue;
                if (pointInQuad(mouse, a, b, c, d))
                    return planeId[i];
            }
        }

        TranslateGizmoAxis best = TranslateGizmoAxis::None;
        float bestSq = hitSq;
        for (int i = 0; i < 3; ++i)
        {
            Math::Vector2f a{}, b{};
            if (!projectToScreen(cam, origin + axes[i] * (size * kAxis0), vw, vh, a))
                continue;
            if (!projectToScreen(cam, origin + axes[i] * (size * kAxis1), vw, vh, b))
                continue;
            if ((b - a).MagnitudeSqrd() < 16.0f)
                continue;
            const float dSq = pointSegmentDistanceSq(mouse, a, b);
            if (dSq <= bestSq)
            {
                bestSq = dSq;
                best   = axisId[i];
            }
        }
        if (best != TranslateGizmoAxis::None)
            return best;

        const float originHit = style.selected ? hitPx * 1.15f : hitPx;
        if ((mouse - originS).MagnitudeSqrd() <= originHit * originHit)
            return TranslateGizmoAxis::View;
        return TranslateGizmoAxis::None;
    }

    void drawTranslateGizmo(const Camera3D& cam, const Math::Vector3f& origin, float vw, float vh, const TranslateGizmoStyle& style);
} // namespace Dark::EditorDetail
