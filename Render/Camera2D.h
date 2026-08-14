#pragma once

#include "Math/Vector2f.h"
#include "Math/Matrix4f.h"
#include "Math/Ray2f.h"
#include "Math/Aabb2f.h"

namespace Dark
{
    // Orthographic 2D camera for top-down / side-view / UI-style worlds.
    // Looks along +Z (left-handed). World X right, Y up.
    // Zoom > 1 zooms in (smaller visible area).
    class Camera2D
    {
    public:
        Camera2D();

        // ── Transform ─────────────────────────────────────────────────────────
        const Math::Vector2f& GetPosition() const
        {
            return m_Position;
        }
        float GetRotation() const
        {
            return m_Rotation;
        } // radians, CCW
        float GetZoom() const
        {
            return m_Zoom;
        }

        void SetPosition(float x, float y);
        void SetPosition(const Math::Vector2f& pos);
        void SetRotation(float radians);
        void SetZoom(float zoom);

        void Move(const Math::Vector2f& delta);
        void MoveLocal(float right, float up); // relative to camera rotation
        void Rotate(float deltaRadians);
        void ZoomBy(float factor); // multiplies zoom (e.g. 1.1 = 10% in)

        // ── Lens (orthographic) ───────────────────────────────────────────────
        // Visible world height at zoom == 1. Width is height * aspect.
        float GetOrthoHeight() const
        {
            return m_OrthoHeight;
        }
        float GetAspect() const
        {
            return m_Aspect;
        }
        float GetNearZ() const
        {
            return m_NearZ;
        }
        float GetFarZ() const
        {
            return m_FarZ;
        }

        void SetOrthoHeight(float worldHeightAtZoom1);
        void SetAspect(float aspect);
        // Convenience: set aspect from pixel viewport.
        void SetViewportSize(float widthPx, float heightPx);
        void SetClipPlanes(float nearZ, float farZ);

        // World-space size currently visible (accounts for zoom).
        float        GetVisibleHeight() const;
        float        GetVisibleWidth() const;
        Math::Aabb2f GetVisibleBounds() const;

        // ── Matrices (getters rebuild if dirty) ───────────────────────────────
        void UpdateMatrices() const;

        const Math::Matrix4f& GetView() const;
        const Math::Matrix4f& GetProj() const;
        Math::Matrix4f        GetViewProj() const;

        // ── Screen / world (viewport size in pixels, origin top-left Y-down) ──
        Math::Vector2f WorldToScreen(const Math::Vector2f& world, float viewportW, float viewportH) const;
        Math::Vector2f ScreenToWorld(const Math::Vector2f& screen, float viewportW, float viewportH) const;
        Math::Ray2f    ScreenPointToRay(float screenX, float screenY, float viewportW, float viewportH) const;

    private:
        void RebuildView() const;
        void RebuildProj() const;

        Math::Vector2f m_Position;
        float          m_Rotation; // radians
        float          m_Zoom;

        float m_OrthoHeight; // world units vertical span at zoom 1
        float m_Aspect;
        float m_NearZ;
        float m_FarZ;

        mutable bool m_ViewDirty;
        mutable bool m_ProjDirty;

        mutable Math::Matrix4f m_View;
        mutable Math::Matrix4f m_Proj;
    };
} // namespace Dark
