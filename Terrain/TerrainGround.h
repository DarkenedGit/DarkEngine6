#pragma once

#include "Terrain/HeightMap.h"
#include "Terrain/SplatMap.h"

#include <string>
#include <string_view>

namespace Dark::Terrain
{
    // Splat channels: 0 dirt, 1 grass, 2 rock, 3 snow.
    struct GroundContact
    {
        const char* id        = "grass";
        const char* cue       = "step_grass";
        const char* footstep  = "audio/foot_grass.wav";
        float       moveSpeed = 1.0f;
        float       blipHz    = 160.0f;
        int         layer     = 1;
    };

    class TerrainGround
    {
    public:
        TerrainGround();

        bool loadFromContent();
        bool parse(std::string_view jsonText);

        int layerCount() const { return kMaxTerrainLayers; }
        const GroundContact& layer(int index) const;

        // Dominant splat weight under XZ. No map → grass.
        GroundContact at(const HeightMap* height, const SplatMap* splat, float worldX, float worldZ) const;

    private:
        void setDefaults();

        GroundContact m_layers[kMaxTerrainLayers];
        std::string   m_id[kMaxTerrainLayers];
        std::string   m_cue[kMaxTerrainLayers];
        std::string   m_footstep[kMaxTerrainLayers];
    };
}
