#pragma once

#include "Particles/ParticleEmitter.h"

// ImGui panel for authoring a ParticleEmitterDesc and controlling playback.
class ParticleEditorPanel
{
public:
    void draw(Dark::ParticleEmitter& emitter, bool* open = nullptr);

    // True when user requested a burst this frame.
    bool consumeBurstRequest(uint32_t& outCount);

private:
    int      m_burstCount = 32;
    bool     m_burstRequested = false;
    uint32_t m_lastBurst = 0;
};
