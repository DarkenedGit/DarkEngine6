#pragma once

#include "Particles/ParticleTypes.h"

#include "third_party/nlohmann/json.hpp"

namespace Dark
{

    // Full emitter object. A child reaction of SpawnSecondary is stored as Stop.
    nlohmann::json particleToJson(const ParticleEmitterDesc& desc);

    // Overlays keys that are present. Missing keys keep the current desc.
    // A sim mode other than Gpu becomes Cpu. An unknown collision value keeps the fallback.
    void particleFromJson(const nlohmann::json& particle, ParticleEmitterDesc& desc, const char* where = "Particle");

} // namespace Dark
