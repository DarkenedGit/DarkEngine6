#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Dark
{

    struct ThrowableDef
    {
        std::string id;
        float       weight        = 0.0f;
        float       throwDistance = 0.0f;
        std::string impactSound;
        float       callDistance = 0.0f;
        std::string model;
    };

    class ThrowableCatalog
    {
    public:
        ThrowableCatalog();

        // nlohmann::json::parse(..., nullptr, false). On failure *this is unchanged.
        bool parse(std::string_view jsonText);
        // Reads content/items/throwables.json once. A later call does not read again.
        bool loadFromContent();

        const ThrowableDef*              find(std::string_view id) const;
        const std::vector<ThrowableDef>& items() const
        {
            return m_items;
        }

    private:
        std::vector<ThrowableDef> m_items;
        bool                      m_loaded = false;
        bool                      m_loadOk = true;
    };

    ThrowableCatalog& throwableCatalog();

} // namespace Dark
