#pragma once

#include "Sprite/SpriteSheet.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    // Plays named clips on a SpriteSheet. Does not own the sheet.
    class SpriteAnimator
    {
    public:
        void setSheet(const SpriteSheet* sheet);
        const SpriteSheet* sheet() const { return m_sheet; }

        void clearClips();
        bool addClip(const SpriteClip& clip, const SpriteSheet* sheet = nullptr);
        const SpriteClip* findClip(std::string_view name) const;

        // If the named clip is already current, leave time alone unless restart.
        bool play(std::string_view name, bool restart = false);
        void stop();
        void update(float dt);

        const SpriteSheet* currentSheet() const;
        Texture2D*         currentTexture() const;
        SpriteUvRect       currentUv() const;
        uint32_t           currentFrame() const { return m_frame; }
        uint32_t     clipFrame() const;
        const char*  currentClip() const { return m_clipName.c_str(); }
        bool         isPlaying() const { return m_playing; }
        bool         finished() const { return m_finished; }
        float        time() const { return m_time; }

    private:
        const SpriteClip* activeClip() const;

        struct BoundClip
        {
            SpriteClip         clip;
            const SpriteSheet* sheet = nullptr;
        };

        const SpriteSheet*      m_sheet = nullptr;
        std::vector<BoundClip>  m_clips;
        std::string             m_clipName;
        float                   m_time     = 0.0f;
        uint32_t                m_frame    = 0;
        bool                    m_playing  = false;
        bool                    m_finished = false;
    };

} // namespace Dark
