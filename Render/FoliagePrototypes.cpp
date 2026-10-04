#include "Render/FoliagePrototypes.h"

#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Core/Log.h"
#include "Render/GpuUpload.h"
#include "Render/MeshGen.h"
#include "Render/Renderer.h"

#include <filesystem>

namespace Dark
{
    using namespace Math;

    namespace
    {
        const AssetRef<Model>& emptyModel()
        {
            static const AssetRef<Model> kEmpty;
            return kEmpty;
        }

        AssetRef<Model> makeModel(Renderer& renderer, AssetManager& assets, std::vector<Model::Part> parts, const char* cacheKey)
        {
            auto model = std::make_shared<Model>();
            if (!model->createFromParts(std::move(parts)))
            {
                DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: createFromParts failed ({})", cacheKey);
                return {};
            }
            AssetRef<Model> uploaded = registerAndUploadModel(renderer, assets, model, cacheKey);
            if (!uploaded)
                DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: upload failed ({})", cacheKey);
            return uploaded;
        }

        AssetRef<Model> loadOverride(Renderer& renderer, AssetManager& assets, const std::string& path)
        {
            const std::filesystem::path asPath(path);
            if (asPath.is_absolute())
                return loadAndUploadModelFile(renderer, assets, asPath);
            return loadAndUploadModel(renderer, assets, path);
        }
    } // namespace

    bool FoliagePrototypes::create(Renderer& renderer, AssetManager& assets)
    {
        for (int i = 0; i < kKinds; ++i)
        {
            m_proto[i].reset();
            m_override[i].reset();
            m_path[i].clear();
            m_logged[i] = false;
        }

        AssetRef<Material> trunkMat  = internSolidMaterial(assets, 118, 78, 38, 255, "runtime:/foliage/tree-trunk-mat");
        AssetRef<Material> canopyMat = internSolidMaterial(assets, 46, 140, 62, 255, "runtime:/foliage/tree-canopy-mat");
        if (!trunkMat || !canopyMat)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: tree material failed");
            return false;
        }

        MeshData trunkData;
        MeshData canopyData;
        if (!CreateCylinder(trunkData, 1.0f, 1.0f, 1.0f, 16, true, true) || !CreateCone(canopyData, 1.0f, 1.0f, 16, true))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: tree mesh failed");
            return false;
        }

        Model::Part trunk;
        trunk.mesh        = std::move(trunkData);
        trunk.material    = std::move(trunkMat);
        trunk.localToRoot = Matrix4f::ScaleMatrixXYZ(0.5f, 2.0f, 0.5f) * Matrix4f::TranslationMatrix(0.0f, 1.0f, 0.0f);
        trunk.name        = "Trunk";

        Model::Part canopy;
        canopy.mesh        = std::move(canopyData);
        canopy.material    = std::move(canopyMat);
        canopy.localToRoot = Matrix4f::ScaleMatrixXYZ(2.0f, 4.0f, 2.0f) * Matrix4f::TranslationMatrix(0.0f, 4.0f, 0.0f);
        canopy.name        = "Canopy";

        m_proto[static_cast<int>(Terrain::FoliageKind::Tree)] = makeModel(renderer, assets, { std::move(trunk), std::move(canopy) }, "runtime:/foliage/tree");
        if (!m_proto[static_cast<int>(Terrain::FoliageKind::Tree)])
            return false;

        AssetRef<Material> stemMat    = internSolidMaterial(assets, 60, 110, 40, 255, "runtime:/foliage/flower-stem-mat");
        AssetRef<Material> blossomMat = internSolidMaterial(assets, 210, 170, 40, 255, "runtime:/foliage/flower-blossom-mat");
        if (!stemMat || !blossomMat)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: flower material failed");
            return false;
        }

        MeshData stemData;
        MeshData blossomData;
        if (!CreateCylinder(stemData, 0.03f, 0.03f, 0.22f, 6, true, true) || !CreateCone(blossomData, 0.12f, 0.16f, 8, true))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: flower mesh failed");
            return false;
        }

        Model::Part stem;
        stem.mesh        = std::move(stemData);
        stem.material    = std::move(stemMat);
        stem.localToRoot = Matrix4f::TranslationMatrix(0.0f, 0.11f, 0.0f);
        stem.name        = "Stem";

        Model::Part blossom;
        blossom.mesh     = std::move(blossomData);
        blossom.material = std::move(blossomMat);
        // CreateCone is centered. Lift so the base sits on the stem.
        blossom.localToRoot = Matrix4f::TranslationMatrix(0.0f, 0.30f, 0.0f);
        blossom.name        = "Blossom";

        m_proto[static_cast<int>(Terrain::FoliageKind::Flower)] = makeModel(renderer, assets, { std::move(stem), std::move(blossom) }, "runtime:/foliage/flower");
        if (!m_proto[static_cast<int>(Terrain::FoliageKind::Flower)])
            return false;

        AssetRef<Material> rockMat = internSolidMaterial(assets, 140, 140, 138, 255, "runtime:/foliage/rock-mat");
        if (!rockMat)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: rock material failed");
            return false;
        }

        MeshData rockData;
        if (!CreateSphere(rockData, 0.45f, 6, 8))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: rock mesh failed");
            return false;
        }

        Model::Part rock;
        rock.mesh        = std::move(rockData);
        rock.material    = std::move(rockMat);
        rock.localToRoot = Matrix4f::TranslationMatrix(0.0f, 0.45f, 0.0f);
        rock.name        = "Rock";

        m_proto[static_cast<int>(Terrain::FoliageKind::Rock)] = makeModel(renderer, assets, { std::move(rock) }, "runtime:/foliage/rock");
        if (!m_proto[static_cast<int>(Terrain::FoliageKind::Rock)])
            return false;

        DE_LOG_INFO(LogCategory::Render, "FoliagePrototypes: ready");
        return true;
    }

    bool FoliagePrototypes::ready() const
    {
        for (int i = 0; i < kKinds; ++i)
        {
            if (!m_proto[i] || !m_proto[i]->hasOpaque() || m_proto[i]->skinned())
                return false;
        }
        return true;
    }

    void FoliagePrototypes::sync(Renderer& renderer, AssetManager& assets, const Terrain::FoliageDensity& density)
    {
        const std::string* paths[kKinds] = { &density.treeModel, &density.flowerModel, &density.rockModel };
        for (int i = 0; i < kKinds; ++i)
        {
            if (*paths[i] == m_path[i])
                continue;
            m_path[i] = *paths[i];
            m_override[i].reset();
            m_logged[i] = false;
            if (m_path[i].empty())
                continue;

            AssetRef<Model> loaded = loadOverride(renderer, assets, m_path[i]);
            if (!loaded || !loaded->hasOpaque() || loaded->skinned())
            {
                if (!m_logged[i])
                {
                    DE_LOG_WARN(LogCategory::Render, "Foliage: '{}' is missing, has no opaque parts, or is skinned — using the procedural prototype", m_path[i]);
                    m_logged[i] = true;
                }
                continue;
            }
            m_override[i] = std::move(loaded);
        }
    }

    const AssetRef<Model>& FoliagePrototypes::model(Terrain::FoliageKind kind) const
    {
        const int i = static_cast<int>(kind);
        if (i < 0 || i >= kKinds)
            return emptyModel();
        if (m_override[i] && m_override[i]->hasOpaque() && !m_override[i]->skinned())
            return m_override[i];
        if (m_proto[i])
            return m_proto[i];
        return emptyModel();
    }

} // namespace Dark
