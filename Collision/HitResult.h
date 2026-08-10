#pragma once

#include "Math/Vector3f.h"
#include "Math/Vector2f.h"

namespace Dark
{
	namespace Collision
	{

		// 2D variants
		struct StaticHit2D
		{
			bool hit = false;
			Math::Vector2f point = Math::Vector2f::ZERO;
			Math::Vector2f normal = Math::Vector2f::ZERO;
			float          depth = 0.0f;

			//explicit operator bool() const { return hit; }
		};

		struct RayHit2D
		{
			bool  hit = false;
			float t = 0.0f;
			float tExit = 0.0f;
			Math::Vector2f point = Math::Vector2f::ZERO;
			Math::Vector2f normal = Math::Vector2f::ZERO;

			//explicit operator bool() const { return hit; }
		};

		struct SweptHit2D
		{
			bool  hit = false;
			float t = 1.0f;
			Math::Vector2f point = Math::Vector2f::ZERO;
			Math::Vector2f normal = Math::Vector2f::ZERO;

			//explicit operator bool() const { return hit; }
		};


		// Static pair test (no motion).
		struct StaticHit3D
		{
			bool hit = false;

			// Optional geometric detail (filled when available).
			Math::Vector3f point  = Math::Vector3f::ZERO; // contact / closest on first shape
			Math::Vector3f normal = Math::Vector3f::ZERO; // from B toward A when meaningful
			float          depth  = 0.0f;                 // penetration depth (>=0) when overlapping

			//explicit operator bool() const { return hit; }
		};

		// Parametric / ray query: t is distance along the ray (if direction is unit).
		struct RayHit3D
		{
			bool  hit = false;
			float t   = 0.0f; // first intersection, t >= 0
			float tExit = 0.0f; // second hit for segment entry/exit (optional)
			Math::Vector3f point  = Math::Vector3f::ZERO;
			Math::Vector3f normal = Math::Vector3f::ZERO;

			//explicit operator bool() const { return hit; }
		};

		// Continuous (linear motion) test over time interval [0, 1].
		// Positions: P(t) = P0 + delta * t
		// t is first time of impact in [0, 1]. If already overlapping at t=0, t=0.
		struct SweptHit3D
		{
			bool  hit = false;
			float t   = 1.0f; // TOI; 1 means no hit in the interval when hit==false
			Math::Vector3f point  = Math::Vector3f::ZERO;
			Math::Vector3f normal = Math::Vector3f::ZERO; // contact normal at TOI (B→A)

			//explicit operator bool() const { return hit; }
		};

	}
}
