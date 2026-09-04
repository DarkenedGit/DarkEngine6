#pragma once

#include "Math/Vector3f.h"
#include "Math/Vector2f.h"
#include "Math/Vector4f.h"
#include "Math/Matrix4f.h"
#include "Math/Ray3f.h"

#include <cstdint>

namespace Dark
{
    // Perspective / orthographic 3D camera (left-handed, row-vector, D3D-style).
    // View matrix is rebuilt lazily by GetView / GetViewProj.
    class Camera3D
    {
    public:
        Camera3D();

        // ── Position / basis ──────────────────────────────────────────────────
        const Math::Vector3f& GetPosition() const
        {
            return m_Position;
        }
        const Math::Vector3f& GetRight() const
        {
            return m_Right;
        }
        const Math::Vector3f& GetUp() const
        {
            return m_Up;
        }
        const Math::Vector3f& GetLook() const
        {
            return m_Look;
        }

        void SetPosition(float x, float y, float z);
        void SetPosition(const Math::Vector3f& pos);

        // ── Lens (projection) ─────────────────────────────────────────────────
        float GetNearZ() const
        {
            return m_NearZ;
        }
        float GetFarZ() const
        {
            return m_FarZ;
        }
        float GetAspect() const
        {
            return m_Aspect;
        }
        float GetFovY() const
        {
            return m_FovY;
        } // radians
        float GetFovX() const;

        float GetNearWindowWidth() const;
        float GetNearWindowHeight() const
        {
            return m_NearWindowHeight;
        }
        float GetFarWindowWidth() const;
        float GetFarWindowHeight() const
        {
            return m_FarWindowHeight;
        }

        // Perspective: fovY in radians.
        void SetLens(float fovY, float aspect, float nearZ, float farZ);

        // Orthographic: width/height in world units at the projection plane.
        void SetOrthographic(float width, float height, float nearZ, float farZ);

        // ── Orientation ───────────────────────────────────────────────────────
        void LookAt(const Math::Vector3f& pos, const Math::Vector3f& target, const Math::Vector3f& worldUp);

        // FPS-style movement / rotation (radians for angles).
        void Strafe(float distance); // along right
        void Walk(float distance);   // along look
        void Climb(float distance);  // along world up (Y)
        void Pitch(float angle);     // rotate about right
        void RotateY(float angle);   // rotate about world Y
        void Yaw(float angle);       // rotate about local up

        // Rebuild view matrix from basis if dirty (also called by getters).
        void UpdateViewMatrix() const;

        // Subpixel NDC jitter for TAA. pixelX/Y in [-0.5, 0.5]. Call after SetLens.
        void SetSubpixelJitter(float pixelX, float pixelY, uint32_t width, uint32_t height);
        void ClearSubpixelJitter();

        // ── Matrices ──────────────────────────────────────────────────────────
        const Math::Matrix4f& GetView() const;
        const Math::Matrix4f& GetProj() const
        {
            return m_Proj;
        }
        Math::Matrix4f GetViewProj() const;

        // ── Screen / world helpers (viewport size in pixels) ──────────────────
        // screen origin top-left, Y down (typical UI); NDC Y up.
        Math::Vector3f WorldToScreen(const Math::Vector3f& world, float viewportW, float viewportH) const;
        Math::Ray3f    ScreenPointToRay(float screenX, float screenY, float viewportW, float viewportH) const;

    private:
        void RebuildProjPerspective();
        void RebuildProjOrthographic();

        Math::Vector3f m_Position;

        float m_NearZ;
        float m_FarZ;
        float m_Aspect;
        float m_FovY;
        float m_NearWindowHeight;
        float m_FarWindowHeight;

        // Orthographic cache
        bool  m_Orthographic;
        float m_OrthoWidth;
        float m_OrthoHeight;

        mutable bool           m_ViewDirty;
        mutable Math::Vector3f m_Right;
        mutable Math::Vector3f m_Up;
        mutable Math::Vector3f m_Look;

        mutable Math::Matrix4f m_View;
        Math::Matrix4f         m_Proj;
        Math::Matrix4f         m_ProjUnjittered;
    };
} // namespace Dark
