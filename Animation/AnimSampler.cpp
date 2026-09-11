#include "Animation/AnimSampler.h"
#include "Math/MathDefines.h"
#include "Math/MathHelper.h"
#include "Core/Log.h"
#include <cmath>

namespace Dark
{
	using namespace Math;

	Matrix4f composeLocal(const Vector3f& t, const Quaternion& r, const Vector3f& s)
	{
		return Matrix4f::ScaleMatrixXYZ(s.x, s.y, s.z) * r.ToMatrix4() * Matrix4f::TranslationMatrix(t.x, t.y, t.z);
	}

	static int findKey(const std::vector<float>& times, float t)
	{
		const int n = static_cast<int>(times.size());
		if (n <= 0)
			return 0;
		if (t <= times[0])
			return 0;
		if (t >= times[n - 1])
			return n - 1;
		int lo = 0;
		int hi = n - 1;
		while (lo + 1 < hi)
		{
			const int mid = lo + (hi - lo) / 2;
			if (times[static_cast<size_t>(mid)] <= t)
				lo = mid;
			else
				hi = mid;
		}
		return lo;
	}

	static void sampleVec3(const AnimChannel& ch, float t, int comps, float* out)
	{
		const int n = static_cast<int>(ch.times.size());
		if (n <= 0)
			return;
		if (n == 1 || t <= ch.times[0] || ch.interp == AnimInterp::Step)
		{
			int k = 0;
			if (ch.interp == AnimInterp::Step)
				k = findKey(ch.times, t);
			if (ch.interp == AnimInterp::CubicSpline)
			{
				const int stride = 3 * comps;
				const float* src = ch.values.data() + static_cast<size_t>(k) * static_cast<size_t>(stride) + comps;
				for (int c = 0; c < comps; ++c)
					out[c] = src[c];
				return;
			}
			const float* src = ch.values.data() + static_cast<size_t>(k) * static_cast<size_t>(comps);
			for (int c = 0; c < comps; ++c)
				out[c] = src[c];
			return;
		}
		if (t >= ch.times[static_cast<size_t>(n - 1)])
		{
			if (ch.interp == AnimInterp::CubicSpline)
			{
				const int stride = 3 * comps;
				const float* src = ch.values.data() + static_cast<size_t>(n - 1) * static_cast<size_t>(stride) + comps;
				for (int c = 0; c < comps; ++c)
					out[c] = src[c];
				return;
			}
			const float* src = ch.values.data() + static_cast<size_t>(n - 1) * static_cast<size_t>(comps);
			for (int c = 0; c < comps; ++c)
				out[c] = src[c];
			return;
		}

		const int k = findKey(ch.times, t);
		const int k1 = k + 1;
		const float t0 = ch.times[static_cast<size_t>(k)];
		const float t1 = ch.times[static_cast<size_t>(k1)];
		const float dt = t1 - t0;
		const float u = (dt > Epsilon) ? ((t - t0) / dt) : 0.0f;

		if (ch.interp == AnimInterp::CubicSpline)
		{
			const int stride = 3 * comps;
			const float* vk = ch.values.data() + static_cast<size_t>(k) * static_cast<size_t>(stride) + comps;
			const float* mk = vk + comps; // out-tangent k
			const float* nk1 = ch.values.data() + static_cast<size_t>(k1) * static_cast<size_t>(stride); // in-tangent k+1
			const float* vk1 = nk1 + comps;
			const float u2 = u * u;
			const float u3 = u2 * u;
			const float c0 = 2.0f * u3 - 3.0f * u2 + 1.0f;
			const float c1 = u3 - 2.0f * u2 + u;
			const float c2 = -2.0f * u3 + 3.0f * u2;
			const float c3 = u3 - u2;
			for (int c = 0; c < comps; ++c)
				out[c] = c0 * vk[c] + c1 * dt * mk[c] + c2 * vk1[c] + c3 * dt * nk1[c];
			return;
		}

		const float* a = ch.values.data() + static_cast<size_t>(k) * static_cast<size_t>(comps);
		const float* b = ch.values.data() + static_cast<size_t>(k1) * static_cast<size_t>(comps);
		for (int c = 0; c < comps; ++c)
			out[c] = Lerp(a[c], b[c], u);
	}

	bool sampleClipLocal(
		const AnimationClip& clip,
		const Skeleton& skeleton,
		float timeSec,
		Vector3f* outT,
		Quaternion* outR,
		Vector3f* outS)
	{
		const uint32_t n = static_cast<uint32_t>(skeleton.joints.size());
		if (!outT || !outR || !outS)
		{
			DE_LOG_ERROR("sampleClipLocal: null pose buffers");
			return false;
		}
		if (n == 0)
			return false;
		if (clip.channels.empty())
			return false;

		for (uint32_t i = 0; i < n; ++i)
		{
			outT[i] = skeleton.joints[i].restT;
			outR[i] = skeleton.joints[i].restR;
			outS[i] = skeleton.joints[i].restS;
		}

		for (const AnimChannel& ch : clip.channels)
		{
			if (ch.joint >= n || ch.times.empty() || ch.values.empty())
				return false;
			if (ch.path == AnimPath::Translation)
			{
				float tmp[3]{};
				sampleVec3(ch, timeSec, 3, tmp);
				outT[ch.joint] = Vector3f(tmp[0], tmp[1], tmp[2]);
			}
			else if (ch.path == AnimPath::Scale)
			{
				float tmp[3]{};
				sampleVec3(ch, timeSec, 3, tmp);
				outS[ch.joint] = Vector3f(tmp[0], tmp[1], tmp[2]);
			}
			else
			{
				Quaternion q = Quaternion::IDENTITY;
				if (ch.interp == AnimInterp::Linear && ch.times.size() >= 2
					&& timeSec > ch.times.front() && timeSec < ch.times.back())
				{
					const int k = findKey(ch.times, timeSec);
					const int k1 = k + 1;
					const float t0 = ch.times[static_cast<size_t>(k)];
					const float t1 = ch.times[static_cast<size_t>(k1)];
					const float dt = t1 - t0;
					const float u = (dt > Epsilon) ? ((timeSec - t0) / dt) : 0.0f;
					const float* a = ch.values.data() + static_cast<size_t>(k) * 4u;
					const float* b = ch.values.data() + static_cast<size_t>(k1) * 4u;
					q = Quaternion::Slerp(
						Quaternion(a[0], a[1], a[2], a[3]),
						Quaternion(b[0], b[1], b[2], b[3]),
						u);
				}
				else
				{
					float tmp[4]{};
					sampleVec3(ch, timeSec, 4, tmp);
					q = Quaternion(tmp[0], tmp[1], tmp[2], tmp[3]);
					q.Normalize();
				}
				outR[ch.joint] = q;
			}
		}
		return true;
	}

	void localToJointWorlds(
		const Skeleton& skeleton,
		const Vector3f* T,
		const Quaternion* R,
		const Vector3f* S,
		Matrix4f* outWorlds)
	{
		const uint32_t n = static_cast<uint32_t>(skeleton.joints.size());
		if (!outWorlds || n == 0)
			return;
		const uint32_t count = (n > AnimPose::kMaxBones) ? AnimPose::kMaxBones : n;
		for (uint32_t step = 0; step < skeleton.fkOrder.size() && step < count; ++step)
		{
			const uint32_t j = skeleton.fkOrder[step];
			if (j >= count)
				continue;
			const Matrix4f local = composeLocal(T[j], R[j], S[j]);
			const int32_t parent = skeleton.joints[j].parent;
			const Matrix4f parentWorld = (parent >= 0 && static_cast<uint32_t>(parent) < count)
				? outWorlds[parent]
				: Matrix4f();
			outWorlds[j] = local * skeleton.joints[j].ancestorBindWorld * parentWorld;
		}
	}

	void localToPalette(
		const Skeleton& skeleton,
		const Vector3f* T,
		const Quaternion* R,
		const Vector3f* S,
		AnimPose& pose)
	{
		const uint32_t n = static_cast<uint32_t>(skeleton.joints.size());
		pose.boneCount = n;
		if (n == 0)
			return;

		Matrix4f worlds[AnimPose::kMaxBones];
		localToJointWorlds(skeleton, T, R, S, worlds);
		const uint32_t count = (n > AnimPose::kMaxBones) ? AnimPose::kMaxBones : n;
		const Matrix4f invMesh = skeleton.meshWorld.Inverse();
		for (uint32_t j = 0; j < count; ++j)
		{
			pose.jointWorld[j] = worlds[j];
			const Matrix4f jointWorldMesh = invMesh * worlds[j];
			pose.palette[j] = skeleton.joints[j].inverseBind * jointWorldMesh;
		}
	}
}
