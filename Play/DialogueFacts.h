#pragma once

#include "ECS/Persist.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Dark
{

    struct DialogueFactsComponent
    {
        static constexpr const char* kTypeName    = "DialogueFacts";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        std::vector<std::pair<std::string, int>> facts;

        int  get(std::string_view key) const;
        bool set(std::string_view key, int value);
        bool add(std::string_view key, int delta);
    };

} // namespace Dark
