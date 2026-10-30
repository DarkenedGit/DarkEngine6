#pragma once

#include "ECS/Persist.h"
#include "Play/DialogueGraph.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Dark
{

    struct DialogueLineMemory
    {
        std::vector<std::string> onceHeard;
        std::vector<std::string> cycleHeard;
        uint32_t                 rng = 0x9E3779B9u;
    };

    enum class DialoguePending : uint8_t
    {
        Line,
        Choice
    };

    struct DialogueBookmark
    {
        bool             valid   = false;
        std::string      graphId;
        std::string      nodeId;
        DialoguePending  pending = DialoguePending::Line;
    };

    struct DialogueMemoryComponent
    {
        static constexpr const char* kTypeName    = "DialogueMemory";
        static constexpr uint16_t    kSaveVersion = 1;
        static const PersistFns      kPersist;

        DialogueLineMemory lines;
        DialogueBookmark   bookmark;
    };

    enum class DialogueEntryKind : uint8_t
    {
        Missing,
        Refusal,
        Resume,
        Greeting
    };

    const DialogueLine* pickDialogueLine(DialogueLineMemory& memory, const std::vector<DialogueLine>& bank);
    bool                dialogueHeard(const DialogueLineMemory& memory, const std::string& id);
    void                rememberDialogueLine(DialogueLineMemory& memory, const DialogueLine& line);
    void                rememberDialogueChoice(DialogueLineMemory& memory, const std::string& choiceId);

    DialogueEntryKind chooseDialogueEntry(bool graphOk, bool dead, bool alert, bool inCombat, bool bookmarkMatches);

} // namespace Dark
