#pragma once

#include "../ecs/world.hpp"
#include "../components/light.hpp"

namespace our
{

    class LightSystem
    {
    public:
        void update(World *world, float deltaTime)
        {
            for (auto entity : world->getEntities())
            {
                if (!entity) continue;
                LightComponent *light = entity->getComponent<LightComponent>();
                if (!light || !light->enabled)
                    continue;
                if (light->flashTimer > 0.0f)
                {
                    light->flashTimer -= deltaTime;
                    if (light->flashTimer < 0.0f)
                        light->flashTimer = 0.0f;
                }
            }
        }
    };

}