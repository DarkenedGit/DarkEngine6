#pragma once

#include "Save/SaveTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Dark::Save
{
    SaveResult writeAtomic(const std::filesystem::path& path, std::string_view bytes);
    SaveResult readCapped(const std::filesystem::path& path, std::string& out, uint64_t cap);

} // namespace Dark::Save
