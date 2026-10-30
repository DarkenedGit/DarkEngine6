#include "Play/DialogueGraph.h"

#include "Core/Log.h"

namespace Dark
{

    DialogueGraphDef::DialogueGraphDef()
    {
        type = AssetType::DialogueGraph;
    }

    const DialogueNode* DialogueGraphDef::findNode(const std::string& nodeId) const
    {
        for (const DialogueNode& node : nodes)
        {
            if (node.id == nodeId)
                return &node;
        }
        return nullptr;
    }

    void DialogueGraphDef::noteUnknown(const std::string& op) const
    {
        for (const std::string& seen : loggedOps)
        {
            if (seen == op)
                return;
        }
        if (loggedOps.size() < 32)
            loggedOps.push_back(op);
        DE_LOG_ERROR(LogCategory::AI, "Dialogue: unknown op \"{}\"", op);
    }

} // namespace Dark
