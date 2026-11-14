#include "Render/FoliagePrototypes.h"

#include "Assets/AssetManager.h"
#include "Assets/Material.h"
#include "Core/Log.h"
#include "Render/GpuUpload.h"
#include "Render/MeshGen.h"
#include "Render/Renderer.h"

#include <filesystem>
#include <string>
#include <vector>

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

        // The grass glTF is a lineup of tufts. Keep the one nearest the origin and
        // drop the showcase translation so each instance is a single plant.
        AssetRef<Model> grassTuft(Renderer& renderer, AssetManager& assets, const AssetRef<Model>& src, const std::string& cacheKey)
        {
            if (!src)
                return {};

            const Model::Part* best = nullptr;
            float              bestD = 0.0f;
            auto consider = [&](const std::vector<Model::Part>& parts) {
                for (const Model::Part& part : parts)
                {
                    if (part.skinned || part.mesh.positions.empty() || part.mesh.indices.empty())
                        continue;
                    const Vector4f row = part.localToRoot.GetRow(3);
                    const float    d   = row.x * row.x + row.z * row.z;
                    if (!best || d < bestD)
                    {
                        best  = &part;
                        bestD = d;
                    }
                }
            };
            consider(src->opaque());
            consider(src->translucent());
            if (!best || !best->material)
                return {};

            Model::Part tuft = *best;
            tuft.localToRoot.SetRow(3, Vector3f(0.0f, 0.0f, 0.0f));
            tuft.translucent = false;
            tuft.skinned     = false;
            if (tuft.material->alphaMode() == MaterialAlphaMode::Blend)
            {
                tuft.material->setAlphaMode(MaterialAlphaMode::Mask);
                tuft.material->setAlphaCutoff(0.5f);
            }

            auto model = std::make_shared<Model>();
            if (!model->createFromParts({ std::move(tuft) }))
            {
                DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: grass tuft build failed");
                return {};
            }
            DE_LOG_INFO(LogCategory::Render, "FoliagePrototypes: grass tuft '{}'", best->name);
            return registerAndUploadModel(renderer, assets, model, cacheKey);
        }

        bool parkedLodName(const std::string& name)
        {
            return name.find("LOD1") != std::string::npos || name.find("LOD2") != std::string::npos || name.find("LOD3") != std::string::npos;
        }

        AssetRef<Model> loadCpuModel(AssetManager& assets, const std::string& path)
        {
            const std::filesystem::path asPath(path);
            if (asPath.is_absolute())
                return assets.loadModelFile(asPath);
            return assets.loadModel(path);
        }
    } // namespace

    bool makeFoliageShowcaseParts(const std::vector<Model::Part>& parts, std::vector<Model::Part>& out)
    {
        out.clear();
        const Model::Part* best = nullptr;
        float              bestD = 0.0f;
        for (const Model::Part& part : parts)
        {
            if (part.skinned || part.mesh.positions.empty() || part.mesh.indices.empty() || parkedLodName(part.name))
                continue;
            const Vector4f row = part.localToRoot.GetRow(3);
            const float    d   = row.x * row.x + row.z * row.z;
            if (!best || d < bestD)
            {
                best  = &part;
                bestD = d;
            }
        }
        if (!best)
            return false;

        const Vector4f origin = best->localToRoot.GetRow(3);
        constexpr float kCluster = 0.25f;
        const float     kClusterSq = kCluster * kCluster;
        for (const Model::Part& part : parts)
        {
            if (part.skinned || part.mesh.positions.empty() || part.mesh.indices.empty() || parkedLodName(part.name))
                continue;
            const Vector4f row = part.localToRoot.GetRow(3);
            const float    dx  = row.x - origin.x;
            const float    dy  = row.y - origin.y;
            const float    dz  = row.z - origin.z;
            if (dx * dx + dy * dy + dz * dz > kClusterSq)
                continue;

            Model::Part copy = part;
            copy.localToRoot.SetRow(3, Vector3f(dx, dy, dz));
            copy.translucent = false;
            copy.skinned     = false;
            if (copy.material && copy.material->alphaMode() == MaterialAlphaMode::Blend)
            {
                copy.material->setAlphaMode(MaterialAlphaMode::Mask);
                copy.material->setAlphaCutoff(0.5f);
            }
            out.push_back(std::move(copy));
        }
        if (out.empty())
            return false;

        float minY = 0.0f;
        bool  any  = false;
        for (const Model::Part& part : out)
        {
            for (const Vector3f& p : part.mesh.positions)
            {
                const Vector4f wp = part.localToRoot * Vector4f(p.x, p.y, p.z, 1.0f);
                if (!any || wp.y < minY)
                    minY = wp.y;
                any = true;
            }
        }
        if (any)
        {
            for (Model::Part& part : out)
            {
                const Vector4f row = part.localToRoot.GetRow(3);
                part.localToRoot.SetRow(3, Vector3f(row.x, row.y - minY, row.z));
            }
        }
        return true;
    }

    bool makeFoliageShowcaseClusters(const std::vector<Model::Part>& parts, std::vector<std::vector<Model::Part>>& out)
    {
        out.clear();
        std::vector<uint8_t> used(parts.size(), 0);
        constexpr float kCluster = 0.25f;
        const float     kClusterSq = kCluster * kCluster;
        auto eligible = [&](size_t index) {
            if (used[index] != 0)
                return false;
            const Model::Part& part = parts[index];
            return !part.skinned && !part.mesh.positions.empty() && !part.mesh.indices.empty() && !parkedLodName(part.name);
        };
        while (true)
        {
            const Model::Part* best = nullptr;
            float              bestD = 0.0f;
            size_t             bestIndex = 0;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (!eligible(i))
                    continue;
                const Vector4f row = parts[i].localToRoot.GetRow(3);
                const float    d   = row.x * row.x + row.z * row.z;
                if (!best || d < bestD)
                {
                    best      = &parts[i];
                    bestD     = d;
                    bestIndex = i;
                }
            }
            if (!best)
                break;
            (void)bestIndex;

            const Vector4f origin = best->localToRoot.GetRow(3);
            std::vector<Model::Part> cluster;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                if (!eligible(i))
                    continue;
                const Vector4f row = parts[i].localToRoot.GetRow(3);
                const float    dx  = row.x - origin.x;
                const float    dy  = row.y - origin.y;
                const float    dz  = row.z - origin.z;
                if (dx * dx + dy * dy + dz * dz > kClusterSq)
                    continue;
                used[i] = 1;
                Model::Part copy = parts[i];
                copy.localToRoot.SetRow(3, Vector3f(dx, dy, dz));
                copy.translucent = false;
                copy.skinned     = false;
                if (copy.material && copy.material->alphaMode() == MaterialAlphaMode::Blend)
                {
                    copy.material->setAlphaMode(MaterialAlphaMode::Mask);
                    copy.material->setAlphaCutoff(0.5f);
                }
                cluster.push_back(std::move(copy));
            }
            if (cluster.empty())
                break;

            float minY = 0.0f;
            bool  any  = false;
            for (const Model::Part& part : cluster)
            {
                for (const Vector3f& p : part.mesh.positions)
                {
                    const Vector4f wp = part.localToRoot * Vector4f(p.x, p.y, p.z, 1.0f);
                    if (!any || wp.y < minY)
                        minY = wp.y;
                    any = true;
                }
            }
            if (any)
            {
                for (Model::Part& part : cluster)
                {
                    const Vector4f row = part.localToRoot.GetRow(3);
                    part.localToRoot.SetRow(3, Vector3f(row.x, row.y - minY, row.z));
                }
            }
            out.push_back(std::move(cluster));
        }
        return !out.empty();
    }

    bool FoliagePrototypes::create(Renderer& renderer, AssetManager& assets)
    {
        for (int i = 0; i < kKinds; ++i)
        {
            m_proto[i].reset();
            m_sig[i].clear();
            m_count[i] = 0;
            for (int v = 0; v < kVariants; ++v)
            {
                m_variant[i][v].reset();
                m_ready[i][v]      = false;
                m_worldScale[i][v] = false;
                m_logged[i][v]     = false;
            }
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

        AssetRef<Material> grassMat = internSolidMaterial(assets, 76, 140, 48, 255, "runtime:/foliage/grass-mat");
        if (!grassMat)
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: grass material failed");
            return false;
        }
        MeshData card;
        if (!CreateQuadXY(card, 0.45f, 0.7f))
        {
            DE_LOG_ERROR(LogCategory::Render, "FoliagePrototypes: grass mesh failed");
            return false;
        }
        Model::Part bladeA;
        bladeA.mesh        = card;
        bladeA.material    = grassMat;
        bladeA.localToRoot = Matrix4f::RotationMatrixY(0.0f) * Matrix4f::TranslationMatrix(0.0f, 0.35f, 0.0f);
        bladeA.name        = "BladeA";
        Model::Part bladeB;
        bladeB.mesh        = card;
        bladeB.material    = grassMat;
        bladeB.localToRoot = Matrix4f::RotationMatrixY(1.57079637f) * Matrix4f::TranslationMatrix(0.0f, 0.35f, 0.0f);
        bladeB.name        = "BladeB";
        m_proto[static_cast<int>(Terrain::FoliageKind::Grass)] = makeModel(renderer, assets, { std::move(bladeA), std::move(bladeB) }, "runtime:/foliage/grass");
        if (!m_proto[static_cast<int>(Terrain::FoliageKind::Grass)])
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
        const std::vector<Terrain::FoliageSpecies>* lists[kKinds] = { &density.trees, &density.flowers, &density.rocks, &density.tufts };
        const std::string* legacy[kKinds] = { &density.treeModel, &density.flowerModel, &density.rockModel, &density.grassModel };
        const char* labels[kKinds] = { "tree", "flower", "rock", "grass" };
        constexpr uint32_t kMaxParts = 8;

        auto warnOnce = [&](int kind, int variant, const std::string& path, const char* why)
        {
            if (m_logged[kind][variant])
                return;
            m_logged[kind][variant] = true;
            if (why != nullptr)
                DE_LOG_WARN(LogCategory::Render, "Foliage: species '{}' dropped ({})", path, why);
            else
                DE_LOG_WARN(LogCategory::Render, "Foliage: '{}' is missing, has no opaque parts, or is skinned — using the procedural prototype", path);
        };

        auto storeParts = [&](int kind, int variant, std::vector<Model::Part> parts, const std::string& path, const std::string& cacheKey, bool worldScale)
        {
            if (parts.size() > kMaxParts)
            {
                warnOnce(kind, variant, path, "more than 8 opaque parts");
                return;
            }
            auto model = std::make_shared<Model>();
            if (!model->createFromParts(std::move(parts)))
            {
                warnOnce(kind, variant, path, nullptr);
                return;
            }
            AssetRef<Model> uploaded = registerAndUploadModel(renderer, assets, model, cacheKey);
            if (!uploaded || !uploaded->hasOpaque() || uploaded->skinned())
            {
                warnOnce(kind, variant, path, nullptr);
                return;
            }
            m_variant[kind][variant]   = std::move(uploaded);
            m_ready[kind][variant]     = true;
            m_worldScale[kind][variant] = worldScale;
        };

        for (int kind = 0; kind < kKinds; ++kind)
        {
            const bool grass = kind == static_cast<int>(Terrain::FoliageKind::Grass);
            const bool legacyTuft = grass && lists[kind]->empty() && !legacy[kind]->empty();
            std::vector<Terrain::FoliageSpecies> use = *lists[kind];
            if (use.empty() && !legacy[kind]->empty())
            {
                Terrain::FoliageSpecies one;
                one.path   = *legacy[kind];
                one.weight = 1.0f;
                use.push_back(std::move(one));
            }
            if (use.size() > static_cast<size_t>(kVariants))
                use.resize(static_cast<size_t>(kVariants));

            std::string sig;
            sig.push_back(legacyTuft ? 'L' : 'S');
            for (const Terrain::FoliageSpecies& species : use)
            {
                sig.push_back('\n');
                sig += species.path;
                sig.push_back(species.worldScale ? '1' : '0');
            }
            if (sig == m_sig[kind])
                continue;
            m_sig[kind] = std::move(sig);
            m_count[kind] = static_cast<uint8_t>(use.size());
            for (int v = 0; v < kVariants; ++v)
            {
                m_variant[kind][v].reset();
                m_ready[kind][v]       = false;
                m_worldScale[kind][v]  = false;
                m_logged[kind][v]      = false;
            }
            if (use.empty())
                continue;

            if (legacyTuft)
            {
                AssetRef<Model> loaded = loadOverride(renderer, assets, use[0].path);
                const std::string tuftKey = std::string("runtime:/foliage/grass-tuft/") + use[0].path;
                if (AssetRef<Model> tuft = grassTuft(renderer, assets, loaded, tuftKey))
                    loaded = std::move(tuft);
                if (!loaded || !loaded->hasOpaque() || loaded->skinned())
                {
                    warnOnce(kind, 0, use[0].path, nullptr);
                    continue;
                }
                m_variant[kind][0]    = std::move(loaded);
                m_ready[kind][0]      = true;
                m_worldScale[kind][0] = use[0].worldScale;
                m_count[kind]         = 1;
                continue;
            }

            const bool expand = grass && use.size() == 1;
            if (expand)
            {
                AssetRef<Model> src = loadCpuModel(assets, use[0].path);
                std::vector<std::vector<Model::Part>> clusters;
                if (src)
                {
                    std::vector<Model::Part> all;
                    all.reserve(src->opaque().size() + src->translucent().size());
                    for (const Model::Part& part : src->opaque())
                        all.push_back(part);
                    for (const Model::Part& part : src->translucent())
                        all.push_back(part);
                    makeFoliageShowcaseClusters(all, clusters);
                }
                if (clusters.empty())
                {
                    warnOnce(kind, 0, use[0].path, nullptr);
                    m_count[kind] = 1;
                    continue;
                }
                if (clusters.size() > static_cast<size_t>(kVariants))
                {
                    DE_LOG_WARN(LogCategory::Render, "Foliage: species '{}' dropped extra clusters past {}", use[0].path, kVariants);
                    clusters.resize(static_cast<size_t>(kVariants));
                }
                m_count[kind] = static_cast<uint8_t>(clusters.size());
                for (size_t v = 0; v < clusters.size(); ++v)
                {
                    const std::string cacheKey = std::string("runtime:/foliage/showcase/") + labels[kind] + "/" + std::to_string(v) + "/" + use[0].path;
                    storeParts(kind, static_cast<int>(v), std::move(clusters[v]), use[0].path, cacheKey, use[0].worldScale);
                }
                continue;
            }

            for (size_t v = 0; v < use.size(); ++v)
            {
                AssetRef<Model> src = loadCpuModel(assets, use[v].path);
                std::vector<std::vector<Model::Part>> clusters;
                if (src)
                {
                    std::vector<Model::Part> all;
                    all.reserve(src->opaque().size() + src->translucent().size());
                    for (const Model::Part& part : src->opaque())
                        all.push_back(part);
                    for (const Model::Part& part : src->translucent())
                        all.push_back(part);
                    makeFoliageShowcaseClusters(all, clusters);
                }
                if (clusters.empty())
                {
                    warnOnce(kind, static_cast<int>(v), use[v].path, nullptr);
                    continue;
                }
                const std::string cacheKey = std::string("runtime:/foliage/showcase/") + labels[kind] + "/" + std::to_string(v) + "/" + use[v].path;
                storeParts(kind, static_cast<int>(v), std::move(clusters[0]), use[v].path, cacheKey, use[v].worldScale);
            }
        }
    }

    const AssetRef<Model>& FoliagePrototypes::model(Terrain::FoliageKind kind) const
    {
        return model(kind, 0);
    }

    const AssetRef<Model>& FoliagePrototypes::model(Terrain::FoliageKind kind, uint8_t variant) const
    {
        const int i = static_cast<int>(kind);
        if (i < 0 || i >= kKinds || variant >= kVariants)
            return emptyModel();
        if (m_ready[i][variant] && m_variant[i][variant] && m_variant[i][variant]->hasOpaque() && !m_variant[i][variant]->skinned())
            return m_variant[i][variant];
        if (variant == 0 && m_proto[i])
            return m_proto[i];
        return emptyModel();
    }

    uint32_t FoliagePrototypes::loadedCount(Terrain::FoliageKind kind) const
    {
        const int i = static_cast<int>(kind);
        if (i < 0 || i >= kKinds)
            return 0;
        return m_count[i];
    }

    bool FoliagePrototypes::variantReady(Terrain::FoliageKind kind, uint8_t variant) const
    {
        const int i = static_cast<int>(kind);
        if (i < 0 || i >= kKinds || variant >= kVariants)
            return false;
        if (m_ready[i][variant])
            return true;
        return variant == 0 && static_cast<bool>(m_proto[i]);
    }

    bool FoliagePrototypes::worldScale(Terrain::FoliageKind kind, uint8_t variant) const
    {
        const int i = static_cast<int>(kind);
        if (i < 0 || i >= kKinds || variant >= kVariants)
            return false;
        return m_worldScale[i][variant];
    }

} // namespace Dark
