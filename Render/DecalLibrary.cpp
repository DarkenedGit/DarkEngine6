#include "Render/DecalLibrary.h"
#include "Render/DecalBasis.h"
#include "Render/Renderer.h"
#include "Core/Log.h"
#include "Math/MathHelper.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace Dark
{

    namespace
    {
        bool createBloodAlbedo(Renderer& renderer, Texture2D& out)
        {
            constexpr uint32_t kSize = static_cast<uint32_t>(kDecalNormalMapSize);
            std::vector<uint8_t> px(static_cast<size_t>(kSize) * kSize * 4u);
            const float cx = (static_cast<float>(kSize) - 1.0f) * 0.5f;
            for (uint32_t y = 0; y < kSize; ++y)
            {
                for (uint32_t x = 0; x < kSize; ++x)
                {
                    const float u = (static_cast<float>(x) - cx) / cx;
                    const float v = (static_cast<float>(y) - cx) / cx;
                    const float r = std::sqrt(u * u + v * v);
                    float blobs = 0.0f;
                    const float drops[5][3] = {
                        { 0.00f, 0.00f, 1.05f },
                        { 0.28f, -0.18f, 0.55f },
                        { -0.32f, 0.22f, 0.50f },
                        { 0.18f, 0.34f, 0.42f },
                        { -0.22f, -0.30f, 0.40f },
                    };
                    for (int i = 0; i < 5; ++i)
                    {
                        const float dx = u - drops[i][0];
                        const float dy = v - drops[i][1];
                        const float d  = std::sqrt(dx * dx + dy * dy) / drops[i][2];
                        float       a  = 1.0f - d;
                        if (a < 0.0f)
                            a = 0.0f;
                        a = a * a * (3.0f - 2.0f * a);
                        blobs = Math::Max(blobs, a);
                    }
                    const float edge = Math::Clamp(1.0f - r, 0.0f, 1.0f);
                    float       a    = blobs * edge;
                    a                = a * a * (3.0f - 2.0f * a);
                    const size_t i   = (static_cast<size_t>(y) * kSize + x) * 4u;
                    px[i + 0]        = 150;
                    px[i + 1]        = 6;
                    px[i + 2]        = 10;
                    px[i + 3]        = static_cast<uint8_t>(a * 255.0f + 0.5f);
                }
            }
            return out.createFromRGBA(renderer, px.data(), kSize, kSize, kSize * 4u, Color::TextureUsage::Albedo);
        }

        bool createWhiteAlbedo(Renderer& renderer, Texture2D& out, DecalDefId alphaId)
        {
            constexpr uint32_t kSize = static_cast<uint32_t>(kDecalNormalMapSize);
            std::vector<uint8_t> px(static_cast<size_t>(kSize) * kSize * 4u, 255);
            if (alphaId == DecalDefId::ImpactBullet || alphaId == DecalDefId::ImpactSlash)
            {
                const float res = static_cast<float>(kSize);
                for (uint32_t y = 0; y < kSize; ++y)
                {
                    for (uint32_t x = 0; x < kSize; ++x)
                    {
                        const float u = (static_cast<float>(x) + 0.5f) / res * 2.0f - 1.0f;
                        const float v = (static_cast<float>(y) + 0.5f) / res * 2.0f - 1.0f;
                        const size_t i = (static_cast<size_t>(y) * kSize + x) * 4u;
                        px[i + 3]      = decalImpactAlpha(alphaId, u, v);
                    }
                }
            }
            return out.createFromRGBA(renderer, px.data(), kSize, kSize, kSize * 4u, Color::TextureUsage::Albedo);
        }

        bool createNormalMap(Renderer& renderer, Texture2D& out, DecalNormalRecipe recipe)
        {
            constexpr uint32_t kSize = static_cast<uint32_t>(kDecalNormalMapSize);
            std::vector<uint8_t> px(static_cast<size_t>(kSize) * kSize * 4u);
            for (uint32_t y = 0; y < kSize; ++y)
            {
                for (uint32_t x = 0; x < kSize; ++x)
                {
                    const DecalBakedNormal n = decalBakeNormalTexel(recipe, static_cast<int>(x), static_cast<int>(y));
                    const size_t i           = (static_cast<size_t>(y) * kSize + x) * 4u;
                    px[i + 0]                = n.r;
                    px[i + 1]                = n.g;
                    px[i + 2]                = n.b;
                    px[i + 3]                = 255;
                }
            }
            return out.createFromRGBA(renderer, px.data(), kSize, kSize, kSize * 4u, Color::TextureUsage::Normal);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE srvOf(const Texture2D* tex)
        {
            if (!tex || !tex->valid())
                return {};
            return tex->cpuHandle();
        }
    } // namespace

    bool DecalLibrary::create(Renderer& renderer)
    {
        destroy(renderer);

        const uint8_t burnPx[4] = { 255, 255, 255, 255 };
        uint8_t flatR = 0;
        uint8_t flatG = 0;
        uint8_t flatB = 0;
        decalWeightZeroNormalBytes(flatR, flatG, flatB);
        const uint8_t flatPx[4] = { flatR, flatG, flatB, 255 };

        const bool ok = createBloodAlbedo(renderer, m_bloodAlbedo)
            && createWhiteAlbedo(renderer, m_footAlbedo, DecalDefId::Footmark)
            && createWhiteAlbedo(renderer, m_bulletAlbedo, DecalDefId::ImpactBullet)
            && createWhiteAlbedo(renderer, m_slashAlbedo, DecalDefId::ImpactSlash)
            && m_burnAlbedo.createFromRGBA(renderer, burnPx, 1, 1, 4, Color::TextureUsage::Albedo)
            && createNormalMap(renderer, m_bloodNormal, DecalNormalRecipe::BloodBump)
            && createNormalMap(renderer, m_footNormal, DecalNormalRecipe::FootDent)
            && createNormalMap(renderer, m_bulletNormal, DecalNormalRecipe::ImpactBullet)
            && createNormalMap(renderer, m_slashNormal, DecalNormalRecipe::ImpactSlash)
            && m_flatNormal.createFromRGBA(renderer, flatPx, 1, 1, 4, Color::TextureUsage::Normal);
        if (!ok)
        {
            DE_LOG_ERROR(LogCategory::Render, "DecalLibrary: texture create failed");
            destroy(renderer);
            return false;
        }

        m_ready = true;
        DE_LOG_INFO(LogCategory::Render, "DecalLibrary: ready");
        return true;
    }

    void DecalLibrary::destroy(Renderer&)
    {
        m_bloodAlbedo  = Texture2D{};
        m_footAlbedo   = Texture2D{};
        m_bulletAlbedo = Texture2D{};
        m_slashAlbedo  = Texture2D{};
        m_burnAlbedo   = Texture2D{};
        m_bloodNormal  = Texture2D{};
        m_footNormal   = Texture2D{};
        m_bulletNormal = Texture2D{};
        m_slashNormal  = Texture2D{};
        m_flatNormal   = Texture2D{};
        m_ready        = false;
    }

    const Texture2D* DecalLibrary::albedoTexture(DecalDefId id) const
    {
        switch (id)
        {
        case DecalDefId::Footmark:
            return &m_footAlbedo;
        case DecalDefId::BloodHit:
        case DecalDefId::BloodDeath:
            return &m_bloodAlbedo;
        case DecalDefId::ImpactBullet:
            return &m_bulletAlbedo;
        case DecalDefId::ImpactSlash:
            return &m_slashAlbedo;
        case DecalDefId::Burn:
            return &m_burnAlbedo;
        case DecalDefId::Count:
            break;
        }
        return nullptr;
    }

    const Texture2D* DecalLibrary::normalTexture(DecalDefId id) const
    {
        switch (id)
        {
        case DecalDefId::Footmark:
            return &m_footNormal;
        case DecalDefId::BloodHit:
            return &m_bloodNormal;
        case DecalDefId::BloodDeath:
        case DecalDefId::Burn:
            return &m_flatNormal;
        case DecalDefId::ImpactBullet:
            return &m_bulletNormal;
        case DecalDefId::ImpactSlash:
            return &m_slashNormal;
        case DecalDefId::Count:
            break;
        }
        return nullptr;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DecalLibrary::albedoSrv(DecalDefId id) const
    {
        return srvOf(albedoTexture(id));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DecalLibrary::normalSrv(DecalDefId id) const
    {
        return srvOf(normalTexture(id));
    }

    DecalDefId decalDefForGpuInstance(const DecalGpuInstance& gpu)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(DecalDefId::Count); ++i)
        {
            const DecalDefId id = static_cast<DecalDefId>(i);
            const DecalDef&  d  = decalDef(id);
            if (gpu.channelMask != d.channelMask)
                continue;
            if (gpu.normalScale != d.normalScale)
                continue;
            if (gpu.roughnessTarget != d.roughnessTarget)
                continue;
            if (gpu.clipLocalYMax != d.clipLocalYMax)
                continue;
            if (gpu.emissiveTarget != d.emissiveTarget)
                continue;
            return id;
        }
        return DecalDefId::Count;
    }

} // namespace Dark
