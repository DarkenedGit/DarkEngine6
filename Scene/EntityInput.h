#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    class ActionMap;

    // One row of entities/player.entity.input.json.
    // An empty app applies to every caller. "sandbox" and "editor" apply to that app only.
    // Exactly one of key, button, axis, or mouse is set. scale -1 reverses a key or axis.
    // threshold > 0 turns an axis into a hold and keeps it out of the analog axis sum.
    struct EntityInputBinding
    {
        std::string action;
        std::string app;
        std::string key;
        std::string button;
        std::string axis;
        std::string mouse;
        float       scale     = 1.0f;
        float       threshold = 0.0f;
        int         pad       = 0;
    };

    // "player" -> "entities/player.entity.input.json"
    std::string entityInputVirtualPath(std::string_view typeName);

    // On failure `out` is unchanged.
    bool parseEntityInput(std::string_view jsonText, std::vector<EntityInputBinding>& out);

    // Clears each action it writes, then adds the rows whose app matches.
    // Unknown device names are skipped. Returns false when nothing was applied.
    bool applyEntityInput(ActionMap& actions, const std::vector<EntityInputBinding>& bindings, std::string_view app);

    // Loads entities/player.entity.input.json onto `actions`. On failure the map is unchanged.
    bool applyPlayerInputFile(ActionMap& actions, std::string_view app);

} // namespace Dark
