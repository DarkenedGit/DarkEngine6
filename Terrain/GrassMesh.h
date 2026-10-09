#pragma once

#include "Assets/MeshData.h"

namespace Dark::Terrain
{
    // lod 0..3. LOD 2 and LOD 3 are the same quad. A bad lod returns false and clears out.
    bool buildGrassBladeMesh(int lod, MeshData& out);

} // namespace Dark::Terrain
