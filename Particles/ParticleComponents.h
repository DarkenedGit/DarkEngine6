#pragma once

#include "Particles/ParticleEmitter.h"

#include <memory>

namespace Dark
{

    struct ParticleEmitterComponent
    {
        static constexpr const char* kTypeName = "ParticleEmitter";

        ParticleEmitterDesc              desc{};
        bool                             playing = true;
        std::unique_ptr<ParticleEmitter> runtime;

        ParticleEmitterComponent() = default;
        ParticleEmitterComponent(ParticleEmitterComponent&&) noexcept            = default;
        ParticleEmitterComponent& operator=(ParticleEmitterComponent&&) noexcept = default;
        ParticleEmitterComponent(const ParticleEmitterComponent&)            = delete;
        ParticleEmitterComponent& operator=(const ParticleEmitterComponent&) = delete;
    };

    inline void ensureParticleRuntime(ParticleEmitterComponent& pe)
    {
        if (pe.runtime)
            return;
        pe.runtime = std::make_unique<ParticleEmitter>();
        pe.runtime->setDesc(pe.desc);
        if (pe.playing)
            pe.runtime->play();
        else
            pe.runtime->stop();
    }

} // namespace Dark
