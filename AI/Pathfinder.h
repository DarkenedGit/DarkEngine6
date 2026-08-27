#pragma once

#include "AI/Walkability.h"
#include "Math/Vector3f.h"

#include <vector>

namespace Dark::AI
{

struct AgentStamp
{
    float x      = 0.0f;
    float z      = 0.0f;
    float radius = 0.8f;
};

struct PathRequest
{
    float              startX     = 0.0f;
    float              startZ     = 0.0f;
    float              destX      = 0.0f;
    float              destZ      = 0.0f;
    const AgentStamp*  others     = nullptr;
    int                otherCount = 0;
    int                expansionCap = 4096;
};

struct PathResult
{
    std::vector<Math::Vector3f> points;
};

class Pathfinder
{
public:
    bool bind(const Walkability* walk);
    bool find(const PathRequest& req, PathResult& out) const;

private:
    bool blockedByAgent(int cx, int cz, const PathRequest& req, int startCx, int startCz) const;
    bool snapDest(const PathRequest& req, int startCx, int startCz, int& dx, int& dz) const;
    bool destInCube(float x, float z) const;
    bool search(int sx, int sz, int dx, int dz, const PathRequest& req, std::vector<int>& cells) const;
    void stringPull(const std::vector<int>& cells, PathResult& out) const;
    float heuristic(int ax, int az, int bx, int bz) const;
    float stepCost(int x0, int z0, int x1, int z1, const PathRequest& req, int sx, int sz) const;

    const Walkability* m_walk = nullptr;
};

} // namespace Dark::AI
