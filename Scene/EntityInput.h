#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    class ActionMap;

    // One row of the player, player2d, sandbox, sandbox2d, or editor input json files.
    // An empty app applies to every caller. "sandbox" and "editor" apply to that app only.
    // Exactly one of key, button, axis, or mouse is set. scale -1 reverses a key or axis.
    // threshold > 0 turns an axis into a hold and keeps it out of the analog axis sum.
    // mods is ModCtrl, ModShift, ModAlt, or 0. A non-zero mask counts only while that modifier is held.
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
        uint8_t     mods      = 0;
    };

    // "player" -> "entities/player.entity.input.json". "player2d" -> "entities/player2d.entity.input.json".
    std::string entityInputVirtualPath(std::string_view typeName);

    // On failure `out` is unchanged. The file type must equal expectedType ("player", "player2d", "sandbox", "sandbox2d", or "editor").
    bool parseEntityInput(std::string_view jsonText, std::vector<EntityInputBinding>& out, std::string_view expectedType = "player");

    // Clears each action it writes, then adds the rows whose app matches.
    // Unknown device names are skipped. Returns false when nothing was applied.
    bool applyEntityInput(ActionMap& actions, const std::vector<EntityInputBinding>& bindings, std::string_view app);

    // Loads entities/player.entity.input.json onto `actions`. On failure the map is unchanged.
    bool applyPlayerInputFile(ActionMap& actions, std::string_view app);

    // Loads entities/player2d.entity.input.json onto `actions`. On failure the map is unchanged.
    bool applyPlayer2dInputFile(ActionMap& actions);

    // Loads content/sandbox.input.json onto `actions`. On failure the map is unchanged.
    bool applySandboxInputFile(ActionMap& actions);

    // Loads content/sandbox2d.input.json onto `actions`. On failure the map is unchanged.
    bool applySandbox2dInputFile(ActionMap& actions);

    // Loads content/editor.input.json onto `actions`. On failure the map is unchanged.
    bool applyEditorInputFile(ActionMap& actions);

} // namespace Dark
