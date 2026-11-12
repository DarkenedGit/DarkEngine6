#pragma once

#include "Gameplay/Quest.h"

#include <cstdint>
#include <string>

namespace Dark
{

    // Packed the same way as IM_COL32: 0xAABBGGRR.
    struct QuestBanner
    {
        std::string label;
        std::string text;
        uint32_t    color    = 0xFFFFFFFFu;
        float       timeLeft = 3.5f;
    };

    inline QuestBanner makeQuestBanner(const QuestNoteEvent& note)
    {
        const auto pack = [](uint32_t r, uint32_t g, uint32_t b) { return 0xFF000000u | (b << 16) | (g << 8) | r; };
        QuestBanner banner;
        banner.text = note.text;
        switch (note.kind)
        {
        case QuestNote::Started:
            banner.label = "NEW QUEST";
            banner.color = pack(240, 205, 110);
            break;
        case QuestNote::NewObjective:
            banner.label = "NEW OBJECTIVE";
            banner.color = pack(235, 235, 235);
            break;
        case QuestNote::ObjectiveDone:
            banner.label = "OBJECTIVE COMPLETE";
            banner.color = pack(120, 230, 140);
            break;
        case QuestNote::QuestDone:
            banner.label = "QUEST COMPLETE";
            banner.color = pack(240, 205, 110);
            break;
        }
        return banner;
    }

} // namespace Dark
