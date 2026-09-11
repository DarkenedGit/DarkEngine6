#pragma once

#include "MeshGen.h"
#include "Math/Sphere2f.h"
#include "Math/Capsule2f.h"
#include "Math/AABox2f.h"
#include "Math/Box2f.h"

// ============================================================
//  MeshGen2D.h  –  Procedural 2-D mesh generation
// ============================================================
//
//  Filled shapes in the XY plane (z = 0), facing +Z.
//  Winding is CCW when viewed from +Z (same as CreateQuadXY).
//  Normals are (0, 0, 1). UVs follow image convention: V = 0 at +Y.
//
//  Generators append to the MeshData (they do not clear it).
// ============================================================

namespace Dark
{
	// Disk from Sphere2f (2D circle). slices = circumference subdivisions (>= 3).
	bool CreateSphere2(MeshData& mesh, const Math::Sphere2f& circle, int slices = 32);

	// Stadium from Capsule2f: body quad + semicircular caps.
	// capSlices = segments per semicircle (>= 2). Degenerate A==B becomes a disk.
	bool CreateCapsule2(MeshData& mesh, const Math::Capsule2f& capsule, int capSlices = 16);

	// Axis-aligned rectangle from AABox2f min/max.
	bool CreateAABox2(MeshData& mesh, const Math::AABox2f& box);

	// Oriented rectangle from Box2f center / axes / extents.
	bool CreateBox2(MeshData& mesh, const Math::Box2f& box);
}
