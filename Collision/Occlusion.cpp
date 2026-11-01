#include "Collision/Occlusion.h"

#include "Collision/StaticCollision.h"
#include "Math/Ray3f.h"

namespace Dark::Collision
{
	bool segmentBlocked(const Math::AABox3f* boxes, int count, const Math::Vector3f& from, const Math::Vector3f& to, float slack)
	{
		if (!boxes || count <= 0)
			return false;
		Math::Vector3f dir  = to - from;
		const float    dist = dir.Magnitude();
		if (dist <= 2.0f * slack)
			return false;
		dir *= 1.0f / dist;
		const Math::Ray3f ray{ from, dir };
		for (int i = 0; i < count; ++i)
		{
			const RayHit3D hit = Intersect(ray, boxes[i]);
			if (hit.hit && hit.t > slack && hit.t < dist - slack)
				return true;
		}
		return false;
	}

	float soundOcclusion(const Math::AABox3f* boxes, int count, const Math::Vector3f& listener, const Math::Vector3f& source)
	{
		if (!boxes || count <= 0)
			return 0.0f;
		Math::Vector3f side{ source.z - listener.z, 0.0f, listener.x - source.x };
		const float    len = side.Magnitude();
		side               = len > 1.0e-4f ? side * (0.6f / len) : Math::Vector3f{ 0.6f, 0.0f, 0.0f };
		int blocked        = 0;
		blocked += segmentBlocked(boxes, count, listener, source) ? 1 : 0;
		blocked += segmentBlocked(boxes, count, listener, source + side) ? 1 : 0;
		blocked += segmentBlocked(boxes, count, listener, source - side) ? 1 : 0;
		return static_cast<float>(blocked) / 3.0f;
	}
}
