#include "Assets/Model.h"
#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/Image.h"
#include "Assets/ImageCache.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector4f.h"

#include <memory>
#include <string>

namespace Dark
{

    Model::Model()
    {
        type = AssetType::Model;
    }

    bool Model::createFromFile(AssetManager& assets, const std::filesystem::path& path)
    {
        GltfCpuModel cpu;
        if (!parseGltfFile(path, cpu))
            return false;
        return createFromParsed(assets, cpu, path);
    }

    bool Model::createFromParsed(AssetManager& assets, const GltfCpuModel& cpu, const std::filesystem::path& path)
    {
        m_opaque.clear();
        m_translucent.clear();
        m_bounds = Math::AABox3f::Empty();
        m_skeleton.reset();
        type     = AssetType::Model;

        if (!cpu.skeleton.joints.empty())
            m_skeleton = cpu.skeleton;

        const std::string pathKey = ImageCache::normalizePath(path);
        for (size_t i = 0; i < cpu.primitives.size(); ++i)
        {
            const GltfCpuPrimitive& src = cpu.primitives[i];
            if (src.mesh.positions.empty() || src.mesh.indices.empty())
            {
                DE_LOG_ERROR("Model: empty mesh for primitive {}", i);
                continue;
            }

            Part part;
            part.mesh           = src.mesh;
            part.localToRoot    = src.localToRoot;
            part.translucent    = src.translucent;
            part.skinned        = src.skinned;
            part.materialIndex  = src.materialIndex;
            if (!src.meshName.empty() && !src.materialName.empty())
                part.name = src.meshName + " / " + src.materialName;
            else if (!src.meshName.empty())
                part.name = src.meshName;
            else if (!src.materialName.empty())
                part.name = src.materialName;
            else
                part.name = "Part " + std::to_string(i);

            AssetRef<Image> albedo;
            if (!src.albedoFile.empty())
                albedo = assets.loadImageFile(src.albedoFile);
            else if (!src.albedoBytes.empty())
            {
                const std::string key = ImageCache::gltfKey(pathKey, src.imageIndex);
                albedo                = assets.loadMemoryImage(key, src.albedoBytes.data(), src.albedoBytes.size());
            }
            if (!albedo || !albedo->valid())
            {
                const uint8_t r = static_cast<uint8_t>(Math::Clamp(src.baseColor[0], 0.0f, 1.0f) * 255.0f + 0.5f);
                const uint8_t g = static_cast<uint8_t>(Math::Clamp(src.baseColor[1], 0.0f, 1.0f) * 255.0f + 0.5f);
                const uint8_t b = static_cast<uint8_t>(Math::Clamp(src.baseColor[2], 0.0f, 1.0f) * 255.0f + 0.5f);
                const uint8_t a = static_cast<uint8_t>(Math::Clamp(src.baseColor[3], 0.0f, 1.0f) * 255.0f + 0.5f);
                albedo          = assets.loadSolidImage(r, g, b, a);
            }

            auto mat = std::make_shared<Material>();
            const bool  solidTint = src.albedoFile.empty() && src.albedoBytes.empty();
            const float cr        = solidTint ? 1.0f : src.baseColor[0];
            const float cg        = solidTint ? 1.0f : src.baseColor[1];
            const float cb        = solidTint ? 1.0f : src.baseColor[2];
            const float ca        = solidTint ? 1.0f : src.baseColor[3];
            if (!mat->createFromAlbedoImage(albedo, cr, cg, cb, ca))
            {
                DE_LOG_ERROR("Model: material create failed for primitive {}", i);
                continue;
            }
            mat->setMetallicRoughness(src.metallic, src.roughness);
            mat->setAlphaMode(src.alphaMode);
            const std::string matKey = (src.materialIndex >= 0)
                ? pathKey + "#mat" + std::to_string(src.materialIndex)
                : pathKey + "#prim" + std::to_string(i);
            mat = assets.internMaterial(mat, matKey);
            if (!mat || mat->id == NULL_ASSET)
            {
                DE_LOG_ERROR("Model: internMaterial failed for primitive {}", i);
                continue;
            }
            part.material = std::move(mat);

            expandBoundsFromPart(part);

            if (part.translucent)
                m_translucent.push_back(std::move(part));
            else
                m_opaque.push_back(std::move(part));
        }

        if (!valid())
        {
            DE_LOG_ERROR("Model: no drawable primitives in '{}'", path.string());
            return false;
        }
        if (skinned() && m_bounds.IsValid())
        {
            const Math::Vector3f c = m_bounds.Center();
            const Math::Vector3f e = m_bounds.Extents() * 1.25f;
            m_bounds               = Math::AABox3f::FromCenterExtents(c, e);
        }
        m_sourcePath = path;
        DE_LOG_INFO("Model: '{}' opaque {} translucent {} joints {}", path.string(), m_opaque.size(), m_translucent.size(), jointCount());
        return true;
    }

    bool Model::createFromParts(std::vector<Part> parts)
    {
        m_opaque.clear();
        m_translucent.clear();
        m_bounds = Math::AABox3f::Empty();
        m_skeleton.reset();
        m_animSet.reset();
        type = AssetType::Model;

        for (size_t i = 0; i < parts.size(); ++i)
        {
            Part& part = parts[i];
            if (part.mesh.positions.empty() || part.mesh.indices.empty())
            {
                DE_LOG_ERROR("Model: empty mesh for part {}", i);
                continue;
            }
            expandBoundsFromPart(part);
            if (part.translucent)
                m_translucent.push_back(std::move(part));
            else
                m_opaque.push_back(std::move(part));
        }

        if (!valid())
        {
            DE_LOG_ERROR("Model: no drawable parts");
            return false;
        }
        DE_LOG_INFO("Model: procedural opaque {} translucent {}", m_opaque.size(), m_translucent.size());
        return true;
    }

    void Model::expandBoundsFromPart(const Part& part)
    {
        for (const Math::Vector3f& p : part.mesh.positions)
        {
            const Math::Vector4f wp = part.localToRoot * Math::Vector4f(p.x, p.y, p.z, 1.0f);
            m_bounds.ExpandToInclude(Math::Vector3f(wp.x, wp.y, wp.z));
        }
    }

    uint32_t Model::partCount() const
    {
        return static_cast<uint32_t>(m_opaque.size() + m_translucent.size());
    }

    const Model::Part* Model::partAt(uint32_t index) const
    {
        if (index < m_opaque.size())
            return &m_opaque[index];
        index -= static_cast<uint32_t>(m_opaque.size());
        if (index < m_translucent.size())
            return &m_translucent[index];
        return nullptr;
    }

    void Model::setSourcePath(std::filesystem::path path)
    {
        m_sourcePath = std::move(path);
    }

    bool Model::skinned() const
    {
        for (const Part& p : m_opaque)
        {
            if (p.skinned)
                return true;
        }
        for (const Part& p : m_translucent)
        {
            if (p.skinned)
                return true;
        }
        return false;
    }

    const Skeleton* Model::skeleton() const
    {
        return m_skeleton ? &*m_skeleton : nullptr;
    }

    uint32_t Model::jointCount() const
    {
        return m_skeleton ? static_cast<uint32_t>(m_skeleton->joints.size()) : 0;
    }

    void Model::setAnimationSet(AssetRef<AnimationSet> set)
    {
        m_animSet = std::move(set);
    }

    void Model::setSkeleton(Skeleton skeleton)
    {
        m_skeleton = std::move(skeleton);
    }

    AssetRef<Model> internProceduralModel(AssetManager& assets, MeshData mesh, AssetRef<Material> material, const std::string& cacheKey)
    {
        if (material && material->id == NULL_ASSET)
            material = assets.internMaterial(material);
        if (!material || material->id == NULL_ASSET)
        {
            DE_LOG_ERROR("internProceduralModel: material is not interned");
            return {};
        }
        if (mesh.positions.empty() || mesh.indices.empty())
        {
            DE_LOG_ERROR("internProceduralModel: empty mesh");
            return {};
        }

        Model::Part part;
        part.mesh     = std::move(mesh);
        part.material = std::move(material);

        auto model = std::make_shared<Model>();
        if (!model->createFromParts({ std::move(part) }))
            return {};
        if (assets.registerAsset(model, cacheKey) == NULL_ASSET)
        {
            DE_LOG_ERROR("internProceduralModel: registerAsset failed");
            return {};
        }
        return model;
    }

} // namespace Dark
