#include "Editor/HsmEditorPanel.h"
#include "Editor/EditorImGui.h"
#include "AI/HsmGraphJson.h"
#include "Editor/HsmStatechart.h"
#include "Assets/ImageCache.h"
#include "Core/Log.h"
#include "Ui/Icons.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace Dark;

namespace
{
std::string uniqueName(const std::vector<std::string>& used, const char* base)
{
    std::string name = base;
    int         n    = 2;
    auto        taken = [&](const std::string& s) {
        for (const std::string& u : used)
        {
            if (u == s)
                return true;
        }
        return false;
    };
    while (taken(name))
        name = std::string(base) + std::to_string(n++);
    return name;
}

std::vector<std::string> stateNames(const HsmGraphDef& def)
{
    std::vector<std::string> names;
    names.reserve(def.states.size());
    for (const HsmStateDef& st : def.states)
        names.push_back(st.name);
    return names;
}

} // namespace
