#pragma once

#include "Math/AABox3f.h"
#include "Terrain/HeightMap.h"

#include <cstdint>
#include <vector>

namespace Dark::AI
{

struct WalkabilityDesc
{
    const Terrain::HeightMap* heightMap   = nullptr;
    float                     waterLevel  = -1.0e9f;
    float                     maxSlopeDeg = 45.0f;
    float                     agentRadius = 0.8f;
    const Math::Aabb3f*       cubes       = nullptr;
    int                       cubeCount   = 0;
};

class Walkability
{
public:
    bool bake(const WalkabilityDesc& desc);

    bool valid() const { return m_cellsX > 0 && m_cellsZ > 0 && m_heightMap != nullptr; }

    int   cellsX() const { return m_cellsX; }
    int   cellsZ() const { return m_cellsZ; }
    float cellSize() const { return m_cellSize; }
    float maxClimb() const { return m_maxClimb; }

    bool worldToCell(float worldX, float worldZ, int& cx, int& cz) const;
    float cellCenterX(int cx) const;
    float cellCenterZ(int cz) const;

    bool inBoundsCell(int cx, int cz) const;
    bool walkable(int cx, int cz) const;
    bool walkableWorld(float worldX, float worldZ) const;
    bool destWet(float worldX, float worldZ) const;

    int island(int cx, int cz) const;
    int islandWorld(float worldX, float worldZ) const;

    bool edgeOk(int x0, int z0, int x1, int z1) const;
    bool lineOfSight(int x0, int z0, int x1, int z1) const;

    const Terrain::HeightMap* heightMap() const { return m_heightMap; }

private:
    int index(int cx, int cz) const { return cz * m_cellsX + cx; }
    bool cellWet(int cx, int cz) const;
    bool cellSteep(int cx, int cz) const;
    bool cellHitsCube(int cx, int cz) const;
    void floodIslands();

    const Terrain::HeightMap* m_heightMap = nullptr;
    WalkabilityDesc           m_desc{};
    int                       m_cellsX   = 0;
    int                       m_cellsZ   = 0;
    float                     m_cellSize = 1.0f;
    float                     m_maxClimb = 1.0f;
    float                     m_minNy    = 0.70710678f;
    std::vector<uint8_t>      m_walk;
    std::vector<int>          m_island;
};

} // namespace Dark::AI
