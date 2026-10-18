#include "Play/PlayerFlashlight.h"

#include "Character/ShieldView.h"
#include "Character/SkillCatalog.h"
#include "Character/SkillComponent.h"
#include "Character/SkillLimits.h"
#include "Character/SkillSense.h"
#include "ECS/Components.h"
#include "ECS/World.h"
#include "Gameplay/Inventory.h"

namespace Dark
{

    bool flashlightEquipped(const InventoryComponent* inv)
    {
        if (!inv)
            return false;
        const ItemStack& eq = inv->equipped[static_cast<int>(EquipSlot::Light)];
        return !eq.empty() && eq.def && std::string_view(eq.def->id) == "flashlight";
    }

    void ensurePlayerFlashlight(World& world, Entity player)
    {
        if (!player.valid() || !world.alive(player))
            return;
        const ItemDef* def = findItemDef("flashlight");
        if (!def)
            return;
        if (!world.has<InventoryComponent>(player))
            world.emplace<InventoryComponent>(player);
        InventoryComponent* inv = world.get<InventoryComponent>(player);
        if (!inv || hasItem(*inv, def->id))
            return;
        if (addItem(*inv, *def, 1) <= 0)
            return;
        for (int i = 0; i < InventoryComponent::kBagSize; ++i)
        {
            if (!inv->bag[i].empty() && inv->bag[i].def == def)
            {
                equipItem(*inv, i);
                break;
            }
        }
    }

    namespace
    {
        void releaseLight(World& world, PlayerFlashlightComponent& rig)
        {
            destroyPlayerFlashlight(world, rig.light);
            rig.baseRange = 0.0f;
            rig.baseOuter = 0.0f;
            rig.baseId    = 0;
        }
    }

    void syncPlayerFlashlight(World& world, Entity player, const Math::Vector3f& fallbackPos, const Math::Vector3f& look, const Math::Vector3f& right, const Math::Vector3f& up, bool lightOn)
    {
        if (!player.valid() || !world.alive(player))
            return;

        const bool equipped = flashlightEquipped(world.get<InventoryComponent>(player));
        PlayerFlashlightComponent* rig = world.get<PlayerFlashlightComponent>(player);
        if (!equipped)
        {
            if (rig)
                releaseLight(world, *rig);
            return;
        }

        if (!rig)
            rig = &world.emplace<PlayerFlashlightComponent>(player);
        if (!rig->light.valid() || !world.alive(rig->light))
        {
            rig->light  = spawnPlayerFlashlight(world);
            rig->baseId = 0;
        }

        TransformComponent*  xf    = world.get<TransformComponent>(rig->light);
        LocalLightComponent* light = world.get<LocalLightComponent>(rig->light);
        if (!xf || !light)
            return;

        const TransformComponent* bodyXf  = world.get<TransformComponent>(player);
        const Math::Vector3f      bodyPos = bodyXf ? bodyXf->position : fallbackPos;
        placePlayerFlashlight(*xf, bodyPos, look, right, up);
        light->enabled = lightOn;
        // Capture the authored cone once per light entity, then reapply the See scale from that base.
        if (rig->baseId != rig->light.id())
        {
            rig->baseRange = light->range;
            rig->baseOuter = light->outerConeDeg;
            rig->baseId    = rig->light.id();
        }

        float seeRange = kSkillIdentity;
        float seeCone  = kSkillIdentity;
        if (const SkillComponent* sk = world.get<SkillComponent>(player))
        {
            const int seeLevel = sk->level(SkillId::See);
            seeRange = skillScalar(SkillId::See, SkillScalar::SeeRangeScale, seeLevel);
            seeCone  = skillScalar(SkillId::See, SkillScalar::SeeConeScale, seeLevel);
        }
        scaleFlashlight(rig->baseRange, rig->baseOuter, seeRange, seeCone, light->range, light->outerConeDeg);
    }

    void clearPlayerFlashlight(World& world, Entity player)
    {
        if (!player.valid() || !world.alive(player))
            return;
        if (PlayerFlashlightComponent* rig = world.get<PlayerFlashlightComponent>(player))
            releaseLight(world, *rig);
    }

} // namespace Dark
