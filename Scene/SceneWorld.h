#pragma once

#include "Assets/AssetHandle.h"
#include "Physics/FoliageCollision.h"
#include "Physics/PhysicsWorld.h"
#include "Render/Camera3D.h"
#include "Render/DeferredLightingPipeline.h"
#include "Render/GtaoPipeline.h"
#include "Render/SsrSettings.h"
#include "Scene/SceneTypes.h"
#include "Sky/Environment.h"
#include "Terrain/FoliageFile.h"
#include "Terrain/GrassField.h"
#include "Terrain/LeafField.h"
#include "Terrain/LeafRustle.h"
#include "Terrain/TerrainGrid.h"
#include "Terrain/TerrainGround.h"
#include "Terrain/WindField.h"
#include "Water/Water.h"

#include <vector>

namespace Dark
{

    // Simulation both Sandbox and the Editor own one of. Each app draws it
    // with its own frame. The engine does not own this object.
    struct SceneWorld
    {
        Camera3D camera;

        Sky::Environment env;
        IblSettings      ibl;
        GtaoSettings     ssao;
        SsrSettings      ssr;
        AssetID          iblImageId = NULL_ASSET;
        float            cloudTime  = 0.0f;
        StealthSceneDesc stealth{};

        Terrain::TerrainGrid   terrain;
        Terrain::TerrainGround ground;
        float                  terrainSeaLevel = 0.0f;
        bool                   haveTerrainSea  = false;
        // A scene with no terrain block still needs a height field for walking.
        // The mesh stays hidden so the scene's floor is the visible ground.
        bool                   showTerrain     = true;

        Terrain::FoliageDensity             foliageDensity{};
        std::vector<Terrain::FoliageRecord> foliage;
        Terrain::WindSettings               wind = Terrain::defaultWindSettings();
        Terrain::WindClock                  windClock;
        Terrain::GrassField                 grassField;
        Terrain::LeafField                  leafField;
        Terrain::LeafRustleVoices           leafRustle;
        float                               leafShedScale  = 1.0f;
        double                              leafSimSeconds = 0.0;

        Physics::PhysicsWorld        physics;
        Physics::PhysicsBodyId       physicsGround = Physics::kNullPhysicsBody;
        Physics::FoliageCollisionSet foliageCollision;
        Physics::PhysicsBodyId       groundIgnore = Physics::kNullPhysicsBody;

        WaterWorld water;
    };

} // namespace Dark
