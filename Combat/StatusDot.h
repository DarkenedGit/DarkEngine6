#pragma once

namespace Dark
{
    class World;
}

namespace Dark::Combat
{

    class CombatSystem;

    int harvestAndResolveDots(World& world, CombatSystem& combat);

} // namespace Dark::Combat
