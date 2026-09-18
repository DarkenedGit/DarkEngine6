#include "Render/ModelDraw.h"
#include "Animation/AnimGraphComponent.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Render/GpuModel.h"
#include "Render/MaterialSurface.h"
#include "Render/ShadowSystem.h"

#include <cstring>

namespace Dark
{
	namespace
	{
		void copyMatrix(float dst[16], const Matrix4f& m)
		{
			std::memcpy(dst, m.m_afEntry, sizeof(float) * 16);
		}

		GpuModel* requireGpu(GpuResourceCache& gpu, const Model& model)
		{
			return gpu.model(model.id);
		}

		void applyGBufferSurface(GpuResourceCache& gpu, AssetID matId, MeshGBufferConstants& cb)
		{
			if (AssetRef<Material> mat = gpu.cpuMaterial(matId))
				applyMaterialSurface(*mat, cb);
			else
			{
				cb.color[0]      = 1.0f;
				cb.color[1]      = 1.0f;
				cb.color[2]      = 1.0f;
				cb.color[3]      = 0.0f;
				cb.roughness     = 1.0f;
				cb.metallic      = 0.0f;
				cb.ao            = 1.0f;
				cb.normalScale   = 1.0f;
				cb.alphaCutoff   = 0.5f;
				cb.alphaModeMask = 0.0f;
			}
		}
	} // namespace

	const AnimPose* skinnedPose(const Model& model, const AnimGraphComponent* ag)
	{
		if (ag && ag->graph.player().pose().boneCount > 0)
			return &ag->graph.player().pose();
		if (model.skeleton())
			return &model.skeleton()->restPose;
		return nullptr;
	}

	void drawModelOpaqueGBuffer(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const MeshPipeline& pipeline,
		const Model& model,
		const Matrix4f& world,
		const Matrix4f& viewProj,
		const Matrix4f& prevViewProj,
		DebugFill fill)
	{
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque())
			return;
		pipeline.bind(cmd, fill);
		const bool points = fill == DebugFill::Points;
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (part.skinned || !part.mesh.valid())
				continue;
			const Matrix4f w = part.localToRoot * world;
			gpu.bindMaterial(cmd, part.materialId, MeshPipeline::kRootAlbedoSrv);
			MeshGBufferConstants cb{};
			copyMatrix(cb.worldViewProj, w * viewProj);
			copyMatrix(cb.world, w);
			copyMatrix(cb.prevWorldViewProj, w * prevViewProj);
			applyGBufferSurface(gpu, part.materialId, cb);
			pipeline.setGBufferConstants(cmd, cb);
			part.mesh.draw(cmd, points);
		}
	}

	void drawModelForward(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const MeshPipeline& pipeline,
		const ShadowSystem& shadows,
		const Model& model,
		bool translucentOnly,
		const Matrix4f& world,
		const Matrix4f& viewProj,
		const MeshFrameConstants& lighting,
		DebugFill fill)
	{
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm)
			return;
		const std::vector<GpuModel::Part>& parts = translucentOnly ? gm->translucent() : gm->opaque();
		if (parts.empty())
			return;
		pipeline.bind(cmd, fill);
		shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
		const bool points = fill == DebugFill::Points;
		for (const GpuModel::Part& part : parts)
		{
			if (part.skinned || !part.mesh.valid())
				continue;
			const Matrix4f w = part.localToRoot * world;
			gpu.bindMaterial(cmd, part.materialId, MeshPipeline::kRootAlbedoSrv);
			MeshFrameConstants cb = lighting;
			copyMatrix(cb.worldViewProj, w * viewProj);
			copyMatrix(cb.world, w);
			if (AssetRef<Material> mat = gpu.cpuMaterial(part.materialId))
				applyMaterialSurface(*mat, cb);
			pipeline.setConstants(cmd, cb);
			part.mesh.draw(cmd, points);
		}
	}

	void drawModelDepth(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const ShadowSystem& shadows,
		int cascade,
		const Model& model,
		const Matrix4f& world)
	{
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque())
			return;
		shadows.pipeline().bind(cmd);
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (part.skinned || !part.mesh.valid())
				continue;
			const Matrix4f w   = part.localToRoot * world;
			const Matrix4f wvp = w * shadows.cascade(cascade).viewProj;
			shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
			part.mesh.draw(cmd);
		}
	}

	void drawSkinnedModelOpaqueGBuffer(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const SkinnedMeshPipeline& skinned,
		const MeshPipeline& staticPipeline,
		SkinningUploadRing& ring,
		const Model& model,
		const AnimPose& pose,
		const Matrix4f& world,
		const Matrix4f& prevWorld,
		const Matrix4f& viewProj,
		const Matrix4f& prevViewProj,
		DebugFill fill)
	{
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque())
			return;
		const bool points = fill == DebugFill::Points;
		enum class Bound { None, Skinned, Static } bound = Bound::None;
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (!part.mesh.valid())
				continue;
			if (part.skinned)
			{
				if (!skinned.isValid())
					continue;
				const D3D12_GPU_VIRTUAL_ADDRESS bones = ring.alloc(pose);
				if (bones == 0)
					continue;
				if (bound != Bound::Skinned)
				{
					skinned.bind(cmd, fill);
					bound = Bound::Skinned;
				}
				skinned.setBoneCbv(cmd, bones);
				skinned.setShadowCbv(cmd, ring.dummyGpuVa());
				const Matrix4f w = part.localToRoot * world;
				const Matrix4f pw = part.localToRoot * prevWorld;
				gpu.bindMaterial(cmd, part.materialId, SkinnedMeshPipeline::kRootAlbedoSrv);
				MeshGBufferConstants cb{};
				copyMatrix(cb.worldViewProj, w * viewProj);
				copyMatrix(cb.world, w);
				copyMatrix(cb.prevWorldViewProj, pw * prevViewProj);
				applyGBufferSurface(gpu, part.materialId, cb);
				skinned.setGBufferConstants(cmd, cb);
				part.mesh.draw(cmd, points);
			}
			else
			{
				if (bound != Bound::Static)
				{
					staticPipeline.bind(cmd, fill);
					bound = Bound::Static;
				}
				const Matrix4f w = part.localToRoot * world;
				gpu.bindMaterial(cmd, part.materialId, MeshPipeline::kRootAlbedoSrv);
				MeshGBufferConstants cb{};
				copyMatrix(cb.worldViewProj, w * viewProj);
				copyMatrix(cb.world, w);
				copyMatrix(cb.prevWorldViewProj, w * prevViewProj);
				applyGBufferSurface(gpu, part.materialId, cb);
				staticPipeline.setGBufferConstants(cmd, cb);
				part.mesh.draw(cmd, points);
			}
		}
	}

	void drawSkinnedModelForward(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const SkinnedMeshPipeline& skinned,
		const MeshPipeline& staticPipeline,
		const ShadowSystem& shadows,
		SkinningUploadRing& ring,
		const Model& model,
		bool translucentOnly,
		const AnimPose& pose,
		const Matrix4f& world,
		const Matrix4f& viewProj,
		const MeshFrameConstants& lighting,
		DebugFill fill)
	{
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm)
			return;
		const std::vector<GpuModel::Part>& parts = translucentOnly ? gm->translucent() : gm->opaque();
		if (parts.empty())
			return;
		const bool points = fill == DebugFill::Points;
		enum class Bound { None, Skinned, Static } bound = Bound::None;
		for (const GpuModel::Part& part : parts)
		{
			if (!part.mesh.valid())
				continue;
			if (part.skinned)
			{
				if (!skinned.isValid())
					continue;
				const D3D12_GPU_VIRTUAL_ADDRESS bones = ring.alloc(pose);
				if (bones == 0)
					continue;
				if (bound != Bound::Skinned)
				{
					skinned.bind(cmd, fill);
					bound = Bound::Skinned;
				}
				skinned.setBoneCbv(cmd, bones);
				shadows.bindReceiverCbv(cmd, SkinnedMeshPipeline::kRootShadowCbv);
				const Matrix4f w = part.localToRoot * world;
				gpu.bindMaterial(cmd, part.materialId, SkinnedMeshPipeline::kRootAlbedoSrv);
				MeshFrameConstants cb = lighting;
				copyMatrix(cb.worldViewProj, w * viewProj);
				copyMatrix(cb.world, w);
				if (AssetRef<Material> mat = gpu.cpuMaterial(part.materialId))
					applyMaterialSurface(*mat, cb);
				skinned.setConstants(cmd, cb);
				part.mesh.draw(cmd, points);
			}
			else
			{
				if (bound != Bound::Static)
				{
					staticPipeline.bind(cmd, fill);
					shadows.bindReceiverCbv(cmd, MeshPipeline::kRootShadowCbv);
					bound = Bound::Static;
				}
				const Matrix4f w = part.localToRoot * world;
				gpu.bindMaterial(cmd, part.materialId, MeshPipeline::kRootAlbedoSrv);
				MeshFrameConstants cb = lighting;
				copyMatrix(cb.worldViewProj, w * viewProj);
				copyMatrix(cb.world, w);
				if (AssetRef<Material> mat = gpu.cpuMaterial(part.materialId))
					applyMaterialSurface(*mat, cb);
				staticPipeline.setConstants(cmd, cb);
				part.mesh.draw(cmd, points);
			}
		}
	}

	void drawSkinnedModelDepth(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const ShadowSystem& shadows,
		int cascade,
		const SkinnedMeshPipeline& skinnedShadow,
		SkinningUploadRing& ring,
		const Model& model,
		const AnimPose& pose,
		const Matrix4f& world)
	{
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque())
			return;
		enum class Bound { None, Skinned, Static } bound = Bound::None;
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (!part.mesh.valid())
				continue;
			const Matrix4f w   = part.localToRoot * world;
			const Matrix4f wvp = w * shadows.cascade(cascade).viewProj;
			if (part.skinned)
			{
				if (!skinnedShadow.isValid())
					continue;
				const D3D12_GPU_VIRTUAL_ADDRESS bones = ring.alloc(pose);
				if (bones == 0)
					continue;
				if (bound != Bound::Skinned)
				{
					skinnedShadow.bind(cmd);
					bound = Bound::Skinned;
				}
				skinnedShadow.setBoneCbv(cmd, bones);
				skinnedShadow.setWvp(cmd, wvp.m_afEntry);
				part.mesh.draw(cmd);
			}
			else
			{
				if (bound != Bound::Static)
				{
					shadows.pipeline().bind(cmd);
					bound = Bound::Static;
				}
				shadows.pipeline().setWvp(cmd, wvp.m_afEntry);
				part.mesh.draw(cmd);
			}
		}
	}
} // namespace Dark
