#include "Play/DialogueFacts.h"

#include "Core/Log.h"

namespace Dark
{

    int DialogueFactsComponent::get(std::string_view key) const
    {
        for (const auto& fact : facts)
        {
            if (fact.first == key)
                return fact.second;
        }
        return 0;
    }

    bool DialogueFactsComponent::set(std::string_view key, int value)
    {
        for (auto& fact : facts)
        {
            if (fact.first == key)
            {
                fact.second = value;
                return true;
            }
        }
        if (facts.size() >= 64)
        {
            DE_LOG_WARN(LogCategory::AI, "Dialogue: fact cap reached, \"{}\" was not stored", key);
            return false;
        }
        facts.emplace_back(std::string(key), value);
        return true;
    }

    bool DialogueFactsComponent::add(std::string_view key, int delta)
    {
        return set(key, get(key) + delta);
    }

} // namespace Dark
