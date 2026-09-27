#pragma once

#include "Math/Vector3f.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Dark::Physics
{
    inline constexpr uint32_t kNullPhysicsSurfaceId = 0;

    // CPU catalog record. Distinct from PBR Material. Box3D copies friction/restitution/
    // rolling/density/sensor onto shapes; footstep/tags/damage stay engine-side and are
    // recovered from userMaterialId (internId stored as uint64).
    struct PhysicsSurface
    {
        std::string              id;
        uint32_t                 internId         = kNullPhysicsSurfaceId;
        float                    friction         = 0.6f;
        float                    restitution      = 0.0f;
        float                    density          = 1.0f;
        float                    rolling          = 0.0f;
        bool                     sensor           = false;
        bool                     climbable        = false;
        float                    damagePerSecond  = 0.0f;
        std::string              footstep;
        std::vector<std::string> tags;
        uint32_t                 customColor      = 0; // 0 = unused; low 24 bits RGB
        Math::Vector3f           tangentVelocity{0.0f, 0.0f, 0.0f};

        uint64_t userMaterialId() const { return internId; }
    };

    class PhysicsSurfaceCatalog
    {
    public:
        // nlohmann::json::parse(text, nullptr, false). On failure the catalog is unchanged.
        bool parse(std::string_view jsonText);
        bool loadFile(const std::filesystem::path& path);
        bool loadFromContent();

        void clear();
        bool empty() const { return m_surfaces.empty(); }
        uint32_t count() const { return static_cast<uint32_t>(m_surfaces.size()); }

        uint32_t idOf(std::string_view name) const;
        const PhysicsSurface* find(std::string_view name) const;
        const PhysicsSurface* find(uint32_t internId) const;
        const PhysicsSurface* findByUserMaterialId(uint64_t userMaterialId) const;

        const PhysicsSurface& getDefault() const;
        const std::vector<PhysicsSurface>& surfaces() const { return m_surfaces; }

    private:
        std::vector<PhysicsSurface> m_surfaces;
    };
} // namespace Dark::Physics
