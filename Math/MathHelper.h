#pragma once

#include "MathDefines.h"
#include <stdlib.h>
#include <math.h>
#include <algorithm>

namespace Dark
{
	namespace Math
	{
		// Returns random float in [0, 1).
		inline float RandF()
		{
			return static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		}

		// Returns random float in [a, b).
		inline float RandF(float a, float b)
		{
			return a + RandF() * (b - a);
		}

		// Returns random int in [a, b].
		inline int Rand(int a, int b)
		{
			return a + rand() % ((b - a) + 1);
		}

		template <typename T>
		inline T Min(const T& a, const T& b)
		{
			return a < b ? a : b;
		}

		template <typename T>
		inline T Max(const T& a, const T& b)
		{
			return a > b ? a : b;
		}

		template <typename T>
		inline T Lerp(const T& a, const T& b, float t)
		{
			return a + (b - a) * t;
		}

		template <typename T>
		inline T Clamp(const T& x, const T& low, const T& high)
		{
			return x < low ? low : (x > high ? high : x);
		}

		inline float DegreesToRadians(float degrees)
		{
			return degrees * DegToRad;
		}

		inline float RadiansToDegrees(float radians)
		{
			return radians * RadToDeg;
		}

		inline bool NearEqual(float a, float b, float epsilon = Epsilon)
		{
			return fabsf(a - b) <= epsilon;
		}

		// Returns the polar angle of the point (x,y) in [0, 2*PI).
		float AngleFromXY(float x, float y);

		// Smoothstep: Hermite interpolation between edge0 and edge1.
		inline float SmoothStep(float edge0, float edge1, float x)
		{
			float t = Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}
	}
}
