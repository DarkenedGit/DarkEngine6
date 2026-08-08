#include "Camera3D.h"
#include "Math/MathDefines.h"
#include "Math/Matrix3f.h"
#include <cmath>

namespace Dark
{
	using namespace Math;

	Camera3D::Camera3D() : m_Position(Vector3f::ZERO), m_Right(Vector3f::X_AXIS), m_Up(Vector3f::Y_AXIS), m_Look(Vector3f::Z_AXIS),
						   m_NearZ(0.1f), m_FarZ(1000.0f), m_Aspect(1.0f), m_FovY(Pi * 0.25f), m_NearWindowHeight(0.0f), m_FarWindowHeight(0.0f),
						   m_Orthographic(false), m_OrthoWidth(10.0f), m_OrthoHeight(10.0f), m_ViewDirty(true), m_View(Mat4f::IDENTITY), m_Proj(Mat4f::IDENTITY)
	{
		SetLens(m_FovY, m_Aspect, m_NearZ, m_FarZ);
	}

	void Camera3D::SetPosition(float x, float y, float z)
	{
		m_Position = Vector3f(x, y, z);
		m_ViewDirty = true;
	}

	void Camera3D::SetPosition(const Vector3f& pos)
	{
		m_Position = pos;
		m_ViewDirty = true;
	}

	float Camera3D::GetFovX() const
	{
		float halfWidth = 0.5f * GetNearWindowWidth();
		return 2.0f * atanf(halfWidth / m_NearZ);
	}

	float Camera3D::GetNearWindowWidth() const
	{
		return m_Aspect * m_NearWindowHeight;
	}

	float Camera3D::GetFarWindowWidth() const
	{
		return m_Aspect * m_FarWindowHeight;
	}

	void Camera3D::SetLens(float fovY, float aspect, float nearZ, float farZ)
	{
		m_Orthographic = false;
		m_FovY   = fovY;
		m_Aspect = aspect;
		m_NearZ  = nearZ;
		m_FarZ   = farZ;

		m_NearWindowHeight = 2.0f * m_NearZ * tanf(0.5f * m_FovY);
		m_FarWindowHeight  = 2.0f * m_FarZ  * tanf(0.5f * m_FovY);

		RebuildProjPerspective();
	}

	void Camera3D::SetOrthographic(float width, float height, float nearZ, float farZ)
	{
		m_Orthographic = true;
		m_OrthoWidth  = width;
		m_OrthoHeight = height;
		m_NearZ       = nearZ;
		m_FarZ        = farZ;
		m_Aspect      = (height != 0.0f) ? (width / height) : 1.0f;

		m_NearWindowHeight = height;
		m_FarWindowHeight  = height;

		RebuildProjOrthographic();
	}

	void Camera3D::RebuildProjPerspective()
	{
		m_Proj = Matrix4f::PerspectiveFovLHMatrix(m_FovY, m_Aspect, m_NearZ, m_FarZ);
	}

	void Camera3D::RebuildProjOrthographic()
	{
		m_Proj = Matrix4f::OrthographicLHMatrix(m_NearZ, m_FarZ, m_OrthoWidth, m_OrthoHeight);
	}

	void Camera3D::LookAt(const Vector3f& pos, const Vector3f& target, const Vector3f& worldUp)
	{
		Vector3f look = target - pos;
		look.Normalize();

		Vector3f right = worldUp.Cross(look);
		right.Normalize();

		Vector3f up = look.Cross(right);

		m_Position = pos;
		m_Look     = look;
		m_Right    = right;
		m_Up       = up;
		m_ViewDirty = true;
	}

	void Camera3D::Strafe(float distance)
	{
		m_Position += m_Right * distance;
		m_ViewDirty = true;
	}

	void Camera3D::Walk(float distance)
	{
		m_Position += m_Look * distance;
		m_ViewDirty = true;
	}

	void Camera3D::Climb(float distance)
	{
		m_Position.y += distance;
		m_ViewDirty = true;
	}

	void Camera3D::Pitch(float angle)
	{
		Matrix3f R;
		R.RotationEuler(m_Right, angle);
		m_Up   = R * m_Up;
		m_Look = R * m_Look;
		m_ViewDirty = true;
	}

	void Camera3D::RotateY(float angle)
	{
		Matrix3f R;
		R.RotationY(angle);
		m_Right = R * m_Right;
		m_Up    = R * m_Up;
		m_Look  = R * m_Look;
		m_ViewDirty = true;
	}

	void Camera3D::Yaw(float angle)
	{
		Matrix3f R;
		R.RotationEuler(m_Up, angle);
		m_Right = R * m_Right;
		m_Look  = R * m_Look;
		m_ViewDirty = true;
	}

	void Camera3D::UpdateViewMatrix() const
	{
		if (!m_ViewDirty)
			return;

		// Orthonormalize basis
		m_Look.Normalize();
		m_Up = m_Look.Cross(m_Right);
		m_Up.Normalize();
		m_Right = m_Up.Cross(m_Look);

		// View matrix (row-vector, LH) — same layout as LookAtLHMatrix
		const float x = -m_Position.Dot(m_Right);
		const float y = -m_Position.Dot(m_Up);
		const float z = -m_Position.Dot(m_Look);

		m_View = Matrix4f(
			m_Right.x, m_Up.x, m_Look.x, 0.0f,
			m_Right.y, m_Up.y, m_Look.y, 0.0f,
			m_Right.z, m_Up.z, m_Look.z, 0.0f,
			x,         y,      z,        1.0f);

		m_ViewDirty = false;
	}

	const Matrix4f& Camera3D::GetView() const
	{
		UpdateViewMatrix();
		return m_View;
	}

	Matrix4f Camera3D::GetViewProj() const
	{
		UpdateViewMatrix();
		return m_View * m_Proj;
	}

	Vector3f Camera3D::WorldToScreen(const Vector3f& world, float viewportW, float viewportH) const
	{
		Vector4f clip = GetViewProj() * Vector4f(world, 1.0f);
		if (fabsf(clip.w) < Epsilon)
			return Vector3f(0.0f, 0.0f, 0.0f);

		float invW = 1.0f / clip.w;
		float ndcX = clip.x * invW;
		float ndcY = clip.y * invW;
		float ndcZ = clip.z * invW;

		// NDC [-1,1] → screen (origin top-left, Y down)
		float sx = (ndcX * 0.5f + 0.5f) * viewportW;
		float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportH;
		return Vector3f(sx, sy, ndcZ);
	}

	Ray3f Camera3D::ScreenPointToRay(float screenX, float screenY, float viewportW, float viewportH) const
	{
		// Screen → NDC
		float ndcX = (screenX / viewportW) * 2.0f - 1.0f;
		float ndcY = 1.0f - (screenY / viewportH) * 2.0f;

		Matrix4f invVP = GetViewProj().Inverse();

		Vector4f nearH = invVP * Vector4f(ndcX, ndcY, 0.0f, 1.0f);
		Vector4f farH  = invVP * Vector4f(ndcX, ndcY, 1.0f, 1.0f);

		Vector3f nearP = nearH.xyz() * (1.0f / nearH.w);
		Vector3f farP  = farH.xyz()  * (1.0f / farH.w);

		Vector3f dir = farP - nearP;
		dir.Normalize();
		return Ray3f(nearP, dir);
	}
}
