#include "Play/DialogueMemory.h"

#include "Core/Log.h"

#include <algorithm>

namespace Dark
{
    namespace
    {
        bool containsId(const std::vector<std::string>& ids, const std::string& id)
        {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        void pushId(std::vector<std::string>& ids, const std::string& id)
        {
            if (containsId(ids, id))
                return;
            if (ids.size() >= 128)
            {
                DE_LOG_WARN(LogCategory::AI, "Dialogue: heard-line cap reached, \"{}\" was not stored", id);
                return;
            }
            ids.push_back(id);
        }

        float random01(uint32_t& rng)
        {
            rng = rng * 1664525u + 1013904223u;
            return static_cast<float>((rng >> 8) & 0xFFFFFFu) / 16777216.0f;
        }

        bool inBank(const std::vector<DialogueLine>& bank, const std::string& id)
        {
            for (const DialogueLine& line : bank)
            {
                if (line.id == id)
                    return true;
            }
            return false;
        }
    } // namespace

    bool dialogueHeard(const DialogueLineMemory& memory, const std::string& id)
    {
        return containsId(memory.onceHeard, id);
    }

    void rememberDialogueLine(DialogueLineMemory& memory, const DialogueLine& line)
    {
        if (line.once)
            pushId(memory.onceHeard, line.id);
        else
            pushId(memory.cycleHeard, line.id);
    }

    void rememberDialogueChoice(DialogueLineMemory& memory, const std::string& choiceId)
    {
        pushId(memory.onceHeard, choiceId);
    }

    const DialogueLine* pickDialogueLine(DialogueLineMemory& memory, const std::vector<DialogueLine>& bank)
    {
        std::vector<const DialogueLine*> open;
        open.reserve(bank.size());
        for (const DialogueLine& line : bank)
        {
            if (line.once && containsId(memory.onceHeard, line.id))
                continue;
            if (!containsId(memory.cycleHeard, line.id))
                open.push_back(&line);
        }
        std::string previous;
        if (open.empty())
        {
            for (auto it = memory.cycleHeard.rbegin(); it != memory.cycleHeard.rend(); ++it)
            {
                if (inBank(bank, *it))
                {
                    previous = *it;
                    break;
                }
            }
            memory.cycleHeard.erase(std::remove_if(memory.cycleHeard.begin(), memory.cycleHeard.end(),
                                                   [&](const std::string& id) {
                                                       for (const DialogueLine& line : bank)
                                                       {
                                                           if (line.id == id && !line.once)
                                                               return true;
                                                       }
                                                       return false;
                                                   }),
                                    memory.cycleHeard.end());
            for (const DialogueLine& line : bank)
            {
                if (line.once && containsId(memory.onceHeard, line.id))
                    continue;
                open.push_back(&line);
            }
        }
        if (open.empty())
            return nullptr;
        size_t index = static_cast<size_t>(random01(memory.rng) * static_cast<float>(open.size())) % open.size();
        if (open.size() > 1 && open[index]->id == previous)
            index = (index + 1) % open.size();
        return open[index];
    }

    DialogueEntryKind chooseDialogueEntry(bool graphOk, bool dead, bool alert, bool inCombat, bool bookmarkMatches)
    {
        if (!graphOk)
            return DialogueEntryKind::Missing;
        if (dead || alert || inCombat)
            return DialogueEntryKind::Refusal;
        if (bookmarkMatches)
            return DialogueEntryKind::Resume;
        return DialogueEntryKind::Greeting;
    }

} // namespace Dark
