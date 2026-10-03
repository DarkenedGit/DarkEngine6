#pragma once

#include "ECS/World.h"
#include "Save/PersistentId.h"
#include "Save/SaveJson.h"
#include "Save/SaveSystem.h"
#include "Save/SaveTypes.h"

#include "third_party/nlohmann/json.hpp"

#include <string>

inline void armSave(Dark::Save::SaveSystem& save, const Dark::Save::SaveHost& host)
{
    save.setHost(host);
    save.setWorldIdentity("", "", "unit");
}

inline Dark::Save::SaveHost testHost()
{
    Dark::Save::SaveHost host{};
    host.id = "test";
    return host;
}

inline bool componentJson(nlohmann::ordered_json& root, const char* key, nlohmann::ordered_json*& out)
{
    auto payload = root.find("payload");
    if (payload == root.end() || !payload->is_object())
        return false;
    auto entities = payload->find("entities");
    if (entities == payload->end() || !entities->is_array())
        return false;
    for (auto& row : *entities)
    {
        if (!row.is_object())
            continue;
        auto comps = row.find("components");
        if (comps == row.end() || !comps->is_object())
            continue;
        auto comp = comps->find(key);
        if (comp != comps->end() && comp->is_object())
        {
            out = &(*comp);
            return true;
        }
    }
    return false;
}

inline void rehashPayload(nlohmann::ordered_json& root)
{
    auto payload = root.find("payload");
    if (payload == root.end())
        return;
    const std::string compact = payload->dump();
    auto integrity = root.find("integrity");
    if (integrity == root.end() || !integrity->is_object())
        return;
    (*integrity)["payload"] = Dark::Save::toHex16(Dark::Save::fnv1a64(compact.data(), compact.size()));
    (*integrity)["bytes"]   = compact.size();
}

inline bool parseSave(const std::string& text, nlohmann::ordered_json& root)
{
    Dark::Save::SaveResult error = Dark::Save::SaveResult::Ok;
    return Dark::Save::parseDocument(text, root, error) && error == Dark::Save::SaveResult::Ok;
}
