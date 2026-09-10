#pragma once

#include "Vector2f.h"
#include "Sphere2f.h"

namespace Dark::Math
{
    // Axis-aligned box in 2D (min/max corners).
    class AABox2f
    {
    public:
        Vector2f Min;
        Vector2f Max;

        AABox2f();
        AABox2f(const Vector2f& min, const Vector2f& max);
        AABox2f(const AABox2f&) noexcept = default;
        AABox2f(AABox2f&&) noexcept = default;
        AABox2f& operator=(const AABox2f&) noexcept = default;
        AABox2f& operator=(AABox2f&&) noexcept = default;

        static AABox2f  FromCenterExtents(const Vector2f& center, const Vector2f& halfExtents);
        static AABox2f  FromPoints(const Vector2f* points, int count);
        static AABox2f  Empty(); // inverted empty box for progressive expansion

        Vector2f Center() const;
        Vector2f Extents() const; // half-size
        Vector2f Size() const;    // full size
        float    Perimeter() const;
        float    Area() const;

        bool IsValid() const; // Min <= Max on both axes

        bool Contains(const Vector2f& point) const;
        bool Contains(const AABox2f& other) const;
        bool Intersects(const AABox2f& other) const;
        bool Intersects(const Sphere2f& circle) const;

        void Expand(float amount);
        void ExpandToInclude(const Vector2f& point);
        void ExpandToInclude(const AABox2f& other);
        void ExpandToInclude(const Sphere2f& circle);

        void     GetCorners(Vector2f outCorners[4]) const;
        Sphere2f ToBoundingCircle() const;
    };
} // namespace Dark::Math
