#include "Terrain/GrassMesh.h"

namespace Dark::Terrain
{
    namespace
    {
        void addJoint(MeshData& out, float t)
        {
            const float half = 0.035f * (1.0f - t) + 0.004f * t;
            out.positions.push_back(Math::Vector3f(-half, t, 0.0f));
            out.positions.push_back(Math::Vector3f(half, t, 0.0f));
            out.normals.push_back(Math::Vector3f(0.0f, 0.0f, 1.0f));
            out.normals.push_back(Math::Vector3f(0.0f, 0.0f, 1.0f));
            out.uvs.push_back(Math::Vector2f(0.0f, t));
            out.uvs.push_back(Math::Vector2f(1.0f, t));
            out.tangents.push_back(Math::Vector4f(1.0f, 0.0f, 0.0f, 1.0f));
            out.tangents.push_back(Math::Vector4f(1.0f, 0.0f, 0.0f, 1.0f));
        }

        void addSegment(MeshData& out, int joint)
        {
            const uint32_t left0  = static_cast<uint32_t>(joint * 2);
            const uint32_t right0 = left0 + 1u;
            const uint32_t left1  = left0 + 2u;
            const uint32_t right1 = left0 + 3u;
            // CCW from +Z, matching FrontCounterClockwise.
            out.indices.push_back(left0);
            out.indices.push_back(right0);
            out.indices.push_back(right1);
            out.indices.push_back(left0);
            out.indices.push_back(right1);
            out.indices.push_back(left1);
        }
    } // namespace

    bool buildGrassBladeMesh(int lod, MeshData& out)
    {
        out = MeshData{};
        if (lod < 0 || lod > 3)
            return false;

        float joints[5]{};
        int   jointCount = 0;
        if (lod == 0)
        {
            joints[0]  = 0.0f;
            joints[1]  = 0.25f;
            joints[2]  = 0.50f;
            joints[3]  = 0.75f;
            joints[4]  = 1.0f;
            jointCount = 5;
        }
        else if (lod == 1)
        {
            joints[0]  = 0.0f;
            joints[1]  = 0.50f;
            joints[2]  = 1.0f;
            jointCount = 3;
        }
        else
        {
            joints[0]  = 0.0f;
            joints[1]  = 1.0f;
            jointCount = 2;
        }

        for (int j = 0; j < jointCount; ++j)
            addJoint(out, joints[j]);
        for (int s = 0; s < jointCount - 1; ++s)
            addSegment(out, s);
        return true;
    }

} // namespace Dark::Terrain
