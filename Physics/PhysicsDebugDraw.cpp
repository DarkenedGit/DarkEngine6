#include "Physics/PhysicsDebugDraw.h"
#include "Math/MathDefines.h"

#include <box3d/box3d.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace Dark::Physics
{
    namespace
    {
        struct DebugWireframe
        {
            std::vector<b3Vec3> a;
            std::vector<b3Vec3> b;
        };

        Math::Vector3f fromB3(const b3Vec3& v)
        {
            return Math::Vector3f(v.x, v.y, v.z);
        }

        Math::Vector3f fromB3Pos(const b3Pos& p)
        {
            return Math::Vector3f(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z));
        }

        void addLocalSegment(DebugWireframe& w, b3Vec3 p1, b3Vec3 p2)
        {
            w.a.push_back(p1);
            w.b.push_back(p2);
        }

        void addCircle(DebugWireframe& w, b3Vec3 center, b3Vec3 axis, float radius, int segs)
        {
            if (radius <= 0.0f || segs < 3)
                return;
            b3Vec3 n = b3Normalize(axis);
            if (b3LengthSquared(n) < 1.0e-12f)
                n = b3Vec3_axisY;
            const b3Vec3 hint = (std::fabs(n.y) < 0.9f) ? b3Vec3_axisY : b3Vec3_axisX;
            b3Vec3       t    = b3Normalize(b3Cross(n, hint));
            b3Vec3       b    = b3Cross(n, t);
            b3Vec3       prev{};
            for (int i = 0; i <= segs; ++i)
            {
                const float th = (static_cast<float>(i) / static_cast<float>(segs)) * Math::TwoPi;
                const float c  = std::cos(th);
                const float s  = std::sin(th);
                const b3Vec3 p = b3Add(center, b3Add(b3MulSV(radius * c, t), b3MulSV(radius * s, b)));
                if (i > 0)
                    addLocalSegment(w, prev, p);
                prev = p;
            }
        }

        void addSphere(DebugWireframe& w, b3Vec3 center, float radius)
        {
            addCircle(w, center, b3Vec3_axisY, radius, 12);
            addCircle(w, center, b3Vec3_axisX, radius, 12);
            addCircle(w, center, b3Vec3_axisZ, radius, 12);
        }

        void addCapsule(DebugWireframe& w, b3Vec3 c1, b3Vec3 c2, float radius)
        {
            b3Vec3 axis = b3Sub(c2, c1);
            if (b3LengthSquared(axis) < 1.0e-12f)
            {
                addSphere(w, c1, radius);
                return;
            }
            addCircle(w, c1, axis, radius, 12);
            addCircle(w, c2, axis, radius, 12);
            b3Vec3 n = b3Normalize(axis);
            const b3Vec3 hint = (std::fabs(n.y) < 0.9f) ? b3Vec3_axisY : b3Vec3_axisX;
            b3Vec3       t    = b3Normalize(b3Cross(n, hint));
            b3Vec3       b    = b3Cross(n, t);
            const b3Vec3 offs[4] = {t, b3Neg(t), b, b3Neg(b)};
            for (const b3Vec3& o : offs)
            {
                const b3Vec3 d = b3MulSV(radius, o);
                addLocalSegment(w, b3Add(c1, d), b3Add(c2, d));
            }
            addSphere(w, c1, radius);
            addSphere(w, c2, radius);
        }

        void addAabb(DebugWireframe& w, b3Vec3 lo, b3Vec3 hi)
        {
            const b3Vec3 c[8] = {
                {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z}, {hi.x, hi.y, lo.z}, {lo.x, hi.y, lo.z},
                {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z}, {hi.x, hi.y, hi.z}, {lo.x, hi.y, hi.z},
            };
            const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto& edge : e)
                addLocalSegment(w, c[edge[0]], c[edge[1]]);
        }

        void addHull(DebugWireframe& w, const b3HullData* hull)
        {
            if (!hull)
                return;
            const b3Vec3*         pts   = b3GetHullPoints(hull);
            const b3HullHalfEdge* edges = b3GetHullEdges(hull);
            if (!pts || !edges || hull->edgeCount <= 0)
                return;
            for (int i = 0; i < hull->edgeCount; ++i)
            {
                const int twin = edges[i].twin;
                if (twin < 0 || twin <= i)
                    continue;
                addLocalSegment(w, pts[edges[i].origin], pts[edges[twin].origin]);
            }
        }

        void addMesh(DebugWireframe& w, const b3Mesh* mesh)
        {
            if (!mesh || !mesh->data)
                return;
            const b3Vec3*         verts = b3GetMeshVertices(mesh->data);
            const b3MeshTriangle* tris  = b3GetMeshTriangles(mesh->data);
            if (!verts || !tris)
                return;
            const b3Vec3 scale = mesh->scale;
            auto scaled = [&](int idx) {
                const b3Vec3 v = verts[idx];
                return b3Vec3{v.x * scale.x, v.y * scale.y, v.z * scale.z};
            };
            for (int i = 0; i < mesh->data->triangleCount; ++i)
            {
                const b3Vec3 p1 = scaled(tris[i].index1);
                const b3Vec3 p2 = scaled(tris[i].index2);
                const b3Vec3 p3 = scaled(tris[i].index3);
                addLocalSegment(w, p1, p2);
                addLocalSegment(w, p2, p3);
                addLocalSegment(w, p3, p1);
            }
        }

        void* createDebugShape(const b3DebugShape* debugShape, void*)
        {
            auto* w = new DebugWireframe();
            if (!debugShape)
                return w;
            switch (debugShape->type)
            {
            case b3_sphereShape:
                if (debugShape->sphere)
                    addSphere(*w, debugShape->sphere->center, debugShape->sphere->radius);
                break;
            case b3_capsuleShape:
                if (debugShape->capsule)
                    addCapsule(*w, debugShape->capsule->center1, debugShape->capsule->center2, debugShape->capsule->radius);
                break;
            case b3_hullShape:
                addHull(*w, debugShape->hull);
                break;
            case b3_meshShape:
                addMesh(*w, debugShape->mesh);
                break;
            default:
                break;
            }
            return w;
        }

        void destroyDebugShape(void* userShape, void*)
        {
            delete static_cast<DebugWireframe*>(userShape);
        }

        bool drawShape(void* userShape, b3WorldTransform transform, b3HexColor, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            auto* w   = static_cast<const DebugWireframe*>(userShape);
            if (!out || !w)
                return true;
            const b3Transform xf = transform;
            for (size_t i = 0; i < w->a.size(); ++i)
            {
                const b3Vec3 p1 = b3TransformPoint(xf, w->a[i]);
                const b3Vec3 p2 = b3TransformPoint(xf, w->b[i]);
                if (!appendPhysicsDebugLine(*out, fromB3(p1), fromB3(p2)))
                    return false;
            }
            return true;
        }

        void drawSegment(b3Pos p1, b3Pos p2, b3HexColor, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            if (!out)
                return;
            appendPhysicsDebugLine(*out, fromB3Pos(p1), fromB3Pos(p2));
        }

        void drawPoint(b3Pos p, float size, b3HexColor, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            if (!out)
                return;
            float s = size * 0.01f;
            if (s < 0.03f)
                s = 0.03f;
            if (s > 0.15f)
                s = 0.15f;
            const Math::Vector3f c = fromB3Pos(p);
            appendPhysicsDebugLine(*out, Math::Vector3f(c.x - s, c.y, c.z), Math::Vector3f(c.x + s, c.y, c.z));
            appendPhysicsDebugLine(*out, Math::Vector3f(c.x, c.y - s, c.z), Math::Vector3f(c.x, c.y + s, c.z));
            appendPhysicsDebugLine(*out, Math::Vector3f(c.x, c.y, c.z - s), Math::Vector3f(c.x, c.y, c.z + s));
        }

        void drawBounds(b3AABB aabb, b3HexColor, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            if (!out)
                return;
            DebugWireframe w;
            addAabb(w, aabb.lowerBound, aabb.upperBound);
            for (size_t i = 0; i < w.a.size(); ++i)
            {
                if (!appendPhysicsDebugLine(*out, fromB3(w.a[i]), fromB3(w.b[i])))
                    return;
            }
        }

        void drawBox(b3Vec3 extents, b3WorldTransform transform, b3HexColor, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            if (!out)
                return;
            DebugWireframe w;
            addAabb(w, b3Neg(extents), extents);
            const b3Transform xf = transform;
            for (size_t i = 0; i < w.a.size(); ++i)
            {
                const b3Vec3 p1 = b3TransformPoint(xf, w.a[i]);
                const b3Vec3 p2 = b3TransformPoint(xf, w.b[i]);
                if (!appendPhysicsDebugLine(*out, fromB3(p1), fromB3(p2)))
                    return;
            }
        }

        void drawSphere(b3Pos p, float radius, b3HexColor, float, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            if (!out)
                return;
            DebugWireframe w;
            addSphere(w, b3Vec3{static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)}, radius);
            for (size_t i = 0; i < w.a.size(); ++i)
            {
                if (!appendPhysicsDebugLine(*out, fromB3(w.a[i]), fromB3(w.b[i])))
                    return;
            }
        }

        void drawCapsule(b3Pos p1, b3Pos p2, float radius, b3HexColor, float, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            if (!out)
                return;
            DebugWireframe w;
            addCapsule(w,
                       b3Vec3{static_cast<float>(p1.x), static_cast<float>(p1.y), static_cast<float>(p1.z)},
                       b3Vec3{static_cast<float>(p2.x), static_cast<float>(p2.y), static_cast<float>(p2.z)},
                       radius);
            for (size_t i = 0; i < w.a.size(); ++i)
            {
                if (!appendPhysicsDebugLine(*out, fromB3(w.a[i]), fromB3(w.b[i])))
                    return;
            }
        }

        void drawTransform(b3WorldTransform xf, void* context)
        {
            auto* out = static_cast<LineMeshData*>(context);
            if (!out)
                return;
            const b3Transform t = xf;
            const b3Vec3      o = t.p;
            const b3Vec3      x = b3TransformPoint(t, b3Vec3{0.25f, 0.0f, 0.0f});
            const b3Vec3      y = b3TransformPoint(t, b3Vec3{0.0f, 0.25f, 0.0f});
            const b3Vec3      z = b3TransformPoint(t, b3Vec3{0.0f, 0.0f, 0.25f});
            appendPhysicsDebugLine(*out, fromB3(o), fromB3(x));
            appendPhysicsDebugLine(*out, fromB3(o), fromB3(y));
            appendPhysicsDebugLine(*out, fromB3(o), fromB3(z));
        }

        void drawString(b3Pos, const char*, b3HexColor, void*)
        {
        }
    } // namespace

    bool appendPhysicsDebugLine(LineMeshData& out, const Math::Vector3f& a, const Math::Vector3f& b)
    {
        if (out.indices.size() / 2 >= kMaxPhysicsDebugLines)
            return false;
        const uint32_t i = static_cast<uint32_t>(out.positions.size());
        out.positions.push_back(a);
        out.positions.push_back(b);
        out.indices.push_back(i);
        out.indices.push_back(i + 1);
        return true;
    }

    void attachPhysicsDebugCallbacks(b3WorldDef& def)
    {
        def.createDebugShape       = &createDebugShape;
        def.destroyDebugShape      = &destroyDebugShape;
        def.userDebugShapeContext  = nullptr;
    }

    void debugDrawPhysicsWorld(b3WorldId worldId, LineMeshData& out)
    {
        if (!b3World_IsValid(worldId))
            return;
        b3DebugDraw draw       = b3DefaultDebugDraw();
        draw.DrawShapeFcn      = &drawShape;
        draw.DrawSegmentFcn    = &drawSegment;
        draw.DrawTransformFcn  = &drawTransform;
        draw.DrawPointFcn      = &drawPoint;
        draw.DrawSphereFcn     = &drawSphere;
        draw.DrawCapsuleFcn    = &drawCapsule;
        draw.DrawBoundsFcn     = &drawBounds;
        draw.DrawBoxFcn        = &drawBox;
        draw.DrawStringFcn     = &drawString;
        draw.drawShapes        = true;
        draw.drawContacts      = true;
        draw.drawBounds        = false;
        draw.drawJoints        = false;
        draw.drawMass          = false;
        draw.context           = &out;
        const float h          = 10000.0f;
        draw.drawingBounds     = {{ -h, -h, -h }, { h, h, h }};
        b3World_Draw(worldId, &draw, UINT64_MAX);
    }
} // namespace Dark::Physics
