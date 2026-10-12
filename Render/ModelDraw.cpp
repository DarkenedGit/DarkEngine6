#include "Render/ModelDraw.h"
#include "Animation/AnimGraphComponent.h"
#include "Assets/Material.h"
#include "Assets/Model.h"
#include "Render/GpuModel.h"
#include "Render/GpuResourceCache.h"
#include "Render/MaterialSurface.h"
#include "Render/ShadowSystem.h"
#include "Render/SkinnedInstanceBuffer.h"

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

	bool submitSkinnedModelOpaque(
		SkinnedInstanceBuffer& buffer,
		GpuResourceCache& gpu,
		const GpuModel& model,
		const AnimPose* pose,
		const Matrix4f& entityWorld,
		const Matrix4f& prevEntityWorld,
		StaticPopulation population,
		bool castShadow)
	{
		if (!pose)
			return false;
		const uint32_t saved = buffer.mark();
		bool any = false;
		for (const GpuModel::Part& part : model.opaque())
		{
			// sortKey 0 is an empty mesh. record skips a mesh that is not valid().
			if (!part.skinned || part.translucent || part.mesh.sortKey() == 0)
				continue;
			any = true;
			SkinnedPartSubmit sub;
			sub.mesh        = &part.mesh;
			sub.material    = part.materialId;
			sub.world       = part.localToRoot * entityWorld;
			sub.prevWorld   = part.localToRoot * prevEntityWorld;
			sub.population  = population;
			sub.castShadow  = castShadow;
			sub.pose        = pose;
			sub.color[0]    = 1.0f;
			sub.color[1]    = 1.0f;
			sub.color[2]    = 1.0f;
			sub.color[3]    = 0.0f;
			sub.alphaMask   = false;
			if (const AssetRef<Material> mat = gpu.cpuMaterial(part.materialId))
			{
				MeshGBufferConstants surface{};
				applyMaterialSurface(*mat, surface);
				sub.color[0]  = surface.color[0];
				sub.color[1]  = surface.color[1];
				sub.color[2]  = surface.color[2];
				sub.color[3]  = surface.color[3];
				sub.alphaMask = surface.alphaModeMask > 0.5f;
			}
			if (!buffer.submit(sub))
			{
				buffer.rollback(saved);
				return false;
			}
		}
		if (!any)
			return false;
		return true;
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
		const ShadowPipeline& pipeline,
		const Matrix4f& lightViewProj,
		const Model& model,
		const Matrix4f& world)
	{
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque())
			return;
		pipeline.bind(cmd);
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (part.skinned || !part.mesh.valid())
				continue;
			const Matrix4f w   = part.localToRoot * world;
			const Matrix4f wvp = w * lightViewProj;
			pipeline.setWvp(cmd, wvp.m_afEntry);
			part.mesh.draw(cmd);
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
		drawModelDepth(cmd, gpu, shadows.pipeline(), shadows.cascade(cascade).viewProj, model, world);
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
		const ShadowPipeline& staticDepth,
		const Matrix4f& lightViewProj,
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
			const Matrix4f wvp = w * lightViewProj;
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
					staticDepth.bind(cmd);
					bound = Bound::Static;
				}
				staticDepth.setWvp(cmd, wvp.m_afEntry);
				part.mesh.draw(cmd);
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
		drawSkinnedModelDepth(cmd, gpu, shadows.pipeline(), shadows.cascade(cascade).viewProj, skinnedShadow, ring, model, pose, world);
	}

	void drawSkinnedModelOpaqueGBufferSkinnedOnly(
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
		(void)staticPipeline;
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque() || !skinned.isValid())
			return;
		const bool points = fill == DebugFill::Points;
		bool bound = false;
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (!part.skinned || !part.mesh.valid())
				continue;
			const D3D12_GPU_VIRTUAL_ADDRESS bones = ring.alloc(pose);
			if (bones == 0)
				continue;
			if (!bound)
			{
				skinned.bind(cmd, fill);
				bound = true;
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
	}

	void drawSkinnedModelDepthSkinnedOnly(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const ShadowPipeline& staticDepth,
		const Matrix4f& lightViewProj,
		const SkinnedMeshPipeline& skinnedShadow,
		SkinningUploadRing& ring,
		const Model& model,
		const AnimPose& pose,
		const Matrix4f& world)
	{
		(void)staticDepth;
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque() || !skinnedShadow.isValid())
			return;
		bool bound = false;
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (!part.skinned || !part.mesh.valid())
				continue;
			const D3D12_GPU_VIRTUAL_ADDRESS bones = ring.alloc(pose);
			if (bones == 0)
				continue;
			if (!bound)
			{
				skinnedShadow.bind(cmd);
				bound = true;
			}
			const Matrix4f w = part.localToRoot * world;
			skinnedShadow.setBoneCbv(cmd, bones);
			skinnedShadow.setWvp(cmd, (w * lightViewProj).m_afEntry);
			part.mesh.draw(cmd);
		}
	}

	void drawSkinnedModelForwardSkinnedOnly(
		ID3D12GraphicsCommandList* cmd,
		GpuResourceCache& gpu,
		const SkinnedMeshPipeline& skinned,
		const MeshPipeline& staticPipeline,
		const ShadowSystem& shadows,
		SkinningUploadRing& ring,
		const Model& model,
		const AnimPose& pose,
		const Matrix4f& world,
		const Matrix4f& viewProj,
		const MeshFrameConstants& lighting,
		DebugFill fill)
	{
		(void)staticPipeline;
		GpuModel* gm = requireGpu(gpu, model);
		if (!cmd || !gm || !gm->hasOpaque() || !skinned.isValid())
			return;
		const bool points = fill == DebugFill::Points;
		bool bound = false;
		for (const GpuModel::Part& part : gm->opaque())
		{
			if (!part.skinned || !part.mesh.valid())
				continue;
			const D3D12_GPU_VIRTUAL_ADDRESS bones = ring.alloc(pose);
			if (bones == 0)
				continue;
			if (!bound)
			{
				skinned.bind(cmd, fill);
				bound = true;
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
	}
} // namespace Dark
