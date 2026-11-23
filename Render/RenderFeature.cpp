#include "Render/RenderFeature.h"

#include "Core/Log.h"

namespace Dark
{

    bool RenderFeatureRegistry::add(IRenderFeature* feature)
    {
        if (!feature)
        {
            DE_LOG_ERROR(LogCategory::Render, "RenderFeatureRegistry: null feature");
            return false;
        }
        for (const Entry& e : m_entries)
        {
            if (e.feature == feature)
            {
                DE_LOG_ERROR(LogCategory::Render, "RenderFeatureRegistry: {} is already registered", feature->name());
                return false;
            }
            if (e.feature->pass() == feature->pass())
            {
                DE_LOG_ERROR(LogCategory::Render, "RenderFeatureRegistry: {} and {} both declare {}", e.feature->name(), feature->name(), passName(feature->pass()));
                return false;
            }
        }
        auto at = m_entries.end();
        while (at != m_entries.begin() && (at - 1)->feature->stage() > feature->stage())
            --at;
        m_entries.insert(at, Entry{ feature, false });
        return true;
    }

    void RenderFeatureRegistry::clear()
    {
        destroyAll();
        m_entries.clear();
    }

    bool RenderFeatureRegistry::createAll(const FeatureCreateInfo& info)
    {
        bool ok = true;
        for (Entry& e : m_entries)
        {
            if (e.created)
                continue;
            e.created = e.feature->create(info);
            if (!e.created)
            {
                DE_LOG_ERROR(LogCategory::Render, "{}: {} feature create failed; its passes are skipped", info.logTag ? info.logTag : "RenderFeatureRegistry", e.feature->name());
                ok = false;
            }
        }
        return ok;
    }

    void RenderFeatureRegistry::destroyAll()
    {
        for (auto it = m_entries.rbegin(); it != m_entries.rend(); ++it)
        {
            if (!it->created)
                continue;
            it->feature->destroy();
            it->created = false;
        }
    }

    IRenderFeature* RenderFeatureRegistry::find(PassId id) const
    {
        for (const Entry& e : m_entries)
        {
            if (e.feature->pass() == id)
                return e.feature;
        }
        return nullptr;
    }

} // namespace Dark
