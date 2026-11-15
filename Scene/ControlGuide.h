#pragma once

#include "Scene/EntityInput.h"

#include <string>
#include <vector>

namespace Dark
{

    // One settings-page row. `keys` joins every device that does the same thing, separated by " / ".
    struct ControlRow
    {
        std::string keys;
        std::string what;
    };

    // Sandbox settings lists. Game is play. Engine is the debug and sandbox tooling.
    struct ControlGuide
    {
        std::vector<ControlRow> game;
        std::vector<ControlRow> engine;
    };

    // Display name for one binding. Empty when the row has no device.
    // Shift, Ctrl, and the two Alt keys collapse to one word. Pad face buttons are prefixed so they
    // do not look like keyboard letters.
    std::string controlDeviceLabel(const EntityInputBinding& row);

    // Editor-only rows are omitted. An action with no written line still appears, using its id.
    // Mouse look, dialogue choice keys, the fly boost, and the movie keys are added here because
    // they are read in code and are not in the input json.
    void buildSandboxControlGuide(const std::vector<EntityInputBinding>& player, const std::vector<EntityInputBinding>& sandbox, ControlGuide& out);

    // Reads entities/player.entity.input.json and sandbox.input.json. On failure `out` is unchanged.
    bool loadSandboxControlGuide(ControlGuide& out);

} // namespace Dark
