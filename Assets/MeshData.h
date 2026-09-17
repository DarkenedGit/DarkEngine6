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
        std::vector<Math::Vector4f> tangents; // xyz = tangent, w = bitangent sign (glTF TANGENT)
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

        // Lengyel accumulation; orthonormalize against N. Degenerate UV → (1,0,0,1). False if positions or indices empty.
        inline bool computeTangents(const MeshData& m, std::vector<Math::Vector4f>& out)
        {
            if (m.positions.empty() || m.indices.empty())
                return false;

            const size_t nVerts = m.positions.size();
            out.assign(nVerts, Math::Vector4f(1.0f, 0.0f, 0.0f, 1.0f));

            std::vector<Math::Vector3f> tan1(nVerts, Math::Vector3f(0.0f, 0.0f, 0.0f));
            std::vector<Math::Vector3f> tan2(nVerts, Math::Vector3f(0.0f, 0.0f, 0.0f));

            for (size_t i = 0; i + 2 < m.indices.size(); i += 3)
            {
                const uint32_t i0 = m.indices[i];
                const uint32_t i1 = m.indices[i + 1];
                const uint32_t i2 = m.indices[i + 2];
                if (i0 >= nVerts || i1 >= nVerts || i2 >= nVerts)
                    continue;

                const Math::Vector3f& p0  = m.positions[i0];
                const Math::Vector3f& p1  = m.positions[i1];
                const Math::Vector3f& p2  = m.positions[i2];
                const Math::Vector2f  uv0 = (i0 < m.uvs.size()) ? m.uvs[i0] : Math::Vector2f(0.0f, 0.0f);
                const Math::Vector2f  uv1 = (i1 < m.uvs.size()) ? m.uvs[i1] : Math::Vector2f(0.0f, 0.0f);
                const Math::Vector2f  uv2 = (i2 < m.uvs.size()) ? m.uvs[i2] : Math::Vector2f(0.0f, 0.0f);

                const float x1 = p1.x - p0.x;
                const float x2 = p2.x - p0.x;
                const float y1 = p1.y - p0.y;
                const float y2 = p2.y - p0.y;
                const float z1 = p1.z - p0.z;
                const float z2 = p2.z - p0.z;

                const float s1 = uv1.x - uv0.x;
                const float s2 = uv2.x - uv0.x;
                const float t1 = uv1.y - uv0.y;
                const float t2 = uv2.y - uv0.y;

                const float det = s1 * t2 - s2 * t1;
                if (det > -1.0e-8f && det < 1.0e-8f)
                    continue;

                const float          r    = 1.0f / det;
                const Math::Vector3f sdir((t2 * x1 - t1 * x2) * r, (t2 * y1 - t1 * y2) * r, (t2 * z1 - t1 * z2) * r);
                const Math::Vector3f tdir((s1 * x2 - s2 * x1) * r, (s1 * y2 - s2 * y1) * r, (s1 * z2 - s2 * z1) * r);

                tan1[i0] += sdir;
                tan1[i1] += sdir;
                tan1[i2] += sdir;
                tan2[i0] += tdir;
                tan2[i1] += tdir;
                tan2[i2] += tdir;
            }

            for (size_t a = 0; a < nVerts; ++a)
            {
                const Math::Vector3f  n     = (a < m.normals.size()) ? m.normals[a] : Math::Vector3f(0.0f, 0.0f, 0.0f);
                const Math::Vector3f& t     = tan1[a];
                Math::Vector3f        ortho = t - n * n.Dot(t);
                if (ortho.MagnitudeSqrd() < 1.0e-16f)
                    continue;
                ortho.Normalize();
                const float w = (n.Cross(t).Dot(tan2[a]) < 0.0f) ? -1.0f : 1.0f;
                out[a]        = Math::Vector4f(ortho, w);
            }
            return true;
        }

        inline bool computeTangents(MeshData& m)
        {
            return computeTangents(m, m.tangents);
        }
    } // namespace detail

} // namespace Dark
