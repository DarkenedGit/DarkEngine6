#include "Game/EchoFeature.h"

#include "Core/Log.h"
#include "Render/Renderer.h"

namespace Dark
{

    bool EchoFeature::loadChirp()
    {
        return loadEchoChirpSettings(m_chirp);
    }

    bool EchoFeature::create(Renderer& renderer, const char* tag)
    {
        if (m_pipeline.create(renderer.device()))
            return true;
        DE_LOG_WARN(LogCategory::Render, "{}: EchoPipeline create failed — echolocation disabled", tag);
        return false;
    }

    void EchoFeature::release()
    {
        m_pipeline.release();
    }

} // namespace Dark
