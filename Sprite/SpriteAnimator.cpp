#include "Sprite/SpriteAnimator.h"

#include <cmath>

namespace Dark
{

    void SpriteAnimator::setSheet(const SpriteSheet* sheet)
    {
        m_sheet = sheet;
        if (m_sheet && m_sheet->valid())
            m_frame = 0;
    }

    void SpriteAnimator::clearClips()
    {
        m_clips.clear();
        m_clipName.clear();
        m_time     = 0.0f;
        m_frame    = 0;
        m_playing  = false;
        m_finished = false;
    }

    bool SpriteAnimator::addClip(const SpriteClip& clip, const SpriteSheet* sheet)
    {
        if (clip.name.empty() || clip.frameCount < 1)
            return false;
        SpriteClip c = clip;
        if (c.fps < 0.01f)
            c.fps = 0.01f;
        for (BoundClip& slot : m_clips)
        {
            if (slot.clip.name == c.name)
            {
                slot.clip  = std::move(c);
                slot.sheet = sheet;
                return true;
            }
        }
        m_clips.push_back({ std::move(c), sheet });
        return true;
    }

    const SpriteClip* SpriteAnimator::findClip(std::string_view name) const
    {
        for (const BoundClip& slot : m_clips)
        {
            if (slot.clip.name == name)
                return &slot.clip;
        }
        return nullptr;
    }

    const SpriteClip* SpriteAnimator::activeClip() const
    {
        return findClip(m_clipName);
    }

    bool SpriteAnimator::play(std::string_view name, bool restart)
    {
        const SpriteClip* clip = findClip(name);
        if (!clip)
            return false;

        if (!restart && m_playing && m_clipName == name)
            return true;

        m_clipName = clip->name;
        m_time     = 0.0f;
        m_playing  = true;
        m_finished = false;
        m_frame    = clip->startFrame;
        return true;
    }

    void SpriteAnimator::stop()
    {
        m_playing  = false;
        m_finished = true;
    }

    uint32_t SpriteAnimator::clipFrame() const
    {
        const SpriteClip* clip = activeClip();
        if (!clip)
            return 0;
        if (m_frame < clip->startFrame)
            return 0;
        return m_frame - clip->startFrame;
    }

    void SpriteAnimator::update(float dt)
    {
        const SpriteClip* clip = activeClip();
        if (!clip || !m_playing)
            return;
        if (dt < 0.0f)
            dt = 0.0f;

        m_time += dt;
        const float framesF = m_time * clip->fps;
        int         local   = static_cast<int>(std::floor(framesF));
        if (local < 0)
            local = 0;

        const int count = static_cast<int>(clip->frameCount);
        if (clip->loop)
        {
            local      = local % count;
            m_finished = false;
        }
        else if (local >= count)
        {
            local      = count - 1;
            m_finished = true;
            m_playing  = false;
        }

        m_frame = clip->startFrame + static_cast<uint32_t>(local);
        const SpriteSheet* sheet = currentSheet();
        if (sheet && sheet->frameCount() > 0 && m_frame >= sheet->frameCount())
            m_frame = sheet->frameCount() - 1;
    }

    const SpriteSheet* SpriteAnimator::currentSheet() const
    {
        for (const BoundClip& slot : m_clips)
        {
            if (slot.clip.name != m_clipName)
                continue;
            if (slot.sheet && slot.sheet->valid())
                return slot.sheet;
            break;
        }
        return m_sheet;
    }

    Texture2D* SpriteAnimator::currentTexture() const
    {
        const SpriteSheet* sheet = currentSheet();
        return sheet ? sheet->texture() : nullptr;
    }

    SpriteUvRect SpriteAnimator::currentUv() const
    {
        const SpriteSheet* sheet = currentSheet();
        if (sheet)
            return sheet->uvForFrame(m_frame);
        return {};
    }

} // namespace Dark
