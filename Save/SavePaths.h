#pragma once

#include "Save/SaveTypes.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark::Save
{
    std::filesystem::path savesDirectory();
    bool                  validSaveFileName(std::string_view name);
    SaveResult            makeSaveName(SaveKind kind, std::string& outName);

    struct SaveFileInfo
    {
        std::string           name;
        std::filesystem::path path;
        uint64_t              size = 0;
    };

    SaveResult listSaveFiles(std::vector<SaveFileInfo>& out);
    void       deleteStaleTemps();

} // namespace Dark::Save
