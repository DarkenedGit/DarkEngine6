#pragma once

#include "Game/EchoChirpFile.h"
#include "Game/EchoPipeline.h"

namespace Dark
{
    class Renderer;

    // Install the echo hook only while isValid().
    class EchoFeature
    {
    public:
        // Loads content/render/echolocation.json over the chirp settings (unchanged on failure).
        bool loadChirp();
        // False (logged as a warning with `tag`) when the pipeline cannot be created; echolocation
        // is then disabled.
        bool create(Renderer& renderer, const char* tag);
        void release();

        bool isValid() const
        {
            return m_pipeline.isValid();
        }
        EchoPipeline& pipeline()
        {
            return m_pipeline;
        }
        EchoChirpSettings& chirp()
        {
            return m_chirp;
        }
        const EchoChirpSettings& chirp() const
        {
            return m_chirp;
        }

    private:
        EchoPipeline      m_pipeline;
        EchoChirpSettings m_chirp{};
    };

} // namespace Dark
