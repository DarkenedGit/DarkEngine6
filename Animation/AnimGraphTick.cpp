#include "Animation/AnimGraphTick.h"
#include "Animation/AnimGraphComponent.h"
#include "Core/Log.h"
#include "ECS/Components.h"
#include "ECS/World.h"

namespace Dark
{
	void tickAnimGraphs(World& world, AssetManager& /*assets*/, float dt)
	{
		if (dt <= 0.0f)
			return;

		world.each<AnimGraphComponent>([&](Entity e, AnimGraphComponent& ag) {
			if (!ag.model || !ag.model->skeleton())
			{
				if (!ag.warnedMissing)
				{
					DE_LOG_WARN("tickAnimGraphs: entity missing model skeleton");
					ag.warnedMissing = true;
				}
				return;
			}
			if (!ag.graphDef && !ag.animSet)
			{
				if (!ag.warnedMissing)
				{
					DE_LOG_WARN("tickAnimGraphs: player-only entity missing AnimationSet");
					ag.warnedMissing = true;
				}
				return;
			}

			const Skeleton* skel = ag.model->skeleton();
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
		});
	}
}
