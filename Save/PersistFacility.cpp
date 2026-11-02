#include "Gameplay/Inventory.h"
#include "Gameplay/Item.h"
#include "Save/ProgressComponents.h"
#include "Save/SaveBinding.h"
#include "Save/SaveJson.h"
#include "Save/SaveTypes.h"
#include "Scene/Building.h"

#include <string>
#include <vector>

namespace Dark
{
    namespace
    {
        using Save::SaveReader;
        using Save::SaveWriter;

        void captureDoor(const void* component, void* writer)
        {
            const auto& door = *static_cast<const DoorComponent*>(component);
            auto&       out  = *static_cast<SaveWriter*>(writer);
            out.string("lock", door.lock);
            out.f32("open", door.open);
            out.boolean("wantOpen", door.wantOpen);
        }

        void applyDoor(void* component, void* reader, uint16_t)
        {
            auto&       door     = *static_cast<DoorComponent*>(component);
            auto&       in       = *static_cast<SaveReader*>(reader);
            std::string lock     = door.lock;
            float       open     = door.open;
            bool        wantOpen = door.wantOpen;
            if (!in.string("lock", lock, Save::kMaxStringBytes) || !in.f32("open", open, 0.0f, 1.0f) || !in.boolean("wantOpen", wantOpen))
                return;
            door.lock     = std::move(lock);
            door.open     = open;
            door.wantOpen = wantOpen;
        }

        void captureCamera(const void* component, void* writer)
        {
            const auto& cam = *static_cast<const SecurityCameraComponent*>(component);
            auto&       out = *static_cast<SaveWriter*>(writer);
            out.f32("phase", cam.phase);
            out.f32("seen", cam.seen);
            out.f32("aimYaw", cam.aimYaw);
        }

        void applyCamera(void* component, void* reader, uint16_t)
        {
            auto& cam    = *static_cast<SecurityCameraComponent*>(component);
            auto& in     = *static_cast<SaveReader*>(reader);
            float phase  = cam.phase;
            float seen   = cam.seen;
            float aimYaw = cam.aimYaw;
            if (!in.f32("phase", phase, -1.0e6f, 1.0e6f) || !in.f32("seen", seen, 0.0f, 10.0f) || !in.f32("aimYaw", aimYaw, -1.0e5f, 1.0e5f))
                return;
            cam.phase  = phase;
            cam.seen   = seen;
            cam.aimYaw = aimYaw;
        }

        struct LootRow
        {
            std::string item;
            int         count = 0;
        };

        void writeLootRow(SaveWriter& item, int index, void* user)
        {
            const auto& rows = *static_cast<const std::vector<LootRow>*>(user);
            item.string("item", rows[static_cast<size_t>(index)].item);
            item.i32("count", rows[static_cast<size_t>(index)].count);
        }

        void readLootRow(SaveReader& item, int, void* user)
        {
            auto&   rows = *static_cast<std::vector<LootRow>*>(user);
            LootRow row;
            if (!item.string("item", row.item, 64) || !item.i32("count", row.count, 1, 10000))
                return;
            rows.push_back(std::move(row));
        }

        void captureLoot(const void* component, void* writer)
        {
            const auto&          loot = *static_cast<const LootComponent*>(component);
            auto&                out  = *static_cast<SaveWriter*>(writer);
            std::vector<LootRow> rows;
            for (const ItemStack& s : loot.contents.bag)
            {
                if (!s.empty())
                    rows.push_back(LootRow{ s.def->id, s.count });
            }
            out.arrayObjects("items", static_cast<int>(rows.size()), &writeLootRow, &rows);
        }

        void applyLoot(void* component, void* reader, uint16_t)
        {
            auto&                loot  = *static_cast<LootComponent*>(component);
            auto&                in    = *static_cast<SaveReader*>(reader);
            std::vector<LootRow> rows;
            int                  count = 0;
            if (!in.arrayObjects("items", InventoryComponent::kBagSize, count, &readLootRow, &rows) || in.failed() || count < 0)
                return;
            InventoryComponent contents;
            for (const LootRow& row : rows)
            {
                if (const ItemDef* def = findItemDef(row.item))
                    addItem(contents, *def, row.count);
            }
            loot.contents = contents;
        }

        bool includeFacility(const void* component)
        {
            const auto& f = *static_cast<const FacilityProgressComponent*>(component);
            return !f.upgrades.empty() || f.alarmTime > 0.0f || f.hiddenIn.valid();
        }

        void writeUpgrade(SaveWriter& item, int index, void* user)
        {
            const auto& ids = *static_cast<const std::vector<std::string>*>(user);
            item.string("id", ids[static_cast<size_t>(index)]);
        }

        void readUpgrade(SaveReader& item, int, void* user)
        {
            auto&       ids = *static_cast<std::vector<std::string>*>(user);
            std::string id;
            if (!item.string("id", id, 64) || id.empty())
                return;
            ids.push_back(std::move(id));
        }

        void captureFacility(const void* component, void* writer)
        {
            const auto& f   = *static_cast<const FacilityProgressComponent*>(component);
            auto&       out = *static_cast<SaveWriter*>(writer);
            out.arrayObjects("upgrades", static_cast<int>(f.upgrades.size()), &writeUpgrade, const_cast<std::vector<std::string>*>(&f.upgrades));
            out.f32("alarmTime", f.alarmTime);
            out.entity("hiddenIn", f.hiddenIn);
            out.f32("hideFound", f.hideFound);
        }

        void applyFacility(void* component, void* reader, uint16_t)
        {
            auto&                    f   = *static_cast<FacilityProgressComponent*>(component);
            auto&                    in  = *static_cast<SaveReader*>(reader);
            std::vector<std::string> ids;
            int                      count     = 0;
            float                    alarmTime = f.alarmTime;
            float                    hideFound = f.hideFound;
            if (!in.arrayObjects("upgrades", 64, count, &readUpgrade, &ids) || in.failed() || count < 0)
                return;
            if (!in.f32("alarmTime", alarmTime, 0.0f, Save::kMaxTimerSeconds) || !in.f32("hideFound", hideFound, 0.0f, Save::kMaxTimerSeconds))
                return;
            f.upgrades  = std::move(ids);
            f.alarmTime = alarmTime;
            f.hideFound = hideFound;
        }

        void bindFacility(void* component, void* reader)
        {
            auto&  f  = *static_cast<FacilityProgressComponent*>(component);
            auto&  in = *static_cast<SaveReader*>(reader);
            Entity hiddenIn{};
            if (in.entityRef("hiddenIn", hiddenIn))
                f.hiddenIn = hiddenIn;
        }
    } // namespace

    const PersistFns DoorComponent::kPersist{ "Door", DoorComponent::kSaveVersion, 100, nullptr, &captureDoor, &applyDoor, nullptr };
    const PersistFns SecurityCameraComponent::kPersist{ "SecurityCamera", SecurityCameraComponent::kSaveVersion, 100, nullptr, &captureCamera, &applyCamera, nullptr };
    const PersistFns LootComponent::kPersist{ "Loot", LootComponent::kSaveVersion, 100, nullptr, &captureLoot, &applyLoot, nullptr };
    const PersistFns FacilityProgressComponent::kPersist{ "FacilityProgress", FacilityProgressComponent::kSaveVersion, 100, &includeFacility, &captureFacility, &applyFacility,
                                                          &bindFacility };

    namespace
    {
        struct PersistReg
        {
            PersistReg()
            {
                Save::bindPersist<DoorComponent>();
                Save::bindPersist<SecurityCameraComponent>();
                Save::bindPersist<LootComponent>();
                Save::bindPersist<FacilityProgressComponent>();
            }
        };
        const PersistReg g_persistFacility;
    } // namespace

} // namespace Dark
