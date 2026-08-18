#include "Sprite/SpriteSheet.h"
#include "Assets/AssetManager.h"
#include "Render/Renderer.h"
#include "Core/Log.h"

#include "third_party/nlohmann/json.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <vector>

namespace Dark
{

    using json = nlohmann::json;

    namespace
    {

        uint32_t clampU32(uint32_t v, uint32_t lo, uint32_t hi)
        {
            if (v < lo)
                return lo;
            if (v > hi)
                return hi;
            return v;
        }

        void putPixel(std::vector<uint8_t>& px, uint32_t w, uint32_t h, int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            if (x < 0 || y < 0 || x >= static_cast<int>(w) || y >= static_cast<int>(h))
                return;
            const size_t i = (static_cast<size_t>(y) * w + static_cast<size_t>(x)) * 4u;
            px[i + 0]      = r;
            px[i + 1]      = g;
            px[i + 2]      = b;
            px[i + 3]      = a;
        }

        void fillRect(
            std::vector<uint8_t>& px,
            uint32_t w,
            uint32_t h,
            int x0,
            int y0,
            int x1,
            int y1,
            uint8_t r,
            uint8_t g,
            uint8_t b,
            uint8_t a)
        {
            if (x0 > x1)
                std::swap(x0, x1);
            if (y0 > y1)
                std::swap(y0, y1);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    putPixel(px, w, h, x, y, r, g, b, a);
        }

        void applySheetDesc(const json& j, SpriteSheetDesc& desc)
        {
            desc.columns     = j.value("columns", desc.columns);
            desc.rows        = j.value("rows", desc.rows);
            desc.frameCount  = j.value("frameCount", desc.frameCount);
            desc.frameWidth  = j.value("frameWidth", desc.frameWidth);
            desc.frameHeight = j.value("frameHeight", desc.frameHeight);
            desc.padding     = j.value("padding", desc.padding);
            desc.margin      = j.value("margin", desc.margin);
            desc.inset       = j.value("inset", desc.inset);
        }

        void drawHeroFrame(
            std::vector<uint8_t>& px,
            uint32_t texW,
            uint32_t texH,
            int originX,
            int originY,
            int bob,
            int legL,
            int legR,
            int armL,
            int armR,
            int squash)
        {
            constexpr uint8_t skinR = 236, skinG = 196, skinB = 158;
            constexpr uint8_t shirtR = 42, shirtG = 168, shirtB = 148;
            constexpr uint8_t darkR = 22, darkG = 70, darkB = 64;
            constexpr uint8_t eyeR = 28, eyeG = 28, eyeB = 32;
            constexpr uint8_t pantR = 36, pantG = 58, pantB = 92;

            const int midX = originX + 16;
            int       headT = originY + 4 + bob + squash;
            int       headB = originY + 12 + bob;
            int       bodyT = originY + 12 + bob;
            int       bodyB = originY + 22 + bob - squash;
            if (headB <= headT + 3)
                headB = headT + 4;
            if (bodyB <= bodyT + 4)
                bodyB = bodyT + 5;

            // Head
            fillRect(px, texW, texH, midX - 5, headT, midX + 5, headB, skinR, skinG, skinB, 255);
            fillRect(px, texW, texH, midX - 3, headT + 3, midX - 1, headT + 5, eyeR, eyeG, eyeB, 255);
            fillRect(px, texW, texH, midX + 1, headT + 3, midX + 3, headT + 5, eyeR, eyeG, eyeB, 255);

            // Torso
            fillRect(px, texW, texH, midX - 4, bodyT, midX + 4, bodyB, shirtR, shirtG, shirtB, 255);
            fillRect(px, texW, texH, midX - 4, bodyT, midX + 4, bodyT + 1, darkR, darkG, darkB, 255);

            // Arms
            fillRect(px, texW, texH, midX - 7, bodyT + 1, midX - 4, bodyT + 3, skinR, skinG, skinB, 255);
            fillRect(px, texW, texH, midX + 4, bodyT + 1, midX + 7, bodyT + 3, skinR, skinG, skinB, 255);
            fillRect(px, texW, texH, midX - 7, bodyT + 3, midX - 5, bodyT + 8 + armL, skinR, skinG, skinB, 255);
            fillRect(px, texW, texH, midX + 5, bodyT + 3, midX + 7, bodyT + 8 + armR, skinR, skinG, skinB, 255);

            // Legs
            const int hip = bodyB;
            fillRect(px, texW, texH, midX - 3, hip, midX - 1, hip + 8 + legL, pantR, pantG, pantB, 255);
            fillRect(px, texW, texH, midX + 1, hip, midX + 3, hip + 8 + legR, pantR, pantG, pantB, 255);
            fillRect(px, texW, texH, midX - 4, hip + 7 + legL, midX - 1, hip + 9 + legL, darkR, darkG, darkB, 255);
            fillRect(px, texW, texH, midX + 1, hip + 7 + legR, midX + 4, hip + 9 + legR, darkR, darkG, darkB, 255);
        }

    } // namespace

    bool SpriteSheet::applyDesc(const SpriteSheetDesc& desc)
    {
        m_columns = clampU32(desc.columns, 1, 256);
        m_rows    = clampU32(desc.rows, 1, 256);
        m_padding = desc.padding;
        m_margin  = desc.margin;
        m_inset   = desc.inset;

        const uint32_t cells = m_columns * m_rows;
        m_frameCount         = desc.frameCount == 0 ? cells : desc.frameCount;
        if (m_frameCount > cells)
            m_frameCount = cells;
        if (m_frameCount < 1)
            m_frameCount = 1;

        if (desc.frameWidth > 0)
            m_frameWidth = desc.frameWidth;
        else if (m_texW > 0)
        {
            const uint32_t inner = (m_texW > 2 * m_margin) ? (m_texW - 2 * m_margin) : m_texW;
            const uint32_t gaps  = (m_columns > 0) ? (m_columns - 1) * m_padding : 0;
            m_frameWidth         = (inner > gaps) ? (inner - gaps) / m_columns : m_texW / m_columns;
        }
        else
            m_frameWidth = 1;

        if (desc.frameHeight > 0)
            m_frameHeight = desc.frameHeight;
        else if (m_texH > 0)
        {
            const uint32_t inner = (m_texH > 2 * m_margin) ? (m_texH - 2 * m_margin) : m_texH;
            const uint32_t gaps  = (m_rows > 0) ? (m_rows - 1) * m_padding : 0;
            m_frameHeight        = (inner > gaps) ? (inner - gaps) / m_rows : m_texH / m_rows;
        }
        else
            m_frameHeight = 1;

        if (m_frameWidth < 1)
            m_frameWidth = 1;
        if (m_frameHeight < 1)
            m_frameHeight = 1;
        return m_frameCount > 0;
    }

    bool SpriteSheet::createLayout(uint32_t texWidth, uint32_t texHeight, const SpriteSheetDesc& desc)
    {
        m_texture.reset();
        if (texWidth == 0 || texHeight == 0)
        {
            DE_LOG_ERROR("SpriteSheet: invalid layout size");
            return false;
        }
        m_texW = texWidth;
        m_texH = texHeight;
        return applyDesc(desc);
    }

    bool SpriteSheet::create(std::shared_ptr<Texture2D> texture, const SpriteSheetDesc& desc)
    {
        if (!texture || !texture->valid())
        {
            DE_LOG_ERROR("SpriteSheet: invalid texture");
            return false;
        }
        m_texture = std::move(texture);
        m_texW    = m_texture->width();
        m_texH    = m_texture->height();
        return applyDesc(desc);
    }

    bool SpriteSheet::createProceduralHero(Renderer& renderer)
    {
        constexpr uint32_t kCols   = 8;
        constexpr uint32_t kRows   = 3;
        constexpr uint32_t kFrame  = 32;
        const uint32_t     texW    = kCols * kFrame;
        const uint32_t     texH    = kRows * kFrame;
        std::vector<uint8_t> px(static_cast<size_t>(texW) * texH * 4u, 0);

        auto frameOrigin = [&](uint32_t frame, int& ox, int& oy) {
            const uint32_t col = frame % kCols;
            const uint32_t row = frame / kCols;
            ox                 = static_cast<int>(col * kFrame);
            oy                 = static_cast<int>(row * kFrame);
        };

        // Idle — 4 frames, gentle bob.
        for (uint32_t i = 0; i < 4; ++i)
        {
            int ox = 0, oy = 0;
            frameOrigin(i, ox, oy);
            const int bob = (i == 1 || i == 3) ? -1 : (i == 2 ? -2 : 0);
            drawHeroFrame(px, texW, texH, ox, oy, bob, 0, 0, 0, 0, 0);
        }

        // Run — 8 frames, legs / arms cycle.
        for (uint32_t i = 0; i < 8; ++i)
        {
            int ox = 0, oy = 0;
            frameOrigin(8 + i, ox, oy);
            const float a    = static_cast<float>(i) * 0.78539816f;
            const int   swing = static_cast<int>(std::sinf(a) * 3.0f);
            drawHeroFrame(px, texW, texH, ox, oy, (i & 1) ? -1 : 0, swing, -swing, -swing, swing, 0);
        }

        // Jump — squat, stretch, hang, fall.
        {
            int ox = 0, oy = 0;
            frameOrigin(16, ox, oy);
            drawHeroFrame(px, texW, texH, ox, oy, 2, -1, -1, 1, 1, 2);
            frameOrigin(17, ox, oy);
            drawHeroFrame(px, texW, texH, ox, oy, -2, 2, 2, -3, -3, -1);
            frameOrigin(18, ox, oy);
            drawHeroFrame(px, texW, texH, ox, oy, -1, 1, 1, -2, -2, 0);
            frameOrigin(19, ox, oy);
            drawHeroFrame(px, texW, texH, ox, oy, 0, 2, 2, 2, 2, 0);
        }

        auto tex = std::make_shared<Texture2D>();
        if (!tex->createFromRGBA(renderer, px.data(), texW, texH, texW * 4u))
        {
            DE_LOG_ERROR("SpriteSheet: procedural hero upload failed");
            return false;
        }

        SpriteSheetDesc desc{};
        desc.columns    = kCols;
        desc.rows       = kRows;
        desc.frameCount = 24;
        desc.frameWidth = kFrame;
        desc.frameHeight = kFrame;
        desc.inset      = true;
        if (!create(std::move(tex), desc))
            return false;

        DE_LOG_INFO("SpriteSheet: procedural hero {}x{} ({} frames)", texW, texH, m_frameCount);
        return true;
    }

    SpriteUvRect SpriteSheet::uvForFrame(uint32_t frame) const
    {
        SpriteUvRect uv{};
        if (m_frameCount == 0 || m_texW == 0 || m_texH == 0)
            return uv;

        if (frame >= m_frameCount)
            frame = m_frameCount - 1;

        const uint32_t col = frame % m_columns;
        const uint32_t row = frame / m_columns;
        const float    px0 = static_cast<float>(m_margin + col * (m_frameWidth + m_padding));
        const float    py0 = static_cast<float>(m_margin + row * (m_frameHeight + m_padding));
        float          u0  = px0 / static_cast<float>(m_texW);
        float          v0  = py0 / static_cast<float>(m_texH);
        float          du  = static_cast<float>(m_frameWidth) / static_cast<float>(m_texW);
        float          dv  = static_cast<float>(m_frameHeight) / static_cast<float>(m_texH);

        if (m_inset)
        {
            const float iu = 0.5f / static_cast<float>(m_texW);
            const float iv = 0.5f / static_cast<float>(m_texH);
            u0 += iu;
            v0 += iv;
            du -= 2.0f * iu;
            dv -= 2.0f * iv;
            if (du < iu)
                du = iu;
            if (dv < iv)
                dv = iv;
        }

        uv.u  = u0;
        uv.v  = v0;
        uv.du = du;
        uv.dv = dv;
        return uv;
    }

    std::vector<SpriteClip> defaultHeroClips()
    {
        return {
            { "idle", 0, 4, 6.0f, true },
            { "run", 8, 8, 12.0f, true },
            { "jump", 16, 4, 10.0f, false },
        };
    }

    bool parseSpriteSheetJson(
        const char* jsonText,
        SpriteSheetDesc& outDesc,
        std::string& outTexturePath,
        std::vector<SpriteClip>& outClips)
    {
        outDesc         = SpriteSheetDesc{};
        outTexturePath.clear();
        outClips.clear();
        if (!jsonText || jsonText[0] == '\0')
            return false;

        const json root = json::parse(jsonText, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return false;

        outTexturePath = root.value("texture", std::string{});
        applySheetDesc(root, outDesc);

        if (root.contains("clips") && root["clips"].is_array())
        {
            for (const json& jc : root["clips"])
            {
                if (!jc.is_object())
                    continue;
                SpriteClip clip;
                clip.name       = jc.value("name", std::string{});
                clip.startFrame = jc.value("start", jc.value("startFrame", 0u));
                clip.frameCount = jc.value("count", jc.value("frameCount", 1u));
                clip.fps        = jc.value("fps", 10.0f);
                clip.loop       = jc.value("loop", true);
                clip.texture    = jc.value("texture", outTexturePath);
                clip.desc       = outDesc;
                applySheetDesc(jc, clip.desc);
                if (clip.name.empty() || clip.frameCount < 1)
                    continue;
                outClips.push_back(std::move(clip));
            }
        }
        return outDesc.columns >= 1 && outDesc.rows >= 1;
    }

    bool loadSpriteSheetDescFromJson(
        AssetManager& assets,
        const std::string& virtualJsonPath,
        SpriteSheetDesc& outDesc,
        std::string& outTexturePath,
        std::vector<SpriteClip>& outClips)
    {
        outDesc = SpriteSheetDesc{};
        outTexturePath.clear();
        outClips.clear();

        const std::filesystem::path path = assets.resolve(virtualJsonPath);
        if (path.empty())
            return false;

        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            DE_LOG_ERROR("SpriteSheet: cannot open '{}'", path.string());
            return false;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        if (!parseSpriteSheetJson(ss.str().c_str(), outDesc, outTexturePath, outClips))
        {
            DE_LOG_ERROR("SpriteSheet: invalid json '{}'", path.string());
            return false;
        }
        return true;
    }

    bool loadSpriteSheetFromJson(
        Renderer& renderer,
        AssetManager& assets,
        const std::string& virtualJsonPath,
        SpriteSheet& outSheet,
        std::vector<SpriteClip>& outClips)
    {
        SpriteSheetDesc desc{};
        std::string     texPath;
        if (!loadSpriteSheetDescFromJson(assets, virtualJsonPath, desc, texPath, outClips))
            return false;
        if (texPath.empty())
        {
            DE_LOG_ERROR("SpriteSheet: '{}' missing texture", virtualJsonPath);
            return false;
        }

        auto tex = assets.loadTexture(renderer, texPath);
        if (!tex || !tex->valid())
        {
            DE_LOG_ERROR("SpriteSheet: failed to load texture '{}'", texPath);
            return false;
        }
        if (!outSheet.create(std::move(tex), desc))
            return false;

        DE_LOG_INFO(
            "SpriteSheet: loaded '{}' ({}x{}, {} frames, {} clips)",
            virtualJsonPath,
            outSheet.texWidth(),
            outSheet.texHeight(),
            outSheet.frameCount(),
            outClips.size());
        return true;
    }

    bool loadSpriteSetFromJson(
        Renderer& renderer,
        AssetManager& assets,
        const std::string& virtualJsonPath,
        std::vector<SpriteSheet>& outSheets,
        std::vector<SpriteClip>& outClips)
    {
        outSheets.clear();
        outClips.clear();

        SpriteSheetDesc unusedRoot{};
        std::string     unusedRootTex;
        if (!loadSpriteSheetDescFromJson(assets, virtualJsonPath, unusedRoot, unusedRootTex, outClips))
            return false;
        if (outClips.empty())
        {
            DE_LOG_ERROR("SpriteSheet: '{}' has no clips", virtualJsonPath);
            return false;
        }

        outSheets.resize(outClips.size());
        uint32_t loaded = 0;
        for (size_t i = 0; i < outClips.size(); ++i)
        {
            const SpriteClip& clip = outClips[i];
            if (clip.texture.empty())
            {
                DE_LOG_ERROR("SpriteSheet: clip '{}' in '{}' has no texture", clip.name, virtualJsonPath);
                continue;
            }

            auto tex = assets.loadTexture(renderer, clip.texture);
            if (!tex || !tex->valid())
            {
                DE_LOG_ERROR("SpriteSheet: clip '{}' failed to load '{}'", clip.name, clip.texture);
                continue;
            }
            if (!outSheets[i].create(std::move(tex), clip.desc))
            {
                DE_LOG_ERROR("SpriteSheet: clip '{}' sheet create failed", clip.name);
                continue;
            }
            ++loaded;
            DE_LOG_INFO(
                "SpriteSheet: clip '{}' '{}' ({}x{}, {} frames)",
                clip.name,
                clip.texture,
                outSheets[i].texWidth(),
                outSheets[i].texHeight(),
                outSheets[i].frameCount());
        }

        if (loaded == 0)
        {
            DE_LOG_ERROR("SpriteSheet: '{}' loaded no clip textures", virtualJsonPath);
            outSheets.clear();
            outClips.clear();
            return false;
        }
        return true;
    }

} // namespace Dark
