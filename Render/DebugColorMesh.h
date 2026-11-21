#pragma once

#include <cstdint>
#include <vector>

namespace Dark
{

    // Position + RGBA, the vertex layout of the walk overlay pipeline. No D3D types, so gameplay code can fill it.
    struct DebugColorVertex
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;
    };

    static_assert(sizeof(DebugColorVertex) == 28, "debug mesh vertex is position + color");

    struct DebugColorMesh
    {
        std::vector<DebugColorVertex> vertices;
        std::vector<uint32_t>         indices;
    };

    // A CPU mesh the renderer uploads only when serial changes. Serial 0 (or no build) means nothing to draw.
    struct DebugMeshSource
    {
        uint32_t serial                                      = 0;
        bool (*build)(const void* user, DebugColorMesh& out) = nullptr;
        const void* user                                     = nullptr;
    };

} // namespace Dark
