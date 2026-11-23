#pragma once

#include "ECS/Entity.h"

namespace Dark
{

    class World;

    // The life state of the entity an Entity or Bone decal sticks to. The pool frees the decal when
    // the entity comes back to life (a revive), so a fresh body does not keep the old wounds.
    class IDecalLifeSource
    {
    public:
        virtual ~IDecalLifeSource() = default;

        // False when the entity has no life state: the decal treats it as alive and never frees on revive.
        virtual bool life(World& world, Entity entity, bool& alive) const = 0;
    };

} // namespace Dark
