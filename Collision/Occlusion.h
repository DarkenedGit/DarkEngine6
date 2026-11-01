#pragma once

#include "Math/AABox3f.h"
#include "Math/Vector3f.h"

namespace Dark::Collision
{
	// True when a box cuts the segment. Hits within slack of either end are ignored, so a lamp
	// or speaker resting against a wall still counts as clear.
	bool segmentBlocked(const Math::AABox3f* boxes, int count, const Math::Vector3f& from, const Math::Vector3f& to, float slack = 0.3f);

	// 0 = clear, 1 = fully behind walls. Casts one ray at the source and two beside it,
	// so a doorway or a corner gives a partial value.
	float soundOcclusion(const Math::AABox3f* boxes, int count, const Math::Vector3f& listener, const Math::Vector3f& source);
}
