#include "Animation/SkeletonDebug.h"

namespace Dark
{
	using namespace Math;

	namespace
	{
		void appendAxis(std::vector<Vector3f>& dst, const Vector3f& origin, const Vector3f& basis, float axisLength)
		{
			const float mag = basis.Magnitude();
			if (mag <= 1.0e-8f || axisLength <= 0.0f)
				return;
			dst.push_back(origin);
			dst.push_back(origin + basis * (axisLength / mag));
		}
	} // namespace

	void collectSkeletonDebugLines(
		const Skeleton& skeleton,
		const AnimPose& pose,
		const Matrix4f& entityWorld,
		float axisLength,
		SkeletonDebugLines& out)
	{
		out.bones.clear();
		out.axisX.clear();
		out.axisY.clear();
		out.axisZ.clear();

		const uint32_t n = pose.boneCount;
		if (n == 0 || n > AnimPose::kMaxBones)
			return;
		const uint32_t jointN = static_cast<uint32_t>(skeleton.joints.size());
		const uint32_t count = (n < jointN) ? n : jointN;

		for (uint32_t j = 0; j < count; ++j)
		{
			const Matrix4f world = pose.jointWorld[j] * entityWorld;
			const Vector3f origin = world.GetTranslation();
			appendAxis(out.axisX, origin, world.GetBasisX(), axisLength);
			appendAxis(out.axisY, origin, world.GetBasisY(), axisLength);
			appendAxis(out.axisZ, origin, world.GetBasisZ(), axisLength);

			const int32_t parent = skeleton.joints[j].parent;
			if (parent < 0 || static_cast<uint32_t>(parent) >= count)
				continue;
			const Vector3f parentOrigin = (pose.jointWorld[static_cast<uint32_t>(parent)] * entityWorld).GetTranslation();
			out.bones.push_back(parentOrigin);
			out.bones.push_back(origin);
		}
	}
}
