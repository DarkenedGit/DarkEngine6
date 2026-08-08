#pragma once

// Umbrella header for DarkEngine6 collision queries.
//
// Two families of tests:
//   1. Static  — Intersects / Intersect (no motion)
//   2. Swept   — SweptIntersects (linear motion over t ∈ [0,1], returns TOI)
//
// Shapes: point, ray, sphere/circle, AABB, OBB, frustum (3D).

#include "HitResult.h"
#include "StaticCollision.h"
#include "SweptCollision.h"
