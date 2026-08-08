#pragma once

#include <cfloat>
#include <cmath>

namespace Dark
{
	namespace Math
	{
		enum class Axis
		{
			X, Y, Z, W
		};

		enum class Color
		{
			R, G, B, A
		};

		// Common constants
		constexpr float Pi          = 3.14159265358979323846f;
		constexpr float TwoPi       = 6.28318530717958647692f;
		constexpr float HalfPi      = 1.57079632679489661923f;
		constexpr float InvPi       = 0.31830988618379067154f;
		constexpr float DegToRad    = Pi / 180.0f;
		constexpr float RadToDeg    = 180.0f / Pi;
		constexpr float Epsilon     = 1.0e-6f;
		constexpr float Infinity    = FLT_MAX;
		constexpr float NegInfinity = -FLT_MAX;
	}
}
