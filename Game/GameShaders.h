#pragma once

namespace Dark
{

    // The game's shader root under content/, beside the engine's content/shaders. Its shaders are
    // listed in content/game/shaders/shaders.manifest and compile as "game/shaders/<file>".
    inline constexpr const char* kGameShaderRoot = "game/shaders";

    // Adds kGameShaderRoot to the shader roots (addShaderRoot). Game pipelines call this before compiling
    // their shaders. False (logged) when the root cannot be added, e.g. a file name clash with content/shaders.
    bool addGameShaderRoot();

} // namespace Dark
