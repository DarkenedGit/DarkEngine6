#pragma once

#include "Render/FrameFeatures.h"
#include "Render/FrameView.h"

#include <cstdint>
#include <vector>

// Render features: a domain module (Sky, Terrain, Water, ...) adds its passes to a host frame through
// IRenderFeature instead of SceneRenderer owning its pipeline. Hosts register the features they run
// with explicit registerXxxFeatures calls; RenderFeatureRegistry creates them with the scene pipelines
// and declares their passes each frame. CPU only, so the native graph tests drive it.

namespace Dark
{

    class Renderer;

    // Where a feature's passes belong in the frame. Features are kept in stage order, then
    // registration order; the host feature list still sets the graph order of the passes.
    enum class FeatureStage : uint8_t
    {
        PreFrame,
        GBuffer,
        Shadows,
        PostGBuffer,
        Lighting,
        Sky,
        Transparent,
        PreTonemapHdr,
        Post,
        Overlay,
        Ui,
    };

    struct FeatureCreateInfo
    {
        Renderer*   renderer = nullptr;
        const char* logTag   = "SceneRenderer";
    };

    class IRenderFeature
    {
    public:
        virtual ~IRenderFeature() = default;

        virtual const char*  name() const  = 0;
        virtual FeatureStage stage() const = 0;
        // The feature-list entry its passes count as.
        virtual PassId pass() const = 0;

        // False when the feature cannot run (logged by the feature); it is then not declared.
        virtual bool create(const FeatureCreateInfo& info) = 0;
        virtual void destroy()                             = 0;
        // Setup time, inside the entry's FeatureFrame::declare. Execute callbacks it adds may read the
        // feature's state at execute.
        virtual void addPasses(FeatureBuilder& b, const FrameView& view) = 0;
    };

    // Non-owning: the host owns its features and keeps them alive while they are registered.
    class RenderFeatureRegistry
    {
    public:
        // False (logged) for null, a feature already added, or a second feature on the same PassId.
        bool add(IRenderFeature* feature);
        void clear();

        // Creates every feature not yet created. False when any failed (each failure is logged);
        // the rest still run.
        bool createAll(const FeatureCreateInfo& info);
        void destroyAll();

        uint32_t count() const
        {
            return static_cast<uint32_t>(m_entries.size());
        }
        IRenderFeature* feature(uint32_t i) const
        {
            return m_entries[i].feature;
        }
        bool isCreated(uint32_t i) const
        {
            return m_entries[i].created;
        }
        IRenderFeature* find(PassId id) const;

        // Declares every created feature on `frame` (FeatureFrame or FrameComposer), in registry order.
        // Returns how many the frame accepted.
        template <class Frame> uint32_t declare(Frame& frame, const FrameView& view) const
        {
            uint32_t declared = 0;
            for (const Entry& e : m_entries)
            {
                if (!e.created)
                    continue;
                IRenderFeature* const f = e.feature;
                if (frame.declare(f->pass(), [f, view](FeatureBuilder& b) { f->addPasses(b, view); }))
                    ++declared;
            }
            return declared;
        }

    private:
        struct Entry
        {
            IRenderFeature* feature = nullptr;
            bool            created = false;
        };
        std::vector<Entry> m_entries;
    };

} // namespace Dark
