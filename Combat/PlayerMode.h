#pragma once

namespace Dark::Combat
{

    // Dev-tool switches on the player. Not saved. Both default off.
    // God: ignore incoming hits. Reaper: ignore incoming hits and kill the attacker.
    struct PlayerModeComponent
    {
        static constexpr const char* kTypeName = "PlayerMode";

        bool godMode    = false;
        bool reaperMode = false;
    };

} // namespace Dark::Combat
