#pragma once

#include <string_view>

namespace Dark
{

// Chirp clock. periodSeconds is how long one snapshot is held. speedMetersPerSec is
// how fast the shell expands. Range, edge, and the snapshot scale stay on EchoPipeline.
struct EchoChirpSettings
{
    static constexpr float kPeriodMin = 0.05f;
    static constexpr float kPeriodMax = 8.0f;
    static constexpr float kSpeedMin  = 1.0f;
    static constexpr float kSpeedMax  = 800.0f;

    float periodSeconds     = 1.25f;
    float speedMetersPerSec = 50.0f;
};

// Built-in copy of content/render/echolocation.json, used when that file is missing.
EchoChirpSettings builtinEchoChirpSettings();

// On failure `out` is unchanged. Positive values outside the slider window are clamped.
bool parseEchoChirpSettings(std::string_view jsonText, EchoChirpSettings& out);

// Walks content roots for render/echolocation.json. On failure `out` is unchanged.
bool loadEchoChirpSettings(EchoChirpSettings& out);

} // namespace Dark
