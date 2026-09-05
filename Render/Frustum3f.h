#pragma once

#include <array>
#include "Math/Plane4f.h"
#include "Math/Vector3f.h"
#include "Math/Sphere3f.h"
#include "Math/AABox3f.h"
#include "Math/Box3f.h"
#include "Math/Matrix4f.h"

namespace Dark
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

        std::array<Math::Plane4f, Count> Planes;

        Frustum3f();
        explicit Frustum3f(const Math::Matrix4f& viewProjection, bool normalize = true);

        // Build planes from a combined view*projection matrix (row-vector, D3D LH).
        void Update(const Math::Matrix4f& viewProjection, bool normalize = true);

        const Math::Plane4f& GetPlane(int index) const
        {
            return Planes[index];
        }

        // Classification
        bool Contains(const Math::Vector3f& point) const;
        bool Intersects(const Math::Sphere3f& sphere) const;
        bool Intersects(const Math::AABox3f& box) const;
        bool Intersects(const Math::Box3f& box) const;
        bool Envelops(const Math::Sphere3f& sphere) const; // fully inside
    };
} // namespace Dark
