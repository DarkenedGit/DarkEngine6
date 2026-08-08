#include "MathHelper.h"

namespace Dark
{
	namespace Math
	{
		float AngleFromXY(float x, float y)
		{
			float theta = 0.0f;

			// Quadrant I or IV
			if (x >= 0.0f)
			{
				// If x = 0, then atanf(y/x) = +pi/2 if y > 0
				//                atanf(y/x) = -pi/2 if y < 0
				theta = atanf(y / x); // in [-pi/2, +pi/2]

				if (theta < 0.0f)
					theta += TwoPi; // in [0, 2*pi).
			}
			// Quadrant II or III
			else
			{
				theta = atanf(y / x) + Pi; // in [0, 2*pi).
			}

			return theta;
		}
	}
}
