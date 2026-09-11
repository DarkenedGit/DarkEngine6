#include "Render/MeshGen2D.h"
#include "Core/Log.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include <cmath>

namespace Dark
{
	using namespace Math;

	static void pushIdx(MeshData& m, uint32_t a, uint32_t b, uint32_t c)
	{
		m.indices.push_back(a);
		m.indices.push_back(b);
		m.indices.push_back(c);
	}

	static void pushV(MeshData& m, const Vector2f& p, const Vector2f& uv)
	{
		m.positions.push_back({ p.x, p.y, 0.0f });
		m.normals.push_back({ 0.0f, 0.0f, 1.0f });
		m.uvs.push_back(uv);
	}

	static Vector2f uvFromAabb(const Vector2f& p, const AABox2f& box)
	{
		const Vector2f s = box.Size();
		const float u = (s.x > Epsilon) ? (p.x - box.Min.x) / s.x : 0.5f;
		const float v = (s.y > Epsilon) ? (box.Max.y - p.y) / s.y : 0.5f;
		return Vector2f(u, v);
	}

	bool CreateSphere2(MeshData& mesh, const Sphere2f& circle, int slices)
	{
		if (slices < 3)
		{
			DE_LOG_ERROR(LogCategory::Render, "CreateSphere2: slices must be >= 3");
			return false;
		}
		if (circle.Radius < 0.0f)
		{
			DE_LOG_ERROR(LogCategory::Render, "CreateSphere2: radius must be >= 0");
			return false;
		}

		const uint32_t center = static_cast<uint32_t>(mesh.positions.size());
		pushV(mesh, circle.Center, Vector2f(0.5f, 0.5f));

		const float invR = (circle.Radius > Epsilon) ? (0.5f / circle.Radius) : 0.0f;
		for (int i = 0; i < slices; ++i)
		{
			const float theta = TwoPi * static_cast<float>(i) / static_cast<float>(slices);
			const float c = cosf(theta);
			const float s = sinf(theta);
			const Vector2f p(circle.Center.x + circle.Radius * c, circle.Center.y + circle.Radius * s);
			pushV(mesh, p, Vector2f(0.5f + circle.Radius * c * invR, 0.5f - circle.Radius * s * invR));
		}

		for (int i = 0; i < slices; ++i)
		{
			const uint32_t a = center + 1u + static_cast<uint32_t>(i);
			const uint32_t b = center + 1u + static_cast<uint32_t>((i + 1) % slices);
			pushIdx(mesh, center, a, b);
		}
		return true;
	}

	bool CreateCapsule2(MeshData& mesh, const Capsule2f& capsule, int capSlices)
	{
		if (capSlices < 2)
		{
			DE_LOG_ERROR(LogCategory::Render, "CreateCapsule2: capSlices must be >= 2");
			return false;
		}
		if (capsule.Radius < 0.0f)
		{
			DE_LOG_ERROR(LogCategory::Render, "CreateCapsule2: radius must be >= 0");
			return false;
		}

		const Vector2f axis = capsule.Axis();
		if (axis.MagnitudeSqrd() <= Epsilon)
			return CreateSphere2(mesh, Sphere2f(capsule.PointA, capsule.Radius), capSlices * 2);

		const Vector2f perp = axis.Perpendicular();
		const Vector2f off = perp * capsule.Radius;
		const Vector2f leftA = capsule.PointA + off;
		const Vector2f rightA = capsule.PointA - off;
		const Vector2f rightB = capsule.PointB - off;
		const Vector2f leftB = capsule.PointB + off;
		const AABox2f uvBox = capsule.ToAABox();

		const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
		pushV(mesh, leftA, uvFromAabb(leftA, uvBox));
		pushV(mesh, rightA, uvFromAabb(rightA, uvBox));
		pushV(mesh, rightB, uvFromAabb(rightB, uvBox));
		pushV(mesh, leftB, uvFromAabb(leftB, uvBox));
		detail::pushQuad(mesh, base + 0, base + 1, base + 2, base + 3);

		const uint32_t centerA = static_cast<uint32_t>(mesh.positions.size());
		pushV(mesh, capsule.PointA, uvFromAabb(capsule.PointA, uvBox));
		uint32_t prev = base + 0;
		for (int k = 1; k < capSlices; ++k)
		{
			const float t = Pi * static_cast<float>(k) / static_cast<float>(capSlices);
			const Vector2f p = capsule.PointA + (perp * cosf(t) - axis * sinf(t)) * capsule.Radius;
			const uint32_t cur = static_cast<uint32_t>(mesh.positions.size());
			pushV(mesh, p, uvFromAabb(p, uvBox));
			pushIdx(mesh, centerA, prev, cur);
			prev = cur;
		}
		pushIdx(mesh, centerA, prev, base + 1);

		const uint32_t centerB = static_cast<uint32_t>(mesh.positions.size());
		pushV(mesh, capsule.PointB, uvFromAabb(capsule.PointB, uvBox));
		prev = base + 2;
		for (int k = 1; k < capSlices; ++k)
		{
			const float t = Pi * static_cast<float>(k) / static_cast<float>(capSlices);
			const Vector2f p = capsule.PointB + (-perp * cosf(t) + axis * sinf(t)) * capsule.Radius;
			const uint32_t cur = static_cast<uint32_t>(mesh.positions.size());
			pushV(mesh, p, uvFromAabb(p, uvBox));
			pushIdx(mesh, centerB, prev, cur);
			prev = cur;
		}
		pushIdx(mesh, centerB, prev, base + 3);
		return true;
	}

	bool CreateAABox2(MeshData& mesh, const AABox2f& box)
	{
		if (!box.IsValid())
		{
			DE_LOG_ERROR(LogCategory::Render, "CreateAABox2: box min must be <= max");
			return false;
		}

		const Vector2f bl(box.Min.x, box.Min.y);
		const Vector2f br(box.Max.x, box.Min.y);
		const Vector2f tr(box.Max.x, box.Max.y);
		const Vector2f tl(box.Min.x, box.Max.y);
		const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
		pushV(mesh, bl, Vector2f(0.0f, 1.0f));
		pushV(mesh, br, Vector2f(1.0f, 1.0f));
		pushV(mesh, tr, Vector2f(1.0f, 0.0f));
		pushV(mesh, tl, Vector2f(0.0f, 0.0f));
		detail::pushQuad(mesh, base + 0, base + 1, base + 2, base + 3);
		return true;
	}

	bool CreateBox2(MeshData& mesh, const Box2f& box)
	{
		Vector2f corners[4];
		box.GetCorners(corners);
		// GetCorners: 0 = -X-Y, 1 = +X-Y, 2 = -X+Y, 3 = +X+Y
		const Vector2f& bl = corners[0];
		const Vector2f& br = corners[1];
		const Vector2f& tl = corners[2];
		const Vector2f& tr = corners[3];
		const uint32_t base = static_cast<uint32_t>(mesh.positions.size());
		pushV(mesh, bl, Vector2f(0.0f, 1.0f));
		pushV(mesh, br, Vector2f(1.0f, 1.0f));
		pushV(mesh, tr, Vector2f(1.0f, 0.0f));
		pushV(mesh, tl, Vector2f(0.0f, 0.0f));
		detail::pushQuad(mesh, base + 0, base + 1, base + 2, base + 3);
		return true;
	}
}
