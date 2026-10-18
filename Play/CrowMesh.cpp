#include "Play/Crow.h"

#include "Render/MeshGen.h"

#include <cmath>

namespace Dark
{
namespace
{

    Math::Vector3f rotateZ(const Math::Vector3f& p, float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        return Math::Vector3f{ p.x * c - p.y * s, p.x * s + p.y * c, p.z };
    }

    void appendCuboid(MeshData& dst, float width, float height, float depth, const Math::Vector3f& center, float roll, const Math::Vector3f& pivot)
    {
        MeshData box;
        if (!CreateCuboid(box, width, height, depth))
            return;
        if (box.positions.size() != box.normals.size() || box.uvs.size() != box.positions.size())
            return;

        const uint32_t base = static_cast<uint32_t>(dst.positions.size());
        for (size_t i = 0; i < box.positions.size(); ++i)
        {
            Math::Vector3f p{ box.positions[i].x + center.x, box.positions[i].y + center.y, box.positions[i].z + center.z };
            Math::Vector3f rel{ p.x - pivot.x, p.y - pivot.y, p.z - pivot.z };
            rel = rotateZ(rel, roll);
            p   = Math::Vector3f{ rel.x + pivot.x, rel.y + pivot.y, rel.z + pivot.z };

            Math::Vector3f n = rotateZ(box.normals[i], roll);
            if (n.MagnitudeSqrd() > 1.0e-8f)
                n.Normalize();

            dst.positions.push_back(p);
            dst.normals.push_back(n);
            dst.uvs.push_back(box.uvs[i]);
        }
        for (uint32_t index : box.indices)
            dst.indices.push_back(base + index);
    }

} // namespace

    bool buildCrowMesh(MeshData& body, MeshData& beak, MeshData& eyes, float wingLift)
    {
        body.positions.clear();
        body.normals.clear();
        body.uvs.clear();
        body.indices.clear();
        beak = {};
        eyes = {};

        const Math::Vector3f origin{ 0.0f, 0.0f, 0.0f };
        appendCuboid(body, 0.16f, 0.12f, 0.30f, Math::Vector3f{ 0.0f, 0.11f, 0.0f }, 0.0f, origin);
        appendCuboid(body, 0.10f, 0.09f, 0.11f, Math::Vector3f{ 0.0f, 0.17f, 0.18f }, 0.0f, origin);
        appendCuboid(body, 0.16f, 0.015f, 0.18f, Math::Vector3f{ 0.0f, 0.13f, -0.22f }, 0.0f, origin);

        // Wings pivot at the shoulder so one lift angle poses both sides.
        const Math::Vector3f rightPivot{ 0.08f, 0.15f, 0.02f };
        const Math::Vector3f leftPivot{ -0.08f, 0.15f, 0.02f };
        appendCuboid(body, 0.50f, 0.02f, 0.20f, Math::Vector3f{ 0.33f, 0.15f, 0.02f }, wingLift, rightPivot);
        appendCuboid(body, 0.50f, 0.02f, 0.20f, Math::Vector3f{ -0.33f, 0.15f, 0.02f }, -wingLift, leftPivot);

        appendCuboid(beak, 0.03f, 0.025f, 0.10f, Math::Vector3f{ 0.0f, 0.155f, 0.28f }, 0.0f, origin);
        appendCuboid(eyes, 0.018f, 0.018f, 0.012f, Math::Vector3f{ 0.04f, 0.185f, 0.23f }, 0.0f, origin);
        appendCuboid(eyes, 0.018f, 0.018f, 0.012f, Math::Vector3f{ -0.04f, 0.185f, 0.23f }, 0.0f, origin);

        return !body.positions.empty() && !body.indices.empty() && !beak.positions.empty() && !eyes.positions.empty();
    }

} // namespace Dark
