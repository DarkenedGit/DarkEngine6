#pragma once

#include "Assets/AssetHandle.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Dark
{

    struct DialogueLine
    {
        std::string id;
        std::string text;
        bool        once = false;
        std::string clip;
    };

    struct DialogueCondition
    {
        std::string op;
        std::string key;
        std::string cmp;
        std::string name;
        std::string who;
        int         value    = 0;
        bool        hasValue = false;
        bool        hasAlive = false;
        bool        alive    = true;
        bool        hasDone  = false;
        bool        done     = false;
    };

    struct DialogueEffect
    {
        std::string op;
        std::string key;
        std::string name;
        int         value    = 0;
        int         count    = 1;
        bool        hasCount = false;
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
