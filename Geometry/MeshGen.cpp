#include "Geometry/MeshGen.h"
#include "Core/Log.h"
#include <cmath>
#include <array>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Dark
{
    namespace Geometry
    {
        using namespace Math;

        static void pushIdx(MeshData& m, uint32_t a, uint32_t b, uint32_t c)
        {
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(c);
        }

        // ============================================================
        //  1. SPHERE
        // ============================================================
        bool CreateSphere(MeshData& m, float radius, int stacks, int slices)
        {
            if (stacks < 2)
            {
                //throw std::invalid_argument("stacks must be >= 2");
                DE_LOG_ERROR("stacks must be >= 2");
                return false;
            }
            if (slices < 3)
            {
//                throw std::invalid_argument("slices must be >= 3");
                DE_LOG_ERROR("slices must be >= 3");
                return false;
            }

            const float PI = (float)M_PI;
            for (int i = 0; i <= stacks; ++i)
            {
                float phi    = PI * i / stacks;
                float sinPhi = sinf(phi), cosPhi = cosf(phi);
                for (int j = 0; j <= slices; ++j)
                {
                    float theta = 2.f * PI * j / slices;
                    float cosT = cosf(theta), sinT = sinf(theta);
                    float nx = sinPhi * cosT, ny = cosPhi, nz = sinPhi * sinT;
                    m.positions.push_back({ radius * nx, radius * ny, radius * nz });
                    m.normals.push_back({ nx, ny, nz });
                    m.uvs.push_back({ (float)j / slices, (float)i / stacks });
                }
            }
            int stride = slices + 1;
            for (int i = 0; i < stacks; ++i)
            {
                for (int j = 0; j < slices; ++j)
                {
                    uint32_t a = (uint32_t)(i * stride + j), b = (uint32_t)(i * stride + j + 1);
                    uint32_t c = (uint32_t)((i + 1) * stride + j + 1), d = (uint32_t)((i + 1) * stride + j);
                    if (i != 0)
                        pushIdx(m, a, c, b);
                    if (i != stacks - 1)
                        pushIdx(m, a, d, c);
                }
            }
            return true;
        }

        // ============================================================
        //  2. CONE
        // ============================================================
        bool CreateCone(MeshData& m, float radius, float height, int slices, bool capBase)
        {
            if (slices < 3)
            {
                DE_LOG_ERROR("slices must be >= 3");
                return false;            
            }

            const float halfH = height * .5f;
            const float twoPi = 2.f * (float)M_PI;
            float       len   = sqrtf(radius * radius + height * height);
            float       nR = height / len, nUp = radius / len;

            // Side: each column = 2 verts (base ring, apex)
            for (int i = 0; i <= slices; ++i)
            {
                float t = (float)i / slices, theta = t * twoPi;
                float cosT = cosf(theta), sinT = sinf(theta);
                m.positions.push_back({ radius * cosT, -halfH, radius * sinT });
                m.normals.push_back({ nR * cosT, nUp, nR * sinT });
                m.uvs.push_back({ t, 1.f });
                m.positions.push_back({ 0.f, halfH, 0.f });
                m.normals.push_back({ nR * cosT, nUp, nR * sinT });
                m.uvs.push_back({ t + .5f / slices, 0.f });
            }
            for (int i = 0; i < slices; ++i)
            {
                uint32_t b0 = (uint32_t)(i * 2), a0 = (uint32_t)(i * 2 + 1), b1 = (uint32_t)((i + 1) * 2);
                pushIdx(m, b0, b1, a0);
            }

            if (capBase)
            {
                uint32_t base = (uint32_t)m.positions.size();
                m.positions.push_back({ 0, -halfH, 0 });
                m.normals.push_back({ 0, -1, 0 });
                m.uvs.push_back({ .5f, .5f });
                for (int i = 0; i <= slices; ++i)
                {
                    float theta = (float)i / slices * twoPi;
                    float cosT = cosf(theta), sinT = sinf(theta);
                    m.positions.push_back({ radius * cosT, -halfH, radius * sinT });
                    m.normals.push_back({ 0, -1, 0 });
                    m.uvs.push_back({ .5f + .5f * cosT, .5f + .5f * sinT });
                }
                for (int i = 0; i < slices; ++i)
                    pushIdx(m, base, base + 2 + i, base + 1 + i);
            }
            return true;
        }

        // ============================================================
        //  3. CYLINDER
        // ============================================================
        bool CreateCylinder(MeshData& m, float radiusTop, float radiusBottom, float height, int slices, bool capTop, bool capBottom)
        {
            if (slices < 3)
            {
                DE_LOG_ERROR("slices must be >= 3");
                return false;
            }

            const float halfH = height * .5f, twoPi = 2.f * (float)M_PI;
            float       dr  = radiusBottom - radiusTop;
            float       len = sqrtf(dr * dr + height * height);
            float       nR = height / len, nY = -dr / len;

            for (int i = 0; i <= slices; ++i)
            {
                float t = (float)i / slices, theta = t * twoPi;
                float cosT = cosf(theta), sinT = sinf(theta);
                m.positions.push_back({ radiusBottom * cosT, -halfH, radiusBottom * sinT });
                m.normals.push_back({ nR * cosT, nY, nR * sinT });
                m.uvs.push_back({ t, 0.f });
                m.positions.push_back({ radiusTop * cosT, halfH, radiusTop * sinT });
                m.normals.push_back({ nR * cosT, nY, nR * sinT });
                m.uvs.push_back({ t, 1.f });
            }
            for (int i = 0; i < slices; ++i)
            {
                uint32_t b0 = (uint32_t)(i * 2), t0 = (uint32_t)(i * 2 + 1), b1 = (uint32_t)((i + 1) * 2), t1 = (uint32_t)((i + 1) * 2 + 1);
                pushIdx(m, b0, b1, t1);
                pushIdx(m, b0, t1, t0);
            }

            auto addCap = [&](float r, float y, float ny)
            {
                uint32_t base = (uint32_t)m.positions.size();
                m.positions.push_back({ 0, y, 0 });
                m.normals.push_back({ 0, ny, 0 });
                m.uvs.push_back({ .5f, .5f });
                for (int i = 0; i <= slices; ++i)
                {
                    float theta = (float)i / slices * twoPi;
                    float cosT = cosf(theta), sinT = sinf(theta);
                    m.positions.push_back({ r * cosT, y, r * sinT });
                    m.normals.push_back({ 0, ny, 0 });
                    m.uvs.push_back({ .5f + .5f * cosT, .5f + .5f * sinT });
                }
                for (int i = 0; i < slices; ++i)
                {
                    if (ny > 0)
                        pushIdx(m, base, base + 1 + i, base + 2 + i);
                    else
                        pushIdx(m, base, base + 2 + i, base + 1 + i);
                }
            };
            if (capTop)
                addCap(radiusTop, halfH, 1.f);
            if (capBottom)
                addCap(radiusBottom, -halfH, -1.f);
            return true;
        }

        // ============================================================
        //  Helper: flat-shaded box face
        //  p0..p3 must be ordered so triangles (0,2,1)/(0,3,2) are
        //  front-facing under MeshPipeline (FrontCounterClockwise=TRUE).
        //  uv0..uv3 are D3D UVs (V=0 = top of image).
        // ============================================================
        static void addBoxFace(MeshData& m, Vector3f p0, Vector3f p1, Vector3f p2, Vector3f p3, Vector3f n, Vector2f uv0, Vector2f uv1, Vector2f uv2, Vector2f uv3)
        {
            uint32_t b       = (uint32_t)m.positions.size();
            Vector3f pts[4]  = { p0, p1, p2, p3 };
            Vector2f uvs_[4] = { uv0, uv1, uv2, uv3 };
            for (int i = 0; i < 4; ++i)
            {
                m.positions.push_back(pts[i]);
                m.normals.push_back(n);
                m.uvs.push_back(uvs_[i]);
            }
            pushIdx(m, b + 0, b + 2, b + 1);
            pushIdx(m, b + 0, b + 3, b + 2);
        }

        // ============================================================
        //  4. CUBE
        // ============================================================
        bool CreateCube(MeshData& m, float size)
        {
            return CreateCuboid(m, size, size, size);
        }

        // ============================================================
        //  6. CUBOID
        // ============================================================
        bool CreateCuboid(MeshData& m, float w, float h, float d)
        {
            const float hw = w * .5f, hh = h * .5f, hd = d * .5f;

            // Vertex order kept from the winding that matches the PSO.
            // UVs set so that, viewed from outside with world +Y up on side faces:
            //   bottom-left → (0,1), bottom-right → (1,1),
            //   top-right   → (1,0), top-left     → (0,0)
            // (D3D/WIC: V=0 is the top of the PNG.)

            // +Y top (outside: +Y; "up" on texture → -Z)
            // Walk the quad the opposite way from the side faces. D3D culls in
            // Y-down screen space, so a +Y surface needs CW math winding to be
            // front-facing under FrontCounterClockwise=TRUE.
            addBoxFace(m, { -hw, hh, -hd }, { -hw, hh, hd }, { hw, hh, hd }, { hw, hh, -hd },       { 0, 1, 0 }, { 0, 1 }, { 0, 0 }, { 1, 0 }, { 1, 1 });
            // -Y bottom
            addBoxFace(m, { -hw, -hh, hd }, { hw, -hh, hd }, { hw, -hh, -hd }, { -hw, -hh, -hd },   { 0, -1, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });
            
            // +Z front (outside looking -Z: +X is left in LH view — swap U)
            addBoxFace(m, { -hw, -hh, hd }, { hw, -hh, hd }, { hw, hh, hd }, { -hw, hh, hd },       { 0, 0, 1 }, { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 });
            // -Z back (camera default) — outside looking +Z: +X right
            // verts are BR, BL, TL, TR so UVs are not the naive (0,1)… sequence
            addBoxFace(m, { hw, -hh, -hd }, { -hw, -hh, -hd }, { -hw, hh, -hd }, { hw, hh, -hd },   { 0, 0, -1 }, { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 });

            // +X right (outside looking -X: +Z is left)
            addBoxFace(m, { hw, -hh, hd },  { hw, -hh, -hd }, { hw, hh, -hd }, { hw, hh, hd },       { 1, 0, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 });
            // -X left (outside looking +X: +Z is right)
            addBoxFace(m, { -hw, -hh, -hd }, { -hw, -hh, hd }, { -hw, hh, hd }, { -hw, hh, -hd },   { -1, 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 });

            return true;
        }

        // ============================================================
        //  5. PYRAMID
        // ============================================================
        bool CreatePyramid(MeshData& m, float baseSize, float height)
        {
            float    hb = baseSize * .5f, hh = height * .5f;
            Vector3f BL = { -hb, -hh, -hb }, BR = { hb, -hh, -hb }, FR = { hb, -hh, hb }, FL = { -hb, -hh, hb }, AP = { 0, hh, 0 };

            auto addTri = [&](Vector3f a, Vector3f b, Vector3f c)
            {
                Vector3f n = (b - a).Cross(c - a);
                n.Normalize();
                uint32_t base = (uint32_t)m.positions.size();
                m.positions.push_back(a);
                m.normals.push_back(n);
                m.uvs.push_back({ .5f, 1 });
                m.positions.push_back(b);
                m.normals.push_back(n);
                m.uvs.push_back({ 0, 0 });
                m.positions.push_back(c);
                m.normals.push_back(n);
                m.uvs.push_back({ 1, 0 });
                pushIdx(m, base, base + 1, base + 2);
            };
            addTri(AP, FL, FR);
            addTri(AP, FR, BR);
            addTri(AP, BR, BL);
            addTri(AP, BL, FL);

            uint32_t base = (uint32_t)m.positions.size();
            Vector3f bn   = { 0, -1, 0 };
            m.positions.push_back(BL);
            m.normals.push_back(bn);
            m.uvs.push_back({ 0, 1 });
            m.positions.push_back(BR);
            m.normals.push_back(bn);
            m.uvs.push_back({ 1, 1 });
            m.positions.push_back(FR);
            m.normals.push_back(bn);
            m.uvs.push_back({ 1, 0 });
            m.positions.push_back(FL);
            m.normals.push_back(bn);
            m.uvs.push_back({ 0, 0 });
            pushIdx(m, base + 0, base + 2, base + 1);
            pushIdx(m, base + 0, base + 3, base + 2);
            return true;
        }

        // ============================================================
        //  7. OCTAHEDRON
        // ============================================================
        bool CreateOctahedron(MeshData& m, float radius)
        {
            const float r       = radius;
            Vector3f    V[6]    = { { r, 0, 0 }, { -r, 0, 0 }, { 0, r, 0 }, { 0, -r, 0 }, { 0, 0, r }, { 0, 0, -r } };
            int         F[8][3] = { { 2, 0, 4 }, { 2, 4, 1 }, { 2, 1, 5 }, { 2, 5, 0 }, { 3, 4, 0 }, { 3, 1, 4 }, { 3, 5, 1 }, { 3, 0, 5 } };
            for (auto& f : F)
            {
                Vector3f a = V[f[0]], b = V[f[1]], c = V[f[2]];
                Vector3f n = (b - a).Cross(c - a);
                n.Normalize();
                uint32_t base = (uint32_t)m.positions.size();
                m.positions.push_back(a);
                m.normals.push_back(n);
                m.uvs.push_back({ .5f, 1 });
                m.positions.push_back(b);
                m.normals.push_back(n);
                m.uvs.push_back({ 0, 0 });
                m.positions.push_back(c);
                m.normals.push_back(n);
                m.uvs.push_back({ 1, 0 });
                pushIdx(m, base, base + 1, base + 2);
            }
            return true;
        }

        // ============================================================
        //  8. TRIANGULAR PRISM
        // ============================================================
        bool CreateTriangularPrism(MeshData& m, float baseSize, float height)
        {
            const float hh = height * .5f;
            const float r  = baseSize / sqrtf(3.f);
            Vector3f    bot[3], top_[3];
            for (int i = 0; i < 3; ++i)
            {
                float theta = 2.f * (float)M_PI / 3.f * i - (float)M_PI / 2.f;
                float x = r * cosf(theta), z = r * sinf(theta);
                bot[i]  = Vector3f(x, -hh, z);
                top_[i] = Vector3f(x, hh, z);
            }
            for (int i = 0; i < 3; ++i)
            {
                int      j    = (i + 1) % 3;
                Vector3f p[4] = { bot[i], bot[j], top_[j], top_[i] };
                Vector3f n    = (p[1] - p[0]).Cross(p[2] - p[0]);
                n.Normalize();
                uint32_t base  = (uint32_t)m.positions.size();
                Vector2f uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                for (int k = 0; k < 4; ++k)
                {
                    m.positions.push_back(p[k]);
                    m.normals.push_back(n);
                    m.uvs.push_back(uv[k]);
                }
                pushIdx(m, base + 0, base + 1, base + 2);
                pushIdx(m, base + 0, base + 2, base + 3);
            }
            auto triCap = [&](Vector3f a, Vector3f b, Vector3f c, Vector3f n)
            {
                uint32_t base = (uint32_t)m.positions.size();
                m.positions.push_back(a);
                m.normals.push_back(n);
                m.uvs.push_back({ .5f, 1 });
                m.positions.push_back(b);
                m.normals.push_back(n);
                m.uvs.push_back({ 0, 0 });
                m.positions.push_back(c);
                m.normals.push_back(n);
                m.uvs.push_back({ 1, 0 });
                pushIdx(m, base, base + 1, base + 2);
            };
            triCap(top_[0], top_[1], top_[2], { 0, 1, 0 });
            triCap(bot[0], bot[2], bot[1], { 0, -1, 0 });
            return true;
        }

        // ============================================================
        //  9. DODECAHEDRON
        // ============================================================
        bool CreateDodecahedron(MeshData& m, float radius)
        {
            const float phi  = (1.f + sqrtf(5.f)) * .5f;
            const float iphi = 1.f / phi;
            const float a = 1.f, b_ = iphi, c_ = phi;

            // 20 vertices
            const Vector3f V[20] = { { a, a, a },      { a, a, -a },      { a, -a, a },     { a, -a, -a },    { -a, a, a },      { -a, a, -a },    { -a, -a, a },
                                     { -a, -a, -a },   { 0.f, c_, b_ },   { 0.f, c_, -b_ }, { 0.f, -c_, b_ }, { 0.f, -c_, -b_ }, { b_, 0.f, c_ },  { -b_, 0.f, c_ },
                                     { b_, 0.f, -c_ }, { -b_, 0.f, -c_ }, { c_, b_, 0.f },  { c_, -b_, 0.f }, { -c_, b_, 0.f },  { -c_, -b_, 0.f } };
            // 12 pentagonal faces – CCW outward (Three.js / standard reference)
            const int F[12][5] = { { 3, 11, 7, 15, 2 }, { 3, 2, 16, 17, 1 }, { 3, 1, 14, 9, 5 },   { 3, 5, 19, 18, 11 }, { 7, 19, 5, 14, 6 },  { 7, 6, 4, 13, 15 },
                                   { 9, 1, 17, 8, 4 },  { 9, 4, 13, 0, 18 }, { 16, 2, 15, 13, 0 }, { 16, 0, 18, 19, 6 }, { 11, 18, 0, 12, 8 }, { 11, 8, 4, 6, 10 } };

            float cr = sqrtf(a * a + b_ * b_ + c_ * c_);
            float sc = radius / cr;

            for (auto& face : F)
            {
                Vector3f cen = { 0, 0, 0 };
                for (int k = 0; k < 5; ++k)
                {
                    cen.x += V[face[k]].x;
                    cen.y += V[face[k]].y;
                    cen.z += V[face[k]].z;
                }
                Vector3f fn = cen;
                fn.Normalize();
                Vector3f v0 = { V[face[0]].x * sc, V[face[0]].y * sc, V[face[0]].z * sc };
                for (int k = 1; k < 4; ++k)
                {
                    Vector3f vk   = { V[face[k]].x * sc, V[face[k]].y * sc, V[face[k]].z * sc };
                    Vector3f vk1  = { V[face[k + 1]].x * sc, V[face[k + 1]].y * sc, V[face[k + 1]].z * sc };
                    uint32_t base = (uint32_t)m.positions.size();
                    m.positions.push_back(v0);
                    m.normals.push_back(fn);
                    m.uvs.push_back({ .5f, 1 });
                    m.positions.push_back(vk);
                    m.normals.push_back(fn);
                    m.uvs.push_back({ 0, 0 });
                    m.positions.push_back(vk1);
                    m.normals.push_back(fn);
                    m.uvs.push_back({ 1, 0 });
                    pushIdx(m, base, base + 1, base + 2);
                }
            }
            return true;
        }

        // ============================================================
        // 10. HEXAGONAL PRISM
        // ============================================================
        bool CreateHexagonalPrism(MeshData& m, float radius, float height, bool capTop, bool capBottom)
        {
            const float hh = height * .5f, twoPi = 2.f * (float)M_PI;
            const int   N = 6;
            for (int i = 0; i < N; ++i)
            {
                float    t0 = twoPi / N * i, t1 = twoPi / N * (i + 1);
                float    c0 = cosf(t0), s0 = sinf(t0), c1 = cosf(t1), s1 = sinf(t1);
                Vector3f p[4] = { { radius * c0, -hh, radius * s0 }, { radius * c1, -hh, radius * s1 }, { radius * c1, hh, radius * s1 }, { radius * c0, hh, radius * s0 } };
                Vector3f n    = (p[1] - p[0]).Cross(p[3] - p[0]);
                n.Normalize();
                uint32_t base  = (uint32_t)m.positions.size();
                Vector2f uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                for (int k = 0; k < 4; ++k)
                {
                    m.positions.push_back(p[k]);
                    m.normals.push_back(n);
                    m.uvs.push_back(uv[k]);
                }
                pushIdx(m, base + 0, base + 1, base + 2);
                pushIdx(m, base + 0, base + 2, base + 3);
            }
            auto addCap = [&](float y, float ny)
            {
                uint32_t base = (uint32_t)m.positions.size();
                m.positions.push_back({ 0, y, 0 });
                m.normals.push_back({ 0, ny, 0 });
                m.uvs.push_back({ .5f, .5f });
                for (int i = 0; i <= N; ++i)
                {
                    float theta = twoPi / N * i, cosT = cosf(theta), sinT = sinf(theta);
                    m.positions.push_back({ radius * cosT, y, radius * sinT });
                    m.normals.push_back({ 0, ny, 0 });
                    m.uvs.push_back({ .5f + .5f * cosT, .5f + .5f * sinT });
                }
                for (int i = 0; i < N; ++i)
                {
                    if (ny > 0)
                        pushIdx(m, base, base + 1 + i, base + 2 + i);
                    else
                        pushIdx(m, base, base + 2 + i, base + 1 + i);
                }
            };
            if (capTop)
                addCap(hh, 1.f);
            if (capBottom)
                addCap(-hh, -1.f);
            return true;
        }

        // ============================================================
        // 11. ELLIPSOID
        // ============================================================
        bool CreateEllipsoid(MeshData& m, float rx, float ry, float rz, int stacks, int slices)
        {
            if (stacks < 2)
            {
                DE_LOG_ERROR("stacks must be >= 2");
                return false;
            }
            if (slices < 3)
            {
                DE_LOG_ERROR("slices must be >= 3");
                return false;
            }

            const float PI = (float)M_PI;
            for (int i = 0; i <= stacks; ++i)
            {
                float phi = PI * i / stacks, sinPhi = sinf(phi), cosPhi = cosf(phi);
                for (int j = 0; j <= slices; ++j)
                {
                    float    theta = 2.f * PI * j / slices, cosT = cosf(theta), sinT = sinf(theta);
                    float    px = rx * sinPhi * cosT, py = ry * cosPhi, pz = rz * sinPhi * sinT;
                    Vector3f n = { px / (rx * rx), py / (ry * ry), pz / (rz * rz) };
                    n.Normalize();
                    m.positions.push_back({ px, py, pz });
                    m.normals.push_back(n);
                    m.uvs.push_back({ (float)j / slices, (float)i / stacks });
                }
            }
            int stride = slices + 1;
            for (int i = 0; i < stacks; ++i)
            {
                for (int j = 0; j < slices; ++j)
                {
                    uint32_t a = (uint32_t)(i * stride + j), b = (uint32_t)(i * stride + j + 1);
                    uint32_t c = (uint32_t)((i + 1) * stride + j + 1), d = (uint32_t)((i + 1) * stride + j);
                    if (i != 0)
                        pushIdx(m, a, c, b);
                    if (i != stacks - 1)
                        pushIdx(m, a, d, c);
                }
            }
            return true;
        }

        // ============================================================
        // 12. TORUS
        // ============================================================
        bool CreateTorus(MeshData& m, float R, float r, int majorSlices, int minorSlices)
        {
            if (majorSlices < 3)
            {
                DE_LOG_ERROR("majorSlices must be >= 3");
                return false;
            }
             
            if (minorSlices < 3)
            {
                DE_LOG_ERROR("minorSlices must be >= 3");
                return false;
            }

            const float twoPi = 2.f * (float)M_PI;
            for (int i = 0; i <= majorSlices; ++i)
            {
                float phi = twoPi * i / majorSlices, cosPhi = cosf(phi), sinPhi = sinf(phi);
                for (int j = 0; j <= minorSlices; ++j)
                {
                    float    theta = twoPi * j / minorSlices, cosT = cosf(theta), sinT = sinf(theta);
                    float    px = (R + r * cosT) * cosPhi, py = r * sinT, pz = (R + r * cosT) * sinPhi;
                    Vector3f n = { cosT * cosPhi, sinT, cosT * sinPhi };
                    n.Normalize();
                    m.positions.push_back({ px, py, pz });
                    m.normals.push_back(n);
                    m.uvs.push_back({ (float)i / majorSlices, (float)j / minorSlices });
                }
            }
            int stride = minorSlices + 1;
            for (int i = 0; i < majorSlices; ++i)
            {
                for (int j = 0; j < minorSlices; ++j)
                {
                    uint32_t a = (uint32_t)(i * stride + j), b = (uint32_t)(i * stride + j + 1);
                    uint32_t c = (uint32_t)((i + 1) * stride + j + 1), d = (uint32_t)((i + 1) * stride + j);
                    pushIdx(m, a, b, c);
                    pushIdx(m, a, c, d);
                }
            }
            return true;
        }

        // ============================================================
        // 13. CAPSULE
        //
        //  Built as three sections joined seamlessly:
        //    - Top hemisphere    (capStacks rings, Y in [cylinderTop .. top])
        //    - Cylinder body     (single quad-strip, Y in [-cylinderTop .. cylinderTop])
        //    - Bottom hemisphere (capStacks rings, Y in [-top .. -cylinderTop])
        //
        //  cylinderTop = (height/2 - radius)  clamped to >= 0
        // ============================================================
        bool CreateCapsule(MeshData& m, float radius, float height, int slices, int capStacks)
        {
            if (slices < 3)
            {
                DE_LOG_ERROR("slices must be >= 3");
                return false;
            }
            
            if (capStacks < 1)
            {
                DE_LOG_ERROR("capStacks must be >= 1");
                return false;
            }

            if (height < 2.f * radius)
                height = 2.f * radius; // degenerate → sphere

            const float PI    = (float)M_PI;
            const float twoPi = 2.f * PI;
            const float halfH = height * .5f;
            const float cylH  = halfH - radius; // half-height of the cylinder section

            // Helper: emit one latitude ring
            auto ring = [&](float y, float sinPhi, float cosPhi)
            {
                // cosPhi is the lateral (XZ) scale; sinPhi is the Y normal component
                for (int j = 0; j <= slices; ++j)
                {
                    float t     = (float)j / slices;
                    float theta = t * twoPi;
                    float cosT = cosf(theta), sinT = sinf(theta);
                    float nx = cosT * cosPhi, ny = sinPhi, nz = sinT * cosPhi;
                    m.positions.push_back({ radius * cosT * cosPhi, y, radius * sinT * cosPhi });
                    m.normals.push_back({ nx, ny, nz });
                    m.uvs.push_back({ t, 0.f }); // V will be patched below
                }
            };

            // We'll collect rings then stitch them; track first index of each ring.
            std::vector<uint32_t> ringStart;

            // --- Top hemisphere  (phi from PI/2 down to 0) ---
            for (int i = 0; i <= capStacks; ++i)
            {
                float phi    = PI * .5f * (1.f - (float)i / capStacks); // PI/2 → 0
                float sinPhi = sinf(phi);
                float cosPhi = cosf(phi);
                float y      = cylH + radius * sinPhi;
                ringStart.push_back((uint32_t)m.positions.size());
                ring(y, sinPhi, cosPhi);
            }

            // --- Bottom hemisphere  (phi from 0 down to -PI/2) ---
            for (int i = 1; i <= capStacks; ++i)
            {
                float phi    = -PI * .5f * (float)i / capStacks; // 0 → -PI/2
                float sinPhi = sinf(phi);
                float cosPhi = cosf(phi);
                float y      = -cylH + radius * sinPhi;
                ringStart.push_back((uint32_t)m.positions.size());
                ring(y, sinPhi, cosPhi);
            }

            // Assign V texture coordinate (0 at bottom pole, 1 at top pole)
            int totalRings = (int)ringStart.size();
            for (int r = 0; r < totalRings; ++r)
            {
                float    v     = 1.f - (float)r / (totalRings - 1);
                uint32_t start = ringStart[r];
                for (int j = 0; j <= slices; ++j)
                    m.uvs[start + j].y = v;
            }

            // Stitch consecutive ring pairs into quads
            for (int r = 0; r < totalRings - 1; ++r)
            {
                uint32_t rA = ringStart[r], rB = ringStart[r + 1];
                for (int j = 0; j < slices; ++j)
                {
                    uint32_t a = rA + j, b = rA + j + 1;
                    uint32_t c = rB + j, d = rB + j + 1;
                    pushIdx(m, a, b, d);
                    pushIdx(m, a, d, c);
                }
            }
            return true;
        }

        // ============================================================
        // 14. ICOSAHEDRON  (with optional geodesic subdivision)
        // ============================================================
        bool CreateIcosahedron(MeshData& m, float radius, int subdivisions)
        {
            if (subdivisions < 0)
            {
                DE_LOG_ERROR("subdivisions must be >= 0");
                return false;
            }

            // Golden ratio
            const float t = (1.f + sqrtf(5.f)) * .5f;
            const float s = 1.f / sqrtf(1.f + t * t); // normalise to unit sphere

            // 12 vertices on a unit sphere
            const Vector3f icoV[12] = { { -s, t * s, 0 },  { s, t * s, 0 },  { -s, -t * s, 0 }, { s, -t * s, 0 }, { 0, -s, t * s },  { 0, s, t * s },
                                        { 0, -s, -t * s }, { 0, s, -t * s }, { t * s, 0, -s },  { t * s, 0, s },  { -t * s, 0, -s }, { -t * s, 0, s } };
            // 20 faces (CCW outward)
            const int icoF[20][3] = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
                                      { 3, 9, 4 },  { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 },  { 3, 8, 9 },   { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 },  { 8, 6, 7 },  { 9, 8, 1 } };

            // Build flat list of triangles as Vector3f (positions on unit sphere)
            std::vector<std::array<Vector3f, 3>> tris;
            tris.reserve(20);
            for (auto& f : icoF)
                tris.push_back({ icoV[f[0]], icoV[f[1]], icoV[f[2]] });

            // Subdivide: each triangle → 4 by splitting edges and projecting to sphere
            for (int sub = 0; sub < subdivisions; ++sub)
            {
                std::vector<std::array<Vector3f, 3>> next;
                next.reserve(tris.size() * 4);
                for (auto& tri : tris)
                {
                    // Midpoints projected onto unit sphere
                    auto mid = [](Vector3f a, Vector3f b) -> Vector3f
                    {
                        Vector3f midpoint = { (a.x + b.x) * .5f, (a.y + b.y) * .5f, (a.z + b.z) * .5f };
                        midpoint.Normalize();
                        return midpoint;
                    };
                    Vector3f m01 = mid(tri[0], tri[1]);
                    Vector3f m12 = mid(tri[1], tri[2]);
                    Vector3f m20 = mid(tri[2], tri[0]);
                    next.push_back({ tri[0], m01, m20 });
                    next.push_back({ tri[1], m12, m01 });
                    next.push_back({ tri[2], m20, m12 });
                    next.push_back({ m01, m12, m20 });
                }
                tris = std::move(next);
            }

            // Emit mesh — flat-shaded for subdivisions==0, smooth otherwise
            if (subdivisions == 0)
            {
                // Flat shaded: unique verts per face
                for (auto& tri : tris)
                {
                    Vector3f a = { tri[0].x * radius, tri[0].y * radius, tri[0].z * radius };
                    Vector3f b = { tri[1].x * radius, tri[1].y * radius, tri[1].z * radius };
                    Vector3f c = { tri[2].x * radius, tri[2].y * radius, tri[2].z * radius };
                    Vector3f n = (b - a).Cross(c - a);
                    n.Normalize();
                    uint32_t base = (uint32_t)m.positions.size();
                    m.positions.push_back(a);
                    m.normals.push_back(n);
                    m.uvs.push_back({ 0.5f, 1.0f });
                    m.positions.push_back(b);
                    m.normals.push_back(n);
                    m.uvs.push_back({ 0.0f, 0.0f });
                    m.positions.push_back(c);
                    m.normals.push_back(n);
                    m.uvs.push_back({ 1.0f, 0.0f });
                    pushIdx(m, base, base + 1, base + 2);
                }
            }
            else
            {
                // Smooth shaded: normal = vertex position on unit sphere
                for (auto& tri : tris)
                {
                    uint32_t base = (uint32_t)m.positions.size();
                    for (int k = 0; k < 3; ++k)
                    {
                        Vector3f n = tri[k]; // already unit length
                        Vector3f p = { n.x * radius, n.y * radius, n.z * radius };
                        // Spherical UVs
                        float u = 0.5f + atan2f(n.z, n.x) / (2.f * (float)M_PI);
                        float v = 0.5f + asinf(n.y) / (float)M_PI;
                        m.positions.push_back(p);
                        m.normals.push_back(n);
                        m.uvs.push_back({ u, v });
                    }
                    pushIdx(m, base, base + 1, base + 2);
                }
            }
            return true;
        }

        // ============================================================
        // 15. SPRING  (helical tube)
        //
        //  The centreline is a helix:
        //    C(t) = ( coilRadius*cos(t), height*t/(2π*coils), coilRadius*sin(t) )
        //  A Frenet-Serret frame is computed analytically and a circle of
        //  tubeRadius is swept around it.
        // ============================================================
        bool CreateSpring(MeshData& m, float coilRadius, float tubeRadius, float coils, float height, int coilSlices, int tubeSlices)
        {
            if (coilSlices < 4)
            {
                DE_LOG_ERROR("coilSlices must be >= 4");
                return false;
            }
            
            if (tubeSlices < 3)
            {
                DE_LOG_ERROR("tubeSlices must be >= 3");
                return false;
            }

            const float PI    = (float)M_PI;
            const float twoPi = 2.f * PI;

            // Total number of steps along the helix
            int   totalSteps = (int)(coils * coilSlices);
            float thetaStep  = twoPi * coils / totalSteps;
            float yPerRad    = height / (twoPi * coils); // vertical rise per radian

            // Precompute spine frames
            // Tangent T, Normal N (points toward helix axis), Binormal B
            // For a circular helix:  T = normalise(dC/dt),  N = -radial direction, B = T×N
            struct Frame
            {
                Vector3f pos, T, N, B;
            };
            std::vector<Frame> spine(totalSteps + 1);

            for (int i = 0; i <= totalSteps; ++i)
            {
                float theta = thetaStep * i;
                float cosT = cosf(theta), sinT = sinf(theta);
                float y = (height * theta) / (twoPi * coils) - height * .5f;

                // Position on helix centreline
                Vector3f pos = { coilRadius * cosT, y, coilRadius * sinT };

                // Tangent: dC/dθ = (-R sinθ, yPerRad, R cosθ), then normalise
                Vector3f tangent = { -coilRadius * sinT, yPerRad, coilRadius * cosT };
                tangent.Normalize();
                // Normal: points inward toward axis (−radial)
                Vector3f radial = { cosT, 0.f, sinT };
                Vector3f normal = { -radial.x, -radial.y, -radial.z };

                // Binormal
                Vector3f binormal = tangent.Cross(normal);
                binormal.Normalize();
                // Re-orthogonalise normal
                normal = binormal.Cross(tangent);
                normal.Normalize();
                spine[i] = { pos, tangent, normal, binormal };
            }

            // Emit tube rings around each spine point
            auto emitRing = [&](const Frame& f, float u)
            {
                for (int j = 0; j <= tubeSlices; ++j)
                {
                    float phi  = twoPi * j / tubeSlices;
                    float cosP = cosf(phi), sinP = sinf(phi);
                    // Offset from centreline in the (N, B) plane
                    Vector3f offset = { tubeRadius * (cosP * f.N.x + sinP * f.B.x), tubeRadius * (cosP * f.N.y + sinP * f.B.y), tubeRadius * (cosP * f.N.z + sinP * f.B.z) };
                    Vector3f pos    = { f.pos.x + offset.x, f.pos.y + offset.y, f.pos.z + offset.z };
                    Vector3f nrm    = offset;
                    nrm.Normalize();
                    m.positions.push_back(pos);
                    m.normals.push_back(nrm);
                    m.uvs.push_back({ u, (float)j / tubeSlices });
                }
            };

            int stride = tubeSlices + 1;
            for (int i = 0; i <= totalSteps; ++i)
            {
                float u = (float)i / totalSteps;
                emitRing(spine[i], u);
            }

            // Stitch rings into quads
            for (int i = 0; i < totalSteps; ++i)
            {
                for (int j = 0; j < tubeSlices; ++j)
                {
                    uint32_t a = (uint32_t)(i * stride + j);
                    uint32_t b = (uint32_t)(i * stride + j + 1);
                    uint32_t c = (uint32_t)((i + 1) * stride + j + 1);
                    uint32_t d = (uint32_t)((i + 1) * stride + j);
                    pushIdx(m, a, b, c);
                    pushIdx(m, a, c, d);
                }
            }
            return true;
        }

        // ============================================================
        // 16. ARCH
        //
        //  The arch is a swept annular sector — imagine a washer cut to
        //  an arc angle and extruded along Z.  Six surfaces are emitted:
        //
        //    Outer curved face  – convex cylinder strip
        //    Inner curved face  – concave cylinder strip
        //    Front flat face    – annular sector at +depth/2
        //    Back  flat face    – annular sector at -depth/2
        //    Left  leg cap      – rectangle at angle 0
        //    Right leg cap      – rectangle at angle arcAngle
        //
        //  Y is up; the arch straddles the origin symmetrically about Y.
        //  For a full semicircle (arcAngle = π) the arch sits with its
        //  feet at Y = 0 and its crown at Y = outerRadius.
        // ============================================================
        bool CreateArch(MeshData& m, float innerRadius, float outerRadius, float depth, float arcAngle, int slices)
        {
            if (slices < 2)
            {
                DE_LOG_ERROR("slices must be >= 2");
                return false;
            }
            
            if (outerRadius <= innerRadius)
            {
                DE_LOG_ERROR("outerRadius must be > innerRadius");
                return false;
            }

            const float halfD = depth * .5f;

            // Arch is centred so that angle=0 is at -X axis and sweeps CCW
            // (i.e. a semicircle goes from (-R,0) up and over to (R,0)).
            // We rotate the start angle so the arch "stands up":
            //   angle 0   → left  foot  ( -sin(0),  cos(0) ) rotated
            // Parameterise: theta goes from startAngle to startAngle + arcAngle
            // With startAngle = π (left side), a π-span gives a standard semicircle.
            const float startAngle = (float)M_PI; // arch opens upward

            // Helper: point on arc at parameter t in [0,1]

            // ── Outer curved face ──────────────────────────────────────
            {
                for (int i = 0; i <= slices; ++i)
                {
                    float t     = (float)i / slices;
                    float theta = startAngle + arcAngle * t;
                    float cosT = cosf(theta), sinT = sinf(theta);
                    // outward normal = radial direction
                    Vector3f n  = { cosT, sinT, 0.f };
                    Vector3f pF = { outerRadius * cosT, outerRadius * sinT, halfD };
                    Vector3f pB = { outerRadius * cosT, outerRadius * sinT, -halfD };
                    m.positions.push_back(pF);
                    m.normals.push_back(n);
                    m.uvs.push_back({ t, 1.f });
                    m.positions.push_back(pB);
                    m.normals.push_back(n);
                    m.uvs.push_back({ t, 0.f });
                }
                int stride = 2;
                for (int i = 0; i < slices; ++i)
                {
                    uint32_t f0 = (uint32_t)(i * stride), b0 = (uint32_t)(i * stride + 1);
                    uint32_t f1 = (uint32_t)((i + 1) * stride), b1 = (uint32_t)((i + 1) * stride + 1);
                    pushIdx(m, f0, f1, b1);
                    pushIdx(m, f0, b1, b0);
                }
            }

            // ── Inner curved face ──────────────────────────────────────
            {
                uint32_t base = (uint32_t)m.positions.size();
                for (int i = 0; i <= slices; ++i)
                {
                    float t     = (float)i / slices;
                    float theta = startAngle + arcAngle * t;
                    float cosT = cosf(theta), sinT = sinf(theta);
                    // inward normal = -radial
                    Vector3f n  = { -cosT, -sinT, 0.f };
                    Vector3f pF = { innerRadius * cosT, innerRadius * sinT, halfD };
                    Vector3f pB = { innerRadius * cosT, innerRadius * sinT, -halfD };
                    m.positions.push_back(pF);
                    m.normals.push_back(n);
                    m.uvs.push_back({ t, 1.f });
                    m.positions.push_back(pB);
                    m.normals.push_back(n);
                    m.uvs.push_back({ t, 0.f });
                }
                int stride = 2;
                for (int i = 0; i < slices; ++i)
                {
                    uint32_t f0 = base + (uint32_t)(i * stride), b0 = base + (uint32_t)(i * stride + 1);
                    uint32_t f1 = base + (uint32_t)((i + 1) * stride), b1 = base + (uint32_t)((i + 1) * stride + 1);
                    // reversed winding — faces inward
                    pushIdx(m, f0, b1, f1);
                    pushIdx(m, f0, b0, b1);
                }
            }

            // ── Front and back flat annular faces ──────────────────────
            auto addFlatFace = [&](float z, Vector3f n)
            {
                uint32_t base = (uint32_t)m.positions.size();
                // Emit outer ring then inner ring; stitch into quads
                for (int i = 0; i <= slices; ++i)
                {
                    float t     = (float)i / slices;
                    float theta = startAngle + arcAngle * t;
                    float cosT = cosf(theta), sinT = sinf(theta);
                    m.positions.push_back({ outerRadius * cosT, outerRadius * sinT, z });
                    m.normals.push_back(n);
                    m.uvs.push_back({ t, 1.f });
                    m.positions.push_back({ innerRadius * cosT, innerRadius * sinT, z });
                    m.normals.push_back(n);
                    m.uvs.push_back({ t, 0.f });
                }
                int stride = 2;
                for (int i = 0; i < slices; ++i)
                {
                    uint32_t o0 = base + (uint32_t)(i * stride), in0 = base + (uint32_t)(i * stride + 1);
                    uint32_t o1 = base + (uint32_t)((i + 1) * stride), in1 = base + (uint32_t)((i + 1) * stride + 1);
                    if (n.z > 0)
                    { // front face — CCW from +Z
                        pushIdx(m, o0, in0, in1);
                        pushIdx(m, o0, in1, o1);
                    }
                    else
                    { // back face — CCW from -Z
                        pushIdx(m, o0, in1, in0);
                        pushIdx(m, o0, o1, in1);
                    }
                }
            };
            addFlatFace(halfD, { 0, 0, 1 });
            addFlatFace(-halfD, { 0, 0, -1 });

            // ── Left and right leg-end caps (flat rectangles) ──────────
            // Each cap is a quad between innerRadius and outerRadius, front to back
            auto addCap = [&](float t, Vector3f n)
            {
                float    theta = startAngle + arcAngle * t;
                float    cosT = cosf(theta), sinT = sinf(theta);
                Vector3f outer = { outerRadius * cosT, outerRadius * sinT, 0 };
                Vector3f inner = { innerRadius * cosT, innerRadius * sinT, 0 };

                uint32_t base = (uint32_t)m.positions.size();
                // 4 corners: outerFront, outerBack, innerBack, innerFront
                Vector3f pts[4]  = { { outer.x, outer.y, halfD }, { outer.x, outer.y, -halfD }, { inner.x, inner.y, -halfD }, { inner.x, inner.y, halfD } };
                Vector2f uvs_[4] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
                for (int k = 0; k < 4; ++k)
                {
                    m.positions.push_back(pts[k]);
                    m.normals.push_back(n);
                    m.uvs.push_back(uvs_[k]);
                }
                pushIdx(m, base + 0, base + 1, base + 2);
                pushIdx(m, base + 0, base + 2, base + 3);
            };

            // Left cap normal: tangent to arc at t=0, pointing outward from arch
            {
                float theta = startAngle;
                // tangent direction along arc at t=0 is perpendicular to radius, pointing into arch
                // outward cap normal = -tangent (points away from arch interior)
                Vector3f n{ sinf(theta), -cosf(theta), 0.f };
                n.Normalize();
                addCap(0.f, n);
            }
            // Right cap normal: tangent at t=1, outward
            {
                float    theta = startAngle + arcAngle;
                Vector3f n{ -sinf(theta), cosf(theta), 0.f };
                n.Normalize();
                addCap(1.f, n);
            }

            return true;
        }

        // ============================================================
        // 17a. XY QUAD
        // ============================================================
        bool CreateQuadXY(MeshData& m, float width, float height)
        {
            if (width < 1.0e-4f)
                width = 1.0e-4f;
            if (height < 1.0e-4f)
                height = 1.0e-4f;

            const float    hx = width * 0.5f;
            const float    hy = height * 0.5f;
            const Vector3f n  = { 0.0f, 0.0f, 1.0f };
            const Vector3f bl = { -hx, -hy, 0.0f };
            const Vector3f br = { hx, -hy, 0.0f };
            const Vector3f tr = { hx, hy, 0.0f };
            const Vector3f tl = { -hx, hy, 0.0f };

            auto pushV = [&](Vector3f p, Vector2f uv)
            {
                m.positions.push_back(p);
                m.normals.push_back(n);
                m.uvs.push_back(uv);
            };
            pushV(bl, { 0.0f, 1.0f });
            pushV(br, { 1.0f, 1.0f });
            pushV(tr, { 1.0f, 0.0f });
            pushV(tl, { 0.0f, 0.0f });
            // Camera2D looks +Z; cull is off on the sprite PSO.
            pushIdx(m, 0, 1, 2);
            pushIdx(m, 0, 2, 3);
            return true;
        }

        // ============================================================
        // 17. GROUND PLANE
        // ============================================================
        bool CreateGroundPlane(MeshData& m, float size, float y, float uvScale)
        {
            const float h = size * 0.5f;
            // CCW when viewed from +Y (outward normal +Y)
            // bl, br, tr, tl in XZ (bl = -X,-Z)
            const Vector3f bl = { -h, y, -h };
            const Vector3f br = { h, y, -h };
            const Vector3f tr = { h, y, h };
            const Vector3f tl = { -h, y, h };
            const Vector3f n  = { 0, 1, 0 };
            const float    u  = uvScale;

            auto pushV = [&](Vector3f p, Vector2f uv)
            {
                m.positions.push_back(p);
                m.normals.push_back(n);
                m.uvs.push_back(uv);
            };
            pushV(bl, { 0, u });
            pushV(br, { u, u });
            pushV(tr, { u, 0 });
            pushV(tl, { 0, 0 });
            // CW in Y-up XZ so the plane is front-facing in D3D Y-down screen space.
            pushIdx(m, 0, 1, 2);
            pushIdx(m, 0, 2, 3);
            return true;
        }

        // ============================================================
        // 18. GRID LINES
        // ============================================================
        bool CreateGridLines(LineMeshData& m, float halfExtent, int divisions, float y)
        {
            if (divisions < 1)
                divisions = 1;

            const float step = (halfExtent * 2.0f) / static_cast<float>(divisions);

            // Lines parallel to Z (vary X)
            for (int i = 0; i <= divisions; ++i)
            {
                const float    x = -halfExtent + step * static_cast<float>(i);
                const uint32_t b = static_cast<uint32_t>(m.positions.size());
                m.positions.push_back({ x, y, -halfExtent });
                m.positions.push_back({ x, y, halfExtent });
                m.indices.push_back(b);
                m.indices.push_back(b + 1);
            }
            // Lines parallel to X (vary Z)
            for (int i = 0; i <= divisions; ++i)
            {
                const float    z = -halfExtent + step * static_cast<float>(i);
                const uint32_t b = static_cast<uint32_t>(m.positions.size());
                m.positions.push_back({ -halfExtent, y, z });
                m.positions.push_back({ halfExtent, y, z });
                m.indices.push_back(b);
                m.indices.push_back(b + 1);
            }
            return true;
        }

        bool CreateGridLinesXY(LineMeshData& m, float x0, float y0, float x1, float y1, float step, float z)
        {
            if (x1 < x0)
                std::swap(x0, x1);
            if (y1 < y0)
                std::swap(y0, y1);
            if (step < 0.05f)
                step = 0.05f;

            for (float x = x0; x <= x1 + 0.5f * step; x += step)
            {
                const uint32_t b = static_cast<uint32_t>(m.positions.size());
                m.positions.push_back({ x, y0, z });
                m.positions.push_back({ x, y1, z });
                m.indices.push_back(b);
                m.indices.push_back(b + 1);
            }
            for (float y = y0; y <= y1 + 0.5f * step; y += step)
            {
                const uint32_t b = static_cast<uint32_t>(m.positions.size());
                m.positions.push_back({ x0, y, z });
                m.positions.push_back({ x1, y, z });
                m.indices.push_back(b);
                m.indices.push_back(b + 1);
            }
            return true;
        }

        bool CreateBoxOutlineXY(LineMeshData& m)
        {
            m.positions.push_back({ -0.5f, -0.5f, 0.0f });
            m.positions.push_back({ 0.5f, -0.5f, 0.0f });
            m.positions.push_back({ 0.5f, 0.5f, 0.0f });
            m.positions.push_back({ -0.5f, 0.5f, 0.0f });
            
            const uint32_t edges[] = { 0, 1, 1, 2, 2, 3, 3, 0 };
            m.indices.assign(edges, edges + 8);
            return true;
        }

    } // namespace Geometry
} // namespace Dark
