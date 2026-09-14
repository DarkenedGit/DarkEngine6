#pragma once

#include "AI/HsmGraph.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace Dark
{
    bool parseHsmGraphJson(const char* jsonText, HsmGraphDef& out);
    bool writeHsmGraphJson(const HsmGraphDef& def, std::string& outText);
    bool saveHsmGraphJsonFile(const HsmGraphDef& def, const std::filesystem::path& path);
} // namespace Dark
