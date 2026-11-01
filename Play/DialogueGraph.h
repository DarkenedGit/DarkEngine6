#pragma once

#include "Assets/AssetHandle.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    enum class DialogueConditionOp : uint8_t
    {
        Fact = 0,
        QuestActive,
        QuestComplete,
        Objective,
        Item,
        Alertness,
        Health
    };

    enum class DialogueEffectOp : uint8_t
    {
        SetFact = 0,
        AddFact,
        QuestEvent,
        GiveItem,
        TakeItem
    };

    enum class DialogueCmp : uint8_t
    {
        None = 0,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual
    };

    inline constexpr int kDialogueConditionOpCount = static_cast<int>(DialogueConditionOp::Health) + 1;
    inline constexpr int kDialogueEffectOpCount    = static_cast<int>(DialogueEffectOp::TakeItem) + 1;
    inline constexpr int kDialogueCmpCount         = static_cast<int>(DialogueCmp::GreaterEqual) + 1;

    inline const char* dialogueConditionOpName(DialogueConditionOp op)
    {
        switch (op)
        {
        case DialogueConditionOp::Fact:          return "fact";
        case DialogueConditionOp::QuestActive:   return "questActive";
        case DialogueConditionOp::QuestComplete: return "questComplete";
        case DialogueConditionOp::Objective:     return "objective";
        case DialogueConditionOp::Item:          return "item";
        case DialogueConditionOp::Alertness:     return "alertness";
        case DialogueConditionOp::Health:        return "health";
        }
        return "";
    }

    inline const char* dialogueEffectOpName(DialogueEffectOp op)
    {
        switch (op)
        {
        case DialogueEffectOp::SetFact:    return "setFact";
        case DialogueEffectOp::AddFact:    return "addFact";
        case DialogueEffectOp::QuestEvent: return "questEvent";
        case DialogueEffectOp::GiveItem:   return "giveItem";
        case DialogueEffectOp::TakeItem:   return "takeItem";
        }
        return "";
    }

    inline const char* dialogueCmpName(DialogueCmp cmp)
    {
        switch (cmp)
        {
        case DialogueCmp::None:         return "";
        case DialogueCmp::Equal:        return "==";
        case DialogueCmp::NotEqual:     return "!=";
        case DialogueCmp::Less:         return "<";
        case DialogueCmp::LessEqual:    return "<=";
        case DialogueCmp::Greater:      return ">";
        case DialogueCmp::GreaterEqual: return ">=";
        }
        return "";
    }

    inline bool parseDialogueConditionOp(std::string_view text, DialogueConditionOp& out)
    {
        for (int index = 0; index < kDialogueConditionOpCount; ++index)
        {
            const auto op = static_cast<DialogueConditionOp>(index);
            if (text == dialogueConditionOpName(op))
            {
                out = op;
                return true;
            }
        }
        return false;
    }

    inline bool parseDialogueEffectOp(std::string_view text, DialogueEffectOp& out)
    {
        for (int index = 0; index < kDialogueEffectOpCount; ++index)
        {
            const auto op = static_cast<DialogueEffectOp>(index);
            if (text == dialogueEffectOpName(op))
            {
                out = op;
                return true;
            }
        }
        return false;
    }

    inline bool parseDialogueCmp(std::string_view text, DialogueCmp& out)
    {
        if (text.empty())
        {
            out = DialogueCmp::None;
            return true;
        }
        for (int index = 1; index < kDialogueCmpCount; ++index)
        {
            const auto cmp = static_cast<DialogueCmp>(index);
            if (text == dialogueCmpName(cmp))
            {
                out = cmp;
                return true;
            }
        }
        return false;
    }

    struct DialogueLine
    {
        std::string id;
        std::string text;
        bool        once = false;
        std::string clip;
        std::string speaker; // Empty uses the graph speaker.
    };

    struct DialogueCondition
    {
        DialogueConditionOp op = DialogueConditionOp::Fact;
        std::string         key;
        DialogueCmp         cmp = DialogueCmp::None;
        std::string         name;
        std::string         who;
        int                 value    = 0;
        bool                hasValue = false;
        bool                hasAlive = false;
        bool                alive    = true;
        bool                hasDone  = false;
        bool                done     = false;
    };

    struct DialogueEffect
    {
        DialogueEffectOp op = DialogueEffectOp::SetFact;
        std::string      key;
        std::string      name;
        int              value    = 0;
        int              count    = 1;
        bool             hasCount = false;
    };

    struct DialogueChoice
    {
        std::string                    id;
        std::string                    text;
        std::string                    next;
        bool                           once = false;
        std::vector<DialogueCondition> conditions;
        std::vector<DialogueEffect>    effects;
    };

    struct DialogueNode
    {
        std::string                id;
        float                      x = 0.0f;
        float                      y = 0.0f;
        std::vector<DialogueLine>  lines;
        std::vector<DialogueChoice> choices;
    };

    class DialogueGraphDef : public Asset
    {
    public:
        DialogueGraphDef();

        std::string                id;
        std::string                sourcePath;
        bool                       pauseWorld = false;
        float                      talkRange  = 2.5f;
        float                      leash      = 4.0f;
        std::string                speaker    = "NPC";
        std::string                entry;
        std::vector<DialogueLine>  refusal;
        std::vector<DialogueLine>  resume;
        std::vector<DialogueNode>  nodes;
        mutable std::vector<std::string> loggedOps;

        const DialogueNode* findNode(const std::string& nodeId) const;
        void                noteUnknown(const std::string& op) const;
    };

} // namespace Dark
