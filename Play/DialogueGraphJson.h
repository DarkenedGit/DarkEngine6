#pragma once

#include "Play/DialogueGraph.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark
{

    bool parseDialogueGraph(std::string_view jsonText, DialogueGraphDef& out, std::string* error = nullptr);
    bool writeDialogueGraphJson(const DialogueGraphDef& graph, std::string& out);
    bool saveDialogueGraphJsonFile(const DialogueGraphDef& graph, const std::filesystem::path& path);

} // namespace Dark
