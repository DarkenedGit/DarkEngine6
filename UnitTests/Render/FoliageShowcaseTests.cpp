#include <gtest/gtest.h>

#include "Assets/GltfLoader.h"
#include "Core/ContentRoots.h"
#include "Core/Log.h"
#include "Render/FoliagePrototypes.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace Dark;

namespace
{

    std::filesystem::path findContentFile(const char* relativeUnderContent)
    {
        for (const auto& root : contentRootCandidates())
        {
            std::error_code ec;
            const auto      p = root / relativeUnderContent;
            if (std::filesystem::exists(p, ec) && !ec)
                return p;
        }
        return {};
    }

    std::vector<Model::Part> partsFromFile(const char* relativeUnderContent)
    {
        const std::filesystem::path path = findContentFile(relativeUnderContent);
        EXPECT_FALSE(path.empty()) << relativeUnderContent;
        std::vector<Model::Part> parts;
        if (path.empty())
            return parts;

        GltfCpuModel cpu;
        EXPECT_TRUE(parseGltfFile(path, cpu)) << path.string();
        parts.reserve(cpu.primitives.size());
        for (const GltfCpuPrimitive& prim : cpu.primitives)
        {
            Model::Part part;
            part.mesh        = prim.mesh;
            part.localToRoot = prim.localToRoot;
            part.name        = prim.meshName;
            part.skinned     = prim.skinned;
            part.translucent = prim.translucent;
            parts.push_back(std::move(part));
        }
        return parts;
    }

    void measure(const std::vector<Model::Part>& parts, float& minY, float& maxY, float& spanX, float& spanZ)
    {
        bool any = false;
        float minX = 0.0f;
        float maxX = 0.0f;
        float minZ = 0.0f;
        float maxZ = 0.0f;
        minY = 0.0f;
        maxY = 0.0f;
        for (const Model::Part& part : parts)
        {
            for (const Math::Vector3f& p : part.mesh.positions)
            {
                const Math::Vector4f wp = part.localToRoot * Math::Vector4f(p.x, p.y, p.z, 1.0f);
                if (!any)
                {
                    minX = maxX = wp.x;
                    minY = maxY = wp.y;
                    minZ = maxZ = wp.z;
                    any = true;
                    continue;
                }
                if (wp.x < minX) minX = wp.x;
                if (wp.x > maxX) maxX = wp.x;
                if (wp.y < minY) minY = wp.y;
                if (wp.y > maxY) maxY = wp.y;
                if (wp.z < minZ) minZ = wp.z;
                if (wp.z > maxZ) maxZ = wp.z;
            }
        }
        spanX = maxX - minX;
        spanZ = maxZ - minZ;
    }

    void expectGrounded(const std::vector<Model::Part>& kept, const char* label)
    {
        for (const Model::Part& part : kept)
        {
            const Math::Vector4f row = part.localToRoot.GetRow(3);
            EXPECT_NEAR(row.x, 0.0f, 1.0e-3f) << label << " " << part.name;
            EXPECT_NEAR(row.z, 0.0f, 1.0e-3f) << label << " " << part.name;
            EXPECT_EQ(part.name.find("LOD1"), std::string::npos) << part.name;
            EXPECT_EQ(part.name.find("LOD2"), std::string::npos) << part.name;
            EXPECT_EQ(part.name.find("LOD3"), std::string::npos) << part.name;
        }
        float minY = 0.0f;
        float maxY = 0.0f;
        float spanX = 0.0f;
        float spanZ = 0.0f;
        measure(kept, minY, maxY, spanX, spanZ);
        DE_LOG_INFO("FoliageShowcase {}: parts {} height {:.2f} span {:.2f} x {:.2f}", label, kept.size(), maxY - minY, spanX, spanZ);
        EXPECT_NEAR(minY, 0.0f, 0.02f) << label;
        EXPECT_GT(maxY, minY) << label;
    }

} // namespace

TEST(FoliageShowcase, BirchKeepsOneLod0Tree)
{
    const std::vector<Model::Part> parts = partsFromFile("models/BirchTree/scene.gltf");
    ASSERT_GT(parts.size(), 2u);
    std::vector<Model::Part> kept;
    ASSERT_TRUE(makeFoliageShowcaseParts(parts, kept));
    EXPECT_EQ(kept.size(), 2u);
    expectGrounded(kept, "birch");
    float minY = 0.0f;
    float maxY = 0.0f;
    float spanX = 0.0f;
    float spanZ = 0.0f;
    measure(kept, minY, maxY, spanX, spanZ);
    EXPECT_GT(maxY, 4.0f);
    EXPECT_LT(maxY, 40.0f);
}

TEST(FoliageShowcase, DandelionKeepsOriginPlant)
{
    const std::vector<Model::Part> parts = partsFromFile("models/Dandelion/dandelion_01_2k.gltf");
    ASSERT_GT(parts.size(), 1u);
    std::vector<Model::Part> kept;
    ASSERT_TRUE(makeFoliageShowcaseParts(parts, kept));
    EXPECT_EQ(kept.size(), 1u);
    expectGrounded(kept, "dandelion");
    float minY = 0.0f;
    float maxY = 0.0f;
    float spanX = 0.0f;
    float spanZ = 0.0f;
    measure(kept, minY, maxY, spanX, spanZ);
    EXPECT_GT(maxY, 0.05f);
    EXPECT_LT(maxY, 1.0f);
}

TEST(FoliageShowcase, RockKeepsOneBoulder)
{
    const std::vector<Model::Part> parts = partsFromFile("models/RockMoss/rock_moss_set_01_2k.gltf");
    ASSERT_GT(parts.size(), 1u);
    std::vector<Model::Part> kept;
    ASSERT_TRUE(makeFoliageShowcaseParts(parts, kept));
    EXPECT_EQ(kept.size(), 1u);
    expectGrounded(kept, "rock");
    float minY = 0.0f;
    float maxY = 0.0f;
    float spanX = 0.0f;
    float spanZ = 0.0f;
    measure(kept, minY, maxY, spanX, spanZ);
    EXPECT_GT(maxY, 0.2f);
    EXPECT_LT(spanX, 8.0f);
    EXPECT_LT(spanZ, 8.0f);
}
