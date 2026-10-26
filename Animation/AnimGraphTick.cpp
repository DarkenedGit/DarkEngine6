#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"
#include "Animation/Locomotion.h"
#include "Character/HealthComponent.h"
#include "Combat/HitVolumes.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"

namespace Dark
{
	namespace
	{
		void syncHitVolumes(World& world, Entity e, const Skeleton* skeleton, const AnimPose* pose)
		{
			Combat::HitVolumesComponent* volumes = world.get<Combat::HitVolumesComponent>(e);
			if (!volumes)
				return;
			Combat::rebuildHitVolumes(*volumes, skeleton, pose, world.get<TransformComponent>(e));
		}
	}

	void tickAnimGraphs(World& world, AssetManager& /*assets*/, float dt)
	{
		if (dt <= 0.0f)
			return;

		world.each<AnimGraphComponent>([&](Entity e, AnimGraphComponent& ag) {
			const Skeleton* skel = ag.model ? ag.model->skeleton() : nullptr;
			if (!skel)
			{
				if (!ag.warnedMissing)
				{
					DE_LOG_WARN("tickAnimGraphs: entity missing model skeleton");
					ag.warnedMissing = true;
				}
				syncHitVolumes(world, e, nullptr, nullptr);
				return;
			}
			if (!ag.graphDef && !ag.animSet)
			{
				if (!ag.warnedMissing)
				{
					DE_LOG_WARN("tickAnimGraphs: player-only entity missing AnimationSet");
					ag.warnedMissing = true;
				}
				syncHitVolumes(world, e, skel, nullptr);
				return;
			}

			if (ag.graphDef)
			{
				if (!ag.graph.def())
				{
					if (!ag.graph.bind(ag.graphDef.get(), skel))
					{
						if (!ag.warnedMissing)
						{
							DE_LOG_WARN("tickAnimGraphs: AnimGraph bind failed");
							ag.warnedMissing = true;
						}
						syncHitVolumes(world, e, skel, nullptr);
						return;
					}
				}
				ag.graph.evaluate();
			}

			AnimMarker overlay[64];
			const AnimMarker* overlayPtr = nullptr;
			uint32_t overlayCount = 0;
			if (ag.graphDef)
			{
				overlayCount = ag.graphDef->overlayForClip(ag.graph.player().incomingClip(), overlay, 64);
				overlayPtr = overlay;
			}

			if (HitReactionComponent* hr = world.get<HitReactionComponent>(e))
			{
				hr->hit.decayFlinch(dt);
				Math::Vector3f local = hr->hit.flinchDirection();
				if (const TransformComponent* xf = world.get<TransformComponent>(e))
					local = xf->rotation.Conjugate().Rotate(local);
				const float flinch = hr->hit.flinch();
				ag.graph.player().setHitFlinch(local.z * flinch, local.x * flinch);
			}

			PartImpactWeights impact{};
			if (const Combat::HitVolumesComponent* volumes = world.get<Combat::HitVolumesComponent>(e))
			{
				static_assert(PartImpactWeights::kCount == Combat::HitVolumesComponent::kSlotCount);
				for (int i = 0; i < Combat::HitVolumesComponent::kSlotCount; ++i)
					impact.weight[i] = volumes->impactWeight[i];
			}
			ag.graph.player().setPartImpact(impact);

			AnimNotifyQueue queue;
			ag.graph.player().update(dt, queue, overlayPtr, overlayCount);

			if (ag.graph.player().applyRootMotion())
			{
				if (TransformComponent* xf = world.get<TransformComponent>(e))
				{
					const Math::Vector3f delta = ag.graph.player().rootMotionDelta();
					if (delta.MagnitudeSqrd() > 0.0f)
						xf->position += xf->rotation.Rotate(delta);
				}
			}

			syncHitVolumes(world, e, skel, &ag.graph.player().pose());
		});
	}
}
