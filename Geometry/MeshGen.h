#pragma once

#include "Math/DarkMath.h"
#include <vector>
#include <cmath>
#include <stdexcept>
#include <cstdint>

// ============================================================
//  MeshGen.h  –  Procedural 3-D mesh generation
// ============================================================
//
//  Every generator produces a MeshData struct containing:
//    positions  – Vector3f vertices  (x, y, z)
//    normals    – Vector3f per-vertex normals
//    uvs        – Vector2f texture coordinates
//    indices    – uint32 triangle list (CCW winding)
//
//  All shapes are centred at the origin unless noted.
//  Y is up.
// ============================================================

namespace Dark
{
    namespace Geometry
    {

        // ---- Basic value types ------------------------------------

        struct MeshData
        {
            std::vector<Math::Vector3f> positions;
            std::vector<Math::Vector3f> normals;
            std::vector<Math::Vector2f> uvs;
            std::vector<uint32_t>       indices;
        };

        struct LineMeshData
        {
            std::vector<Math::Vector3f> positions;
            std::vector<uint32_t>       indices; // line list pairs
        };

        // ---- Internal helpers (implementation detail) -------------
        namespace detail
        {
            // Push a flat (shared normal) quad as two triangles
            inline void pushQuad(MeshData& m, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
            {
                m.indices.push_back(a);
                m.indices.push_back(b);
                m.indices.push_back(c);
                m.indices.push_back(a);
                m.indices.push_back(c);
                m.indices.push_back(d);
            }

            // Compute smooth normals by accumulating face normals
            inline void computeSmoothedNormals(MeshData& m)
            {
                m.normals.assign(m.positions.size(), { 0, 0, 0 });
                for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
                {
                    uint32_t       i0 = m.indices[i], i1 = m.indices[i + 1], i2 = m.indices[i + 2];
                    Math::Vector3f e1 = m.positions[i1] - m.positions[i0];
                    Math::Vector3f e2 = m.positions[i2] - m.positions[i0];
                    Math::Vector3f fn = e1.Cross(e2);
                    for (uint32_t idx : { i0, i1, i2 })
                    {
                        m.normals[idx].x += fn.x;
                        m.normals[idx].y += fn.y;
                        m.normals[idx].z += fn.z;
                    }
                }
                for (Math::Vector3f& n : m.normals)
                    n.Normalize();
            }
        } // namespace detail

        // ============================================================
        //  1. SPHERE
        //     radius    – sphere radius
        //     stacks    – latitude subdivisions  (≥ 2)
        //     slices    – longitude subdivisions (≥ 3)
        // ============================================================
        bool CreateSphere(MeshData& mesh, float radius = 1.0f, int stacks = 24, int slices = 48);

        // ============================================================
        //  2. CONE
        //     radius    – base circle radius
        //     height    – total height
        //     slices    – circumference subdivisions
        //     capCenter – include base-cap geometry
        // ============================================================
        bool CreateCone(MeshData& mesh, float radius = 1.0f, float height = 2.0f, int slices = 36, bool capCenter = true);

        // ============================================================
        //  3. CYLINDER
        //     radiusTop / radiusBottom – can be different (truncated cone)
        //     height, slices
        //     capTop / capBottom       – include end-cap geometry
        // ============================================================
        bool CreateCylinder(MeshData& mesh, float radiusTop = 1.0f, float radiusBottom = 1.0f, float height = 2.0f, int slices = 36, bool capTop = true, bool capBottom = true);

        // ============================================================
        //  4. CUBE  (all sides = size)
        // ============================================================
        bool CreateCube(MeshData& mesh, float size = 1.0f);

        // ============================================================
        //  5. PYRAMID  (square base)
        //     baseSize  – side length of square base
        //     height
        // ============================================================
        bool CreatePyramid(MeshData& mesh, float baseSize = 1.0f, float height = 2.0f);

        // ============================================================
        //  6. CUBOID  (box with independent dimensions)
        //     width (X), height (Y), depth (Z)
        // ============================================================
        bool CreateCuboid(MeshData& mesh, float width = 2.0f, float height = 1.0f, float depth = 1.0f);

        // ============================================================
        //  6b. CROSS  (plus-sign: two overlapping cuboids in XY, thickness in Z)
        //     armLength — full span of each bar
        //     armWidth  — bar thickness in the plane
        //     depth     — bar thickness along Z
        // ============================================================
        bool CreateCross(MeshData& mesh, float armLength = 1.0f, float armWidth = 0.28f, float depth = 0.22f);

        // ============================================================
        //  7. OCTAHEDRON  (regular, all 8 equilateral triangular faces)
        //     radius – circumscribed sphere radius
        // ============================================================
        bool CreateOctahedron(MeshData& mesh, float radius = 1.0f);

        // ============================================================
        //  8. TRIANGULAR PRISM
        //     baseSize – side length of equilateral triangular base
        //     height   – length along prism axis (Y)
        // ============================================================
        bool CreateTriangularPrism(MeshData& mesh, float baseSize = 1.0f, float height = 2.0f);

        // ============================================================
        //  9. DODECAHEDRON  (regular, 12 pentagonal faces)
        //     radius – circumscribed sphere radius
        // ============================================================
        bool CreateDodecahedron(MeshData& mesh, float radius = 1.0f);

        // ============================================================
        // 10. HEXAGONAL PRISM
        //     radius – outer radius of hexagonal cross-section
        //     height
        //     capTop / capBottom
        // ============================================================
        bool CreateHexagonalPrism(MeshData& mesh, float radius = 1.0f, float height = 2.0f, bool capTop = true, bool capBottom = true);

        // ============================================================
        // 11. ELLIPSOID
        //     radiusX, radiusY, radiusZ – semi-axes
        //     stacks / slices
        // ============================================================
        bool CreateEllipsoid(MeshData& mesh, float radiusX = 1.5f, float radiusY = 1.0f, float radiusZ = 0.75f, int stacks = 24, int slices = 48);

        // ============================================================
        // 12. TORUS
        //     majorRadius – distance from torus centre to tube centre
        //     minorRadius – tube radius
        //     majorSlices – segments around the large circle
        //     minorSlices – segments around the tube
        // ============================================================
        bool CreateTorus(MeshData& mesh, float majorRadius = 1.0f, float minorRadius = 0.3f, int majorSlices = 48, int minorSlices = 24);

        // ============================================================
        // 13. CAPSULE
        //     radius     – hemisphere + cylinder radius
        //     height     – total height (including both hemispheres)
        //     slices     – longitude subdivisions
        //     capStacks  – latitude subdivisions per hemisphere (≥ 1)
        // ============================================================
        bool CreateCapsule(MeshData& mesh, float radius = 0.5f, float height = 2.0f, int slices = 32, int capStacks = 8);

        // ============================================================
        // 14. ICOSAHEDRON
        //     radius – circumscribed sphere radius
        //     subdivisions – number of subdivision iterations (0 = raw 20 faces,
        //                    1–4 = progressively smoother geodesic sphere)
        // ============================================================
        bool CreateIcosahedron(MeshData& mesh, float radius = 1.0f, int subdivisions = 0);

        // ============================================================
        // 15. SPRING (helical tube)
        //     coilRadius  – distance from spring axis to tube centre
        //     tubeRadius  – radius of the circular tube cross-section
        //     coils       – number of full 360° turns
        //     coilSlices  – segments per coil (resolution along helix)
        //     tubeSlices  – segments around the tube cross-section
        //     height      – total height of the spring along its axis
        // ============================================================
        bool CreateSpring(MeshData& mesh, float coilRadius = 1.0f, float tubeRadius = 0.15f, float coils = 4.0f, float height = 3.0f, int coilSlices = 64, int tubeSlices = 16);

        // ============================================================
        // 16. ARCH
        //     innerRadius  – radius to the inner face of the arch
        //     outerRadius  – radius to the outer face of the arch
        //     depth        – thickness of the arch along its axis (Z)
        //     arcAngle     – angular span in radians (default π = semicircle)
        //     slices       – segments along the arc
        // ============================================================
        bool CreateArch(MeshData& mesh, float innerRadius = 0.6f, float outerRadius = 1.0f, float depth = 0.4f, float arcAngle = 3.14159265f, int slices = 32);

        // ============================================================
        // 17a. XY QUAD (unit square in the XY plane, facing +Z)
        //     width / height — full edge lengths, centred on origin
        // ============================================================
        bool CreateQuadXY(MeshData& mesh, float width = 1.0f, float height = 1.0f);

        // ============================================================
        // 17. GROUND PLANE (XZ, Y-up normal)
        //     size     – edge length of the square
        //     y        – height of the plane
        //     uvScale  – texture tiling
        // ============================================================
        bool CreateGroundPlane(MeshData& mesh, float size = 40.0f, float y = 0.0f, float uvScale = 8.0f);

        // ============================================================
        // 18. GRID LINES (line-list indices; use with LineMesh / LinePipeline)
        //     halfExtent – half-size from origin on X and Z
        //     divisions  – cells along each axis
        //     y          – plane height
        // ============================================================
        bool CreateGridLines(LineMeshData& data, float halfExtent = 20.0f, int divisions = 40, float y = 0.0f);

        // Axis-aligned grid in the XY plane (side-view / 2D).
        bool CreateGridLinesXY(LineMeshData& data, float x0, float y0, float x1, float y1, float step = 1.0f, float z = 0.0f);

        // Unit AABB outline in the XY plane (line list). Scale/translate per box.
        bool CreateBoxOutlineXY(LineMeshData& data);

    } // namespace Geometry

} // namespace Dark
