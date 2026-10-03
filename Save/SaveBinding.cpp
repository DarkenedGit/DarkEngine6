#include "Save/SaveBinding.h"

#include "Core/Log.h"

#include <map>
#include <string>

namespace Dark::Save
{
    namespace
    {
        std::map<std::string, SaveBinding, std::less<>>& bindings()
        {
            static std::map<std::string, SaveBinding, std::less<>> map;
            return map;
        }
    } // namespace

    bool bindPersistRaw(const SaveBinding& binding)
    {
        if (!binding.fns || !binding.fns->key || binding.fns->key[0] == '\0' || !binding.fns->capture || !binding.fns->apply || !binding.getOrEmplace || !binding.remove)
        {
            DE_LOG_ERROR("Save: ignored persist binding with no key");
            DE_ASSERT(false);
            return false;
        }
        const std::string key(binding.fns->key);
        if (key == "Unnamed")
        {
            DE_LOG_ERROR("Save: ignored persist key Unnamed");
            DE_ASSERT(false);
            return false;
        }
        auto& map = bindings();
        if (map.contains(key))
        {
            DE_LOG_ERROR("Save: duplicate persist key {}", key);
            DE_ASSERT(false);
            return false;
        }
        map.emplace(key, binding);
        return true;
    }

    const SaveBinding* findSaveBinding(std::string_view key)
    {
        const auto& map = bindings();
        const auto  it  = map.find(key);
        if (it == map.end())
            return nullptr;
        return &it->second;
    }

} // namespace Dark::Save
