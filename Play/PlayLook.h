#pragma once

#include "Math/MathHelper.h"
#include "Math/Vector3f.h"

#include <cmath>

namespace Dark
{

    struct PlayBasis
    {
        Math::Vector3f look{ Math::Vect3f::ZERO };
        Math::Vector3f flat{ Math::Vect3f::ZERO };
        Math::Vector3f right{ Math::Vect3f::ZERO };
        Math::Vector3f wish{ Math::Vect3f::ZERO };
    };

    // Mouse is pixels. Pad yaw/pitch are already signed by the caller (editor negates pad pitch).
    inline void accumulatePlayLook(float& yaw, float& pitch, float mouseX, float mouseY, bool applyMouse, float padYaw, float padPitch, float dt)
    {
        constexpr float kMouseSens = 0.0045f;
        constexpr float kPadLook   = 2.1f;
        if (applyMouse)
        {
            yaw += mouseX * kMouseSens;
            pitch += mouseY * kMouseSens;
        }
        yaw += padYaw * kPadLook * dt;
        pitch += padPitch * kPadLook * dt;
        yaw   = Math::WrapPi(yaw);
        pitch = Math::Clamp(pitch, -0.96f, 0.96f);
    }

    // Flat basis for wish. Look is not normalized; the shoulder camera normalizes its own copy.
    inline PlayBasis playBasis(float yaw, float pitch, float moveX, float moveZ)
    {
        PlayBasis basis;
        basis.look = Math::Vector3f{ std::sinf(yaw) * std::cosf(pitch), std::sinf(pitch), std::cosf(yaw) * std::cosf(pitch) };
        basis.flat = basis.look;
        basis.flat.y = 0.0f;
        if (basis.flat.MagnitudeSqrd() > 1.0e-6f)
            basis.flat.Normalize();
        basis.right = Math::Vector3f{ 0.0f, 1.0f, 0.0f }.Cross(basis.flat);
        if (basis.right.MagnitudeSqrd() > 1.0e-6f)
            basis.right.Normalize();
        basis.wish = basis.right * moveX + basis.flat * moveZ;
        const float mag = basis.wish.Magnitude();
        if (mag > 1.0f)
            basis.wish *= (1.0f / mag);
        return basis;
    }

} // namespace Dark
