#pragma once

#include "Math/DarkMath.h"

#include <cstdint>
#include <vector>

namespace Dark
{

    // CPU triangle mesh (SoA). No D3D12 types — upload via Mesh::tryCreate.
    struct MeshData
    {
        std::vector<Math::Vector3f> positions;
        std::vector<Math::Vector3f> normals;
        std::vector<Math::Vector2f> uvs;
        std::vector<uint32_t>       indices;
        // Empty = static. If non-empty, size == positions.size().
        std::vector<uint32_t>       jointPacked; // 4x uint8 in a uint32 (j0 in byte 0)
        std::vector<Math::Vector4f> weights;
    };

    // CPU line-list (pairs). Upload via LineMesh::Create.
    struct LineMeshData
    {
        std::vector<Math::Vector3f> positions;
        std::vector<uint32_t>       indices;
    };

    namespace detail
    {
        inline void pushQuad(MeshData& m, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
        {
            m.indices.push_back(a);
            m.indices.push_back(b);
            m.indices.push_back(c);
            m.indices.push_back(a);
            m.indices.push_back(c);
            m.indices.push_back(d);
        }

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

} // namespace Dark
