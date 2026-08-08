#include "Camera2D.h"
#include "Math/MathDefines.h"
#include "Math/Vector4f.h"
#include <cmath>
#include <algorithm>

namespace Dark
{
	using namespace Math;

	Camera2D::Camera2D(): m_Position(Vector2f::ZERO), m_Rotation(0.0f), m_Zoom(1.0f), m_OrthoHeight(20.0f), m_Aspect(16.0f / 9.0f), m_NearZ(0.0f), m_FarZ(1000.0f),
						  m_ViewDirty(true), m_ProjDirty(true), m_View(Mat4f::IDENTITY), m_Proj(Mat4f::IDENTITY)
	{
		UpdateMatrices();
	}

	void Camera2D::SetPosition(float x, float y)
	{
		m_Position = Vector2f(x, y);
		m_ViewDirty = true;
	}

	void Camera2D::SetPosition(const Vector2f& pos)
	{
		m_Position = pos;
		m_ViewDirty = true;
	}

	void Camera2D::SetRotation(float radians)
	{
		m_Rotation = radians;
		m_ViewDirty = true;
	}

	void Camera2D::SetZoom(float zoom)
	{
		m_Zoom = std::max(zoom, Epsilon);
		m_ProjDirty = true; // zoom baked into orthographic height
	}

	void Camera2D::Move(const Vector2f& delta)
	{
		m_Position += delta;
		m_ViewDirty = true;
	}

	void Camera2D::MoveLocal(float right, float up)
	{
		float c = cosf(m_Rotation);
		float s = sinf(m_Rotation);
		// local +X = (c, s), local +Y = (-s, c)
		m_Position.x += right * c + up * (-s);
		m_Position.y += right * s + up * c;
		m_ViewDirty = true;
	}

	void Camera2D::Rotate(float deltaRadians)
	{
		m_Rotation += deltaRadians;
		m_ViewDirty = true;
	}

	void Camera2D::ZoomBy(float factor)
	{
		SetZoom(m_Zoom * factor);
	}

	void Camera2D::SetOrthoHeight(float worldHeightAtZoom1)
	{
		m_OrthoHeight = std::max(worldHeightAtZoom1, Epsilon);
		m_ProjDirty = true;
	}

	void Camera2D::SetAspect(float aspect)
	{
		m_Aspect = (aspect > Epsilon) ? aspect : 1.0f;
		m_ProjDirty = true;
	}

	void Camera2D::SetViewportSize(float widthPx, float heightPx)
	{
		if (heightPx > Epsilon)
			SetAspect(widthPx / heightPx);
	}

	void Camera2D::SetClipPlanes(float nearZ, float farZ)
	{
		m_NearZ = nearZ;
		m_FarZ  = farZ;
		m_ProjDirty = true;
	}

	float Camera2D::GetVisibleHeight() const
	{
		return m_OrthoHeight / m_Zoom;
	}

	float Camera2D::GetVisibleWidth() const
	{
		return GetVisibleHeight() * m_Aspect;
	}

	Aabb2f Camera2D::GetVisibleBounds() const
	{
		// Axis-aligned bounds of the view frustum (ignores camera rotation —
		// rotated cameras still report AABB of the OBB for culling broadphase).
		float hw = GetVisibleWidth()  * 0.5f;
		float hh = GetVisibleHeight() * 0.5f;

		if (fabsf(m_Rotation) < Epsilon)
		{
			return Aabb2f(
				Vector2f(m_Position.x - hw, m_Position.y - hh),
				Vector2f(m_Position.x + hw, m_Position.y + hh));
		}

		// Corners of rotated view rect → AABB
		float c = cosf(m_Rotation);
		float s = sinf(m_Rotation);
		Vector2f axes[2] = { Vector2f(c, s), Vector2f(-s, c) };
		Vector2f corners[4] = 
		{
			m_Position + axes[0] * (-hw) + axes[1] * (-hh),
			m_Position + axes[0] * ( hw) + axes[1] * (-hh),
			m_Position + axes[0] * (-hw) + axes[1] * ( hh),
			m_Position + axes[0] * ( hw) + axes[1] * ( hh),
		};
		return Aabb2f::FromPoints(corners, 4);
	}

	void Camera2D::RebuildView() const
	{
		// Camera world: T(pos) * R(z, rot)
		// View = R(-rot) * T(-pos)  (row-vector: v' = v * View)
		float c = cosf(-m_Rotation);
		float s = sinf(-m_Rotation);

		// Rotation Z * Translation(-pos)
		// R = | c  s  0  0 |
		//     |-s  c  0  0 |
		//     | 0  0  1  0 |
		//     | 0  0  0  1 |
		// T = | 1 0 0 0 |
		//     | 0 1 0 0 |
		//     | 0 0 1 0 |
		//     |-x -y 0 1|
		// View = T * R  (first translate into camera space, then rotate)
		// For row vectors: v * T * R
		m_View = Matrix4f(
			c,   s,  0.0f, 0.0f,
			-s,  c,  0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			-m_Position.x * c + m_Position.y * s,
			-m_Position.x * s - m_Position.y * c,
			0.0f, 1.0f);

		// Simpler explicit: Translation then rotation via multiply
		// v' = (v - pos) rotated by -rot
		// Row 3 translation above is R^T * (-pos) = correct for column... 
		// Verify: for rot=0, view translation should be -pos.
		// c=1,s=0 → tx = -x, ty = -y. Good.

		m_ViewDirty = false;
	}

	void Camera2D::RebuildProj() const
	{
		float height = GetVisibleHeight();
		float width  = height * m_Aspect;
		m_Proj = Matrix4f::OrthographicLHMatrix(m_NearZ, m_FarZ, width, height);
		m_ProjDirty = false;
	}

	void Camera2D::UpdateMatrices() const
	{
		if (m_ViewDirty)
			RebuildView();
		if (m_ProjDirty)
			RebuildProj();
	}

	const Matrix4f& Camera2D::GetView() const
	{
		UpdateMatrices();
		return m_View;
	}

	const Matrix4f& Camera2D::GetProj() const
	{
		UpdateMatrices();
		return m_Proj;
	}

	Matrix4f Camera2D::GetViewProj() const
	{
		UpdateMatrices();
		return m_View * m_Proj;
	}

	Vector2f Camera2D::WorldToScreen(const Vector2f& world, float viewportW, float viewportH) const
	{
		Vector4f clip = GetViewProj() * Vector4f(world.x, world.y, 0.0f, 1.0f);
		if (fabsf(clip.w) < Epsilon)
			return Vector2f::ZERO;

		float invW = 1.0f / clip.w;
		float ndcX = clip.x * invW;
		float ndcY = clip.y * invW;

		float sx = (ndcX * 0.5f + 0.5f) * viewportW;
		float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportH;
		return Vector2f(sx, sy);
	}

	Vector2f Camera2D::ScreenToWorld(const Vector2f& screen, float viewportW, float viewportH) const
	{
		float ndcX = (screen.x / viewportW) * 2.0f - 1.0f;
		float ndcY = 1.0f - (screen.y / viewportH) * 2.0f;

		Matrix4f invVP = GetViewProj().Inverse();
		Vector4f worldH = invVP * Vector4f(ndcX, ndcY, 0.0f, 1.0f);
		if (fabsf(worldH.w) < Epsilon)
			return m_Position;

		float invW = 1.0f / worldH.w;
		return Vector2f(worldH.x * invW, worldH.y * invW);
	}

	Ray2f Camera2D::ScreenPointToRay(float screenX, float screenY, float viewportW, float viewportH) const
	{
		// For 2D ortho, ray is typically a point pick; provide a forward ray along +X
		// through the world point for consistency with Ray2D queries.
		Vector2f world = ScreenToWorld(Vector2f(screenX, screenY), viewportW, viewportH);
		return Ray2f(world, Vector2f::X_AXIS);
	}
}
