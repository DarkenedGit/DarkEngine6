#pragma once

#include <array>
#include "Math/Plane4f.h"
#include "Math/Vector3f.h"
#include "Math/Sphere3f.h"
#include "Math/Aabb3f.h"
#include "Math/Box3f.h"
#include "Math/Matrix4f.h"

namespace Dark
{
	namespace Math
	{
		// View frustum: six planes with normals pointing *inward*.
		// Plane order: Left, Right, Top, Bottom, Near, Far.
		class Frustum3f
		{
		public:
			enum PlaneIndex
			{
				Left = 0,
				Right,
				Top,
				Bottom,
				Near,
				Far,
				Count
			};

			std::array<Plane4f, Count> Planes;

			Frustum3f();
			explicit Frustum3f(const Matrix4f& viewProjection, bool normalize = true);

			// Build planes from a combined view*projection matrix (row-vector, D3D LH).
			void Update(const Matrix4f& viewProjection, bool normalize = true);

			const Plane4f& GetPlane(int index) const { return Planes[index]; }

			// Classification
			bool Contains(const Vector3f& point) const;
			bool Intersects(const Sphere3f& sphere) const;
			bool Intersects(const Aabb3f& box) const;
			bool Intersects(const Box3f& box) const;
			bool Envelops(const Sphere3f& sphere) const; // fully inside
		};
	}
}
