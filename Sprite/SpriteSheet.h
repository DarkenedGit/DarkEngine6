#pragma once

#include "Render/Texture2D.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Dark
{

    class Renderer;
    class AssetManager;

    struct SpriteUvRect
    {
        float u  = 0.0f;
        float v  = 0.0f;
        float du = 1.0f;
        float dv = 1.0f;
    };

    struct SpriteSheetDesc
    {
        uint32_t columns    = 1;
        uint32_t rows       = 1;
        uint32_t frameCount = 0; // 0 = columns * rows
        uint32_t frameWidth = 0; // 0 = texW / columns
        uint32_t frameHeight = 0;
        uint32_t padding    = 0;
        uint32_t margin     = 0;
        bool     inset      = true; // half-texel inset to reduce bleed
    };

    struct SpriteClip
    {
        std::string     name;
        uint32_t        startFrame = 0;
        uint32_t        frameCount = 1;
        float           fps        = 10.0f;
        bool            loop       = true;
        std::string     texture; // empty = use the file's default sheet
        SpriteSheetDesc desc{};  // per-clip grid; inherits file defaults
    };

    // Grid atlas. Frames are row-major, row 0 at the top of the texture.
    class SpriteSheet
    {
    public:
        SpriteSheet() = default;

        bool create(std::shared_ptr<Texture2D> texture, const SpriteSheetDesc& desc);
        bool createLayout(uint32_t texWidth, uint32_t texHeight, const SpriteSheetDesc& desc);
        bool createProceduralHero(Renderer& renderer);

        SpriteUvRect uvForFrame(uint32_t frame) const;

        uint32_t frameCount() const { return m_frameCount; }
        uint32_t columns() const { return m_columns; }
        uint32_t rows() const { return m_rows; }
        uint32_t frameWidth() const { return m_frameWidth; }
        uint32_t frameHeight() const { return m_frameHeight; }
        uint32_t texWidth() const { return m_texW; }
        uint32_t texHeight() const { return m_texH; }

        bool valid() const { return m_frameCount > 0 && m_texW > 0 && m_texH > 0; }

        Texture2D* texture() const { return m_texture.get(); }
        const std::shared_ptr<Texture2D>& texturePtr() const { return m_texture; }

    private:
        bool applyDesc(const SpriteSheetDesc& desc);

        std::shared_ptr<Texture2D> m_texture;
        uint32_t                   m_texW        = 0;
        uint32_t                   m_texH        = 0;
        uint32_t                   m_columns     = 1;
        uint32_t                   m_rows        = 1;
        uint32_t                   m_frameCount  = 0;
        uint32_t                   m_frameWidth  = 0;
        uint32_t                   m_frameHeight = 0;
        uint32_t                   m_padding     = 0;
        uint32_t                   m_margin      = 0;
        bool                       m_inset       = true;
    };

    std::vector<SpriteClip> defaultHeroClips();

    bool parseSpriteSheetJson(
        const char* jsonText,
        SpriteSheetDesc& outDesc,
        std::string& outTexturePath,
        std::vector<SpriteClip>& outClips);

    bool loadSpriteSheetDescFromJson(
        AssetManager& assets,
        const std::string& virtualJsonPath,
        SpriteSheetDesc& outDesc,
        std::string& outTexturePath,
        std::vector<SpriteClip>& outClips);

    bool loadSpriteSheetFromJson(
        Renderer& renderer,
        AssetManager& assets,
        const std::string& virtualJsonPath,
        SpriteSheet& outSheet,
        std::vector<SpriteClip>& outClips);

    // One SpriteSheet per clip (parallel arrays). Clips may share a GPU texture.
    // Returns true if at least one clip sheet loaded.
    bool loadSpriteSetFromJson(
        Renderer& renderer,
        AssetManager& assets,
        const std::string& virtualJsonPath,
        std::vector<SpriteSheet>& outSheets,
        std::vector<SpriteClip>& outClips);

} // namespace Dark
