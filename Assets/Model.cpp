#include "Assets/Model.h"
#include "Assets/AssetManager.h"
#include "Assets/GltfLoader.h"
#include "Assets/TextureCache.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"
#include "Math/Vector4f.h"
#include "Render/Renderer.h"

#include <cstdio>

namespace Dark
{

    Model::Model()
    {
        type = AssetType::Model;
    }

    bool Model::createFromFile(Renderer& renderer, AssetManager& assets, const std::filesystem::path& path)
    {
        GltfCpuModel cpu;
        if (!parseGltfFile(path, cpu))
            return false;
        return createFromParsed(renderer, assets, cpu, path);
    }

    bool Model::createFromParsed(Renderer& renderer, AssetManager& assets, const GltfCpuModel& cpu, const std::filesystem::path& path)
    {
        m_opaque.clear();
        m_translucent.clear();
        m_bounds = Math::AABox3f::Empty();
        m_skeleton.reset();
        type     = AssetType::Model;

        if (!cpu.skeleton.joints.empty())
            m_skeleton = cpu.skeleton;

        const std::string pathKey = TextureCache::normalizePath(path);
        for (size_t i = 0; i < cpu.primitives.size(); ++i)
        {
            const GltfCpuPrimitive& src = cpu.primitives[i];
            Part part;
            part.localToRoot  = src.localToRoot;
            part.translucent  = src.translucent;
            part.skinned      = src.skinned;
            part.roughness    = src.roughness;
            part.metallic     = src.metallic;
            const bool uploaded = src.skinned ? Mesh::tryCreateSkinned(renderer, src.mesh, part.mesh)
                                              : Mesh::tryCreate(renderer, src.mesh, part.mesh);
            if (!uploaded)
            {
                DE_LOG_ERROR("Model: GPU mesh upload failed for primitive {}", i);
                continue;
            }

            std::shared_ptr<Texture2D> albedo;
            if (!src.albedoFile.empty())
                albedo = assets.textureCache().loadFile(renderer, src.albedoFile);
            else if (!src.albedoBytes.empty())
            {
                char key[256];
                std::snprintf(key, sizeof(key), "gltf:%s:%d", pathKey.c_str(), src.imageIndex);
                albedo = assets.textureCache().loadMemory(renderer, key, src.albedoBytes.data(), src.albedoBytes.size());
            }
            if (!albedo || !albedo->valid())
            {
                const uint8_t r = static_cast<uint8_t>(Math::Clamp(src.baseColor[0], 0.0f, 1.0f) * 255.0f + 0.5f);
                const uint8_t g = static_cast<uint8_t>(Math::Clamp(src.baseColor[1], 0.0f, 1.0f) * 255.0f + 0.5f);
                const uint8_t b = static_cast<uint8_t>(Math::Clamp(src.baseColor[2], 0.0f, 1.0f) * 255.0f + 0.5f);
                const uint8_t a = static_cast<uint8_t>(Math::Clamp(src.baseColor[3], 0.0f, 1.0f) * 255.0f + 0.5f);
                albedo          = assets.loadSolidTexture(renderer, r, g, b, a);
            }

            auto mat = std::make_shared<Material>();
            const bool solidTint = src.albedoFile.empty() && src.albedoBytes.empty();
            const float cr = solidTint ? 1.0f : src.baseColor[0];
            const float cg = solidTint ? 1.0f : src.baseColor[1];
            const float cb = solidTint ? 1.0f : src.baseColor[2];
            const float ca = solidTint ? 1.0f : src.baseColor[3];
            if (!mat->createFromAlbedoTexture(renderer, albedo, cr, cg, cb, ca))
            {
                DE_LOG_ERROR("Model: material create failed for primitive {}", i);
                continue;
            }
            mat->setMetallicRoughness(src.metallic, src.roughness);
            part.material = std::move(mat);

            for (const Math::Vector3f& p : src.mesh.positions)
            {
                const Math::Vector4f wp = part.localToRoot * Math::Vector4f(p.x, p.y, p.z, 1.0f);
                m_bounds.ExpandToInclude(Math::Vector3f(wp.x, wp.y, wp.z));
            }

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
            m_bounds = Math::AABox3f::FromCenterExtents(c, e);
        }
        DE_LOG_INFO("Model: '{}' opaque {} translucent {} joints {}", path.string(), m_opaque.size(), m_translucent.size(), jointCount());
        return true;
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

    void Model::setShadowSrv(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE shadowCpu)
    {
        auto apply = [&](std::vector<Part>& parts) {
            for (Part& p : parts)
            {
                if (p.material)
                    p.material->setShadowSrv(device, shadowCpu);
            }
        };
        apply(m_opaque);
        apply(m_translucent);
    }

} // namespace Dark
