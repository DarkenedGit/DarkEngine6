#include "StaticCollision.h"
#include "Math/MathDefines.h"
#include <cmath>
#include <algorithm>

namespace Dark
{
	namespace Collision
	{
		using namespace Math;

		// ─────────────────────────────────────────────────────────────────────
		// Helpers
		// ─────────────────────────────────────────────────────────────────────

		static Vector3f ClosestPointOnAabb(const Vector3f& p, const Aabb3f& box)
		{
			return Vector3f(
				std::max(box.Min.x, std::min(p.x, box.Max.x)),
				std::max(box.Min.y, std::min(p.y, box.Max.y)),
				std::max(box.Min.z, std::min(p.z, box.Max.z)));
		}

		static Vector3f ClosestPointOnObb(const Vector3f& p, const Box3f& box)
		{
			Vector3f d = p - box.Center;
			Vector3f result = box.Center;
			for (int i = 0; i < 3; ++i)
			{
				float dist = d.Dot(box.Axis[i]);
				dist = std::max(-box.Extent[i], std::min(dist, box.Extent[i]));
				result += box.Axis[i] * dist;
			}
			return result;
		}

		static bool ClipRaySlab(float denom, float numer, float& t0, float& t1)
		{
			if (denom > 0.0f)
			{
				if (numer > denom * t1) return false;
				if (numer > denom * t0) t0 = numer / denom;
				return true;
			}
			if (denom < 0.0f)
			{
				if (numer > denom * t0) return false;
				if (numer > denom * t1) t1 = numer / denom;
				return true;
			}
			return numer <= 0.0f;
		}

		// ─────────────────────────────────────────────────────────────────────
		// Point
		// ─────────────────────────────────────────────────────────────────────

		bool Intersects(const Vector3f& point, const Sphere3f& sphere)
		{
			return sphere.Contains(point);
		}

		bool Intersects(const Vector3f& point, const Aabb3f& box)
		{
			return box.Contains(point);
		}

		bool Intersects(const Vector3f& point, const Box3f& box)
		{
			return box.Contains(point);
		}

		bool Intersects(const Vector3f& point, const Frustum3f& frustum)
		{
			return frustum.Contains(point);
		}

		// ─────────────────────────────────────────────────────────────────────
		// Sphere
		// ─────────────────────────────────────────────────────────────────────

		bool Intersects(const Sphere3f& a, const Sphere3f& b)
		{
			return a.Intersects(b);
		}

		bool Intersects(const Sphere3f& sphere, const Aabb3f& box)
		{
			return box.Intersects(sphere);
		}

		bool Intersects(const Sphere3f& sphere, const Box3f& box)
		{
			Vector3f closest = ClosestPointOnObb(sphere.Center, box);
			Vector3f d = closest - sphere.Center;
			return d.MagnitudeSqrd() <= sphere.Radius * sphere.Radius;
		}

		bool Intersects(const Sphere3f& sphere, const Frustum3f& frustum)
		{
			return frustum.Intersects(sphere);
		}

		// ─────────────────────────────────────────────────────────────────────
		// AABB
		// ─────────────────────────────────────────────────────────────────────

		bool Intersects(const Aabb3f& a, const Aabb3f& b)
		{
			return a.Intersects(b);
		}

		bool Intersects(const Aabb3f& aabb, const Box3f& box)
		{
			// Convert AABB to OBB and run OBB-OBB
			return Intersects(Box3f::FromAabb(aabb), box);
		}

		bool Intersects(const Aabb3f& aabb, const Frustum3f& frustum)
		{
			return frustum.Intersects(aabb);
		}

		// ─────────────────────────────────────────────────────────────────────
		// OBB vs OBB (SAT)
		// ─────────────────────────────────────────────────────────────────────

		bool Intersects(const Box3f& a, const Box3f& b)
		{
			const float cutoff = 1.0e-6f;
			float R[3][3], AbsR[3][3];

			for (int i = 0; i < 3; ++i)
			{
				for (int j = 0; j < 3; ++j)
				{
					R[i][j] = a.Axis[i].Dot(b.Axis[j]);
					AbsR[i][j] = fabsf(R[i][j]) + cutoff;
				}
			}

			Vector3f t = b.Center - a.Center;
			float tA[3] = { t.Dot(a.Axis[0]), t.Dot(a.Axis[1]), t.Dot(a.Axis[2]) };

			// A's axes
			for (int i = 0; i < 3; ++i)
			{
				float ra = a.Extent[i];
				float rb = b.Extent[0] * AbsR[i][0] + b.Extent[1] * AbsR[i][1] + b.Extent[2] * AbsR[i][2];
				if (fabsf(tA[i]) > ra + rb)
					return false;
			}

			// B's axes
			for (int j = 0; j < 3; ++j)
			{
				float ra = a.Extent[0] * AbsR[0][j] + a.Extent[1] * AbsR[1][j] + a.Extent[2] * AbsR[2][j];
				float rb = b.Extent[j];
				float tB = t.Dot(b.Axis[j]);
				if (fabsf(tB) > ra + rb)
					return false;
			}

			// Cross products A.axis[i] x B.axis[j]
			// i=0,j=0
			{
				float ra = a.Extent[1] * AbsR[2][0] + a.Extent[2] * AbsR[1][0];
				float rb = b.Extent[1] * AbsR[0][2] + b.Extent[2] * AbsR[0][1];
				if (fabsf(tA[2] * R[1][0] - tA[1] * R[2][0]) > ra + rb) return false;
			}
			// i=0,j=1
			{
				float ra = a.Extent[1] * AbsR[2][1] + a.Extent[2] * AbsR[1][1];
				float rb = b.Extent[0] * AbsR[0][2] + b.Extent[2] * AbsR[0][0];
				if (fabsf(tA[2] * R[1][1] - tA[1] * R[2][1]) > ra + rb) return false;
			}
			// i=0,j=2
			{
				float ra = a.Extent[1] * AbsR[2][2] + a.Extent[2] * AbsR[1][2];
				float rb = b.Extent[0] * AbsR[0][1] + b.Extent[1] * AbsR[0][0];
				if (fabsf(tA[2] * R[1][2] - tA[1] * R[2][2]) > ra + rb) return false;
			}
			// i=1,j=0
			{
				float ra = a.Extent[0] * AbsR[2][0] + a.Extent[2] * AbsR[0][0];
				float rb = b.Extent[1] * AbsR[1][2] + b.Extent[2] * AbsR[1][1];
				if (fabsf(tA[0] * R[2][0] - tA[2] * R[0][0]) > ra + rb) return false;
			}
			// i=1,j=1
			{
				float ra = a.Extent[0] * AbsR[2][1] + a.Extent[2] * AbsR[0][1];
				float rb = b.Extent[0] * AbsR[1][2] + b.Extent[2] * AbsR[1][0];
				if (fabsf(tA[0] * R[2][1] - tA[2] * R[0][1]) > ra + rb) return false;
			}
			// i=1,j=2
			{
				float ra = a.Extent[0] * AbsR[2][2] + a.Extent[2] * AbsR[0][2];
				float rb = b.Extent[0] * AbsR[1][1] + b.Extent[1] * AbsR[1][0];
				if (fabsf(tA[0] * R[2][2] - tA[2] * R[0][2]) > ra + rb) return false;
			}
			// i=2,j=0
			{
				float ra = a.Extent[0] * AbsR[1][0] + a.Extent[1] * AbsR[0][0];
				float rb = b.Extent[1] * AbsR[2][2] + b.Extent[2] * AbsR[2][1];
				if (fabsf(tA[1] * R[0][0] - tA[0] * R[1][0]) > ra + rb) return false;
			}
			// i=2,j=1
			{
				float ra = a.Extent[0] * AbsR[1][1] + a.Extent[1] * AbsR[0][1];
				float rb = b.Extent[0] * AbsR[2][2] + b.Extent[2] * AbsR[2][0];
				if (fabsf(tA[1] * R[0][1] - tA[0] * R[1][1]) > ra + rb) return false;
			}
			// i=2,j=2
			{
				float ra = a.Extent[0] * AbsR[1][2] + a.Extent[1] * AbsR[0][2];
				float rb = b.Extent[0] * AbsR[2][1] + b.Extent[1] * AbsR[2][0];
				if (fabsf(tA[1] * R[0][2] - tA[0] * R[1][2]) > ra + rb) return false;
			}

			return true;
		}

		bool Intersects(const Box3f& box, const Frustum3f& frustum)
		{
			return frustum.Intersects(box);
		}

		bool Intersects(const Frustum3f& a, const Frustum3f& b)
		{
			// Convex intersection via alternating projection onto 12 half-spaces.
			Vector3f p = Vector3f::ZERO;
			for (int iter = 0; iter < 48; ++iter)
			{
				bool satisfied = true;
				for (const auto& plane : a.Planes)
				{
					float d = plane.DistanceToPoint(p);
					if (d < 0.0f)
					{
						p += plane.Normal() * (-d);
						satisfied = false;
					}
				}
				for (const auto& plane : b.Planes)
				{
					float d = plane.DistanceToPoint(p);
					if (d < 0.0f)
					{
						p += plane.Normal() * (-d);
						satisfied = false;
					}
				}
				if (satisfied)
					return true;
			}
			return a.Contains(p) && b.Contains(p);
		}

		// ─────────────────────────────────────────────────────────────────────
		// Ray queries		// ─────────────────────────────────────────────────────────────────────

		RayHit3D Intersect(const Ray3f& ray, const Sphere3f& sphere)
		{
			RayHit3D hit;
			float t = 0.0f;
			if (!ray.IntersectSphere(sphere, t))
				return hit;

			hit.hit = true;
			hit.t = t;
			hit.point = ray.PointAt(t);
			hit.normal = hit.point - sphere.Center;
			hit.normal.Normalize();
			return hit;
		}

		RayHit3D Intersect(const Ray3f& ray, const Aabb3f& box)
		{
			RayHit3D hit;
			float tMin = 0.0f, tMax = 0.0f;
			if (!ray.IntersectAabb(box, tMin, tMax))
				return hit;

			hit.hit = true;
			hit.t = tMin;
			hit.tExit = tMax;
			hit.point = ray.PointAt(tMin);

			// Face normal from closest slab
			Vector3f p = hit.point;
			const float eps = 1.0e-4f;
			if (fabsf(p.x - box.Min.x) < eps) hit.normal = Vector3f(-1, 0, 0);
			else if (fabsf(p.x - box.Max.x) < eps) hit.normal = Vector3f(1, 0, 0);
			else if (fabsf(p.y - box.Min.y) < eps) hit.normal = Vector3f(0, -1, 0);
			else if (fabsf(p.y - box.Max.y) < eps) hit.normal = Vector3f(0, 1, 0);
			else if (fabsf(p.z - box.Min.z) < eps) hit.normal = Vector3f(0, 0, -1);
			else hit.normal = Vector3f(0, 0, 1);
			return hit;
		}

		RayHit3D Intersect(const Ray3f& ray, const Box3f& box)
		{
			RayHit3D hit;
			float t0 = 0.0f;
			float t1 = 1.0e10f;

			Vector3f diff = ray.Origin - box.Center;
			Vector3f bOrigin(
				diff.Dot(box.Axis[0]),
				diff.Dot(box.Axis[1]),
				diff.Dot(box.Axis[2]));
			Vector3f bDir(
				ray.Direction.Dot(box.Axis[0]),
				ray.Direction.Dot(box.Axis[1]),
				ray.Direction.Dot(box.Axis[2]));

			bool ok =
				ClipRaySlab(+bDir.x, -bOrigin.x - box.Extent[0], t0, t1) &&
				ClipRaySlab(-bDir.x, +bOrigin.x - box.Extent[0], t0, t1) &&
				ClipRaySlab(+bDir.y, -bOrigin.y - box.Extent[1], t0, t1) &&
				ClipRaySlab(-bDir.y, +bOrigin.y - box.Extent[1], t0, t1) &&
				ClipRaySlab(+bDir.z, -bOrigin.z - box.Extent[2], t0, t1) &&
				ClipRaySlab(-bDir.z, +bOrigin.z - box.Extent[2], t0, t1);

			if (!ok || t1 < 0.0f)
				return hit;

			hit.hit = true;
			hit.t = (t0 >= 0.0f) ? t0 : 0.0f;
			hit.tExit = t1;
			hit.point = ray.PointAt(hit.t);

			// Normal from dominant box-local face at entry
			Vector3f local = hit.point - box.Center;
			float ax = fabsf(local.Dot(box.Axis[0])) / std::max(box.Extent[0], Epsilon);
			float ay = fabsf(local.Dot(box.Axis[1])) / std::max(box.Extent[1], Epsilon);
			float az = fabsf(local.Dot(box.Axis[2])) / std::max(box.Extent[2], Epsilon);
			if (ax >= ay && ax >= az)
				hit.normal = box.Axis[0] * (local.Dot(box.Axis[0]) >= 0.0f ? 1.0f : -1.0f);
			else if (ay >= az)
				hit.normal = box.Axis[1] * (local.Dot(box.Axis[1]) >= 0.0f ? 1.0f : -1.0f);
			else
				hit.normal = box.Axis[2] * (local.Dot(box.Axis[2]) >= 0.0f ? 1.0f : -1.0f);
			return hit;
		}

		RayHit3D Intersect(const Ray3f& ray, const Frustum3f& frustum)
		{
			// Clip ray against 6 half-spaces (inward normals): n·x + d >= 0
			RayHit3D hit;
			float t0 = 0.0f;
			float t1 = 1.0e10f;

			for (const auto& plane : frustum.Planes)
			{
				// n·(o + t d) + d >= 0  →  t (n·dir) >= -(n·o+d)
				float nd = plane.Normal().Dot(ray.Direction);
				float no = plane.DistanceToPoint(ray.Origin);

				if (nd > Epsilon)
				{
					// t >= -no/nd
					float tEnter = -no / nd;
					if (tEnter > t0) t0 = tEnter;
				}
				else if (nd < -Epsilon)
				{
					// t <= -no/nd
					float tLeave = -no / nd;
					if (tLeave < t1) t1 = tLeave;
				}
				else if (no < 0.0f)
				{
					// parallel and outside
					return hit;
				}

				if (t0 > t1)
					return hit;
			}

			if (t1 < 0.0f)
				return hit;

			hit.hit = true;
			hit.t = (t0 >= 0.0f) ? t0 : 0.0f;
			hit.tExit = t1;
			hit.point = ray.PointAt(hit.t);
			return hit;
		}

		// ─────────────────────────────────────────────────────────────────────
		// 2D
		// ─────────────────────────────────────────────────────────────────────

		static Vector2f ClosestPointOnAabb2(const Vector2f& p, const Aabb2f& box)
		{
			return Vector2f(
				std::max(box.Min.x, std::min(p.x, box.Max.x)),
				std::max(box.Min.y, std::min(p.y, box.Max.y)));
		}

		static Vector2f ClosestPointOnObb2(const Vector2f& p, const Box2f& box)
		{
			Vector2f d = p - box.Center;
			Vector2f result = box.Center;
			for (int i = 0; i < 2; ++i)
			{
				float dist = d.Dot(box.Axis[i]);
				dist = std::max(-box.Extent[i], std::min(dist, box.Extent[i]));
				result += box.Axis[i] * dist;
			}
			return result;
		}

		bool Intersects(const Vector2f& point, const Sphere2f& circle)
		{
			return circle.Contains(point);
		}

		bool Intersects(const Vector2f& point, const Aabb2f& box)
		{
			return box.Contains(point);
		}

		bool Intersects(const Vector2f& point, const Box2f& box)
		{
			return box.Contains(point);
		}

		bool Intersects(const Sphere2f& a, const Sphere2f& b)
		{
			return a.Intersects(b);
		}

		bool Intersects(const Sphere2f& circle, const Aabb2f& box)
		{
			return box.Intersects(circle);
		}

		bool Intersects(const Sphere2f& circle, const Box2f& box)
		{
			Vector2f c = ClosestPointOnObb2(circle.Center, box);
			Vector2f d = c - circle.Center;
			return d.MagnitudeSqrd() <= circle.Radius * circle.Radius;
		}

		bool Intersects(const Aabb2f& a, const Aabb2f& b)
		{
			return a.Intersects(b);
		}

		bool Intersects(const Aabb2f& aabb, const Box2f& box)
		{
			return Intersects(Box2f::FromAabb(aabb), box);
		}

		bool Intersects(const Box2f& a, const Box2f& b)
		{
			// 2D SAT
			const Box2f* boxes[2] = { &a, &b };
			for (int bi = 0; bi < 2; ++bi)
			{
				for (int i = 0; i < 2; ++i)
				{
					Vector2f axis = boxes[bi]->Axis[i];
					float minA = 1.0e20f, maxA = -1.0e20f;
					float minB = 1.0e20f, maxB = -1.0e20f;

					Vector2f ca[4], cb[4];
					a.GetCorners(ca);
					b.GetCorners(cb);
					for (int k = 0; k < 4; ++k)
					{
						float pa = ca[k].Dot(axis);
						float pb = cb[k].Dot(axis);
						minA = std::min(minA, pa); maxA = std::max(maxA, pa);
						minB = std::min(minB, pb); maxB = std::max(maxB, pb);
					}
					if (maxA < minB || maxB < minA)
						return false;
				}
			}
			return true;
		}

		RayHit2D Intersect(const Ray2f& ray, const Sphere2f& circle)
		{
			RayHit2D hit;
			float t = 0.0f;
			if (!ray.IntersectCircle(circle, t))
				return hit;
			hit.hit = true;
			hit.t = t;
			hit.point = ray.PointAt(t);
			hit.normal = hit.point - circle.Center;
			hit.normal.Normalize();
			return hit;
		}

		RayHit2D Intersect(const Ray2f& ray, const Aabb2f& box)
		{
			RayHit2D hit;
			float tMin = 0.0f, tMax = 0.0f;
			if (!ray.IntersectAabb(box, tMin, tMax))
				return hit;
			hit.hit = true;
			hit.t = tMin;
			hit.tExit = tMax;
			hit.point = ray.PointAt(tMin);
			return hit;
		}

		RayHit2D Intersect(const Ray2f& ray, const Box2f& box)
		{
			RayHit2D hit;
			float t0 = 0.0f;
			float t1 = 1.0e10f;

			Vector2f diff = ray.Origin - box.Center;
			Vector2f bOrigin(diff.Dot(box.Axis[0]), diff.Dot(box.Axis[1]));
			Vector2f bDir(ray.Direction.Dot(box.Axis[0]), ray.Direction.Dot(box.Axis[1]));

			bool ok =
				ClipRaySlab(+bDir.x, -bOrigin.x - box.Extent[0], t0, t1) &&
				ClipRaySlab(-bDir.x, +bOrigin.x - box.Extent[0], t0, t1) &&
				ClipRaySlab(+bDir.y, -bOrigin.y - box.Extent[1], t0, t1) &&
				ClipRaySlab(-bDir.y, +bOrigin.y - box.Extent[1], t0, t1);

			if (!ok || t1 < 0.0f)
				return hit;

			hit.hit = true;
			hit.t = (t0 >= 0.0f) ? t0 : 0.0f;
			hit.tExit = t1;
			hit.point = ray.PointAt(hit.t);
			return hit;
		}
	}
}
