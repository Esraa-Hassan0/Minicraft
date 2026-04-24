#pragma once

#include <ecs/world.hpp>
#include <components/light.hpp>

#include <glm/vec3.hpp>

namespace our
{
    class TimeSystem
    {
    private:
        float currentTime;
        float dayDuration;
        int daysPassed = 0;

        float sunriseStart;
        float sunriseEnd;
        float sunsetStart;
        float sunsetEnd;

        float orbitRadius;
        float orbitHeight;

        Entity *sunEntity;
        Entity *sunLightEntity;
        Entity *dayLightEntity;
        Entity *nightLightEntity;

        LightComponent *sunLight;
        LightComponent *dayLight;
        LightComponent *nightLight;

    public:
        void initialize(World *world, float dayDuration = 20.0f);

        void update(World *world, float deltaTime);

        void updateSunScreenPosition(const glm::mat4& viewProjection);

        float getTimeOfDay() const { return currentTime; }

        int getDaysPassed() const { return daysPassed; }

        float getSunElevation() const;

        glm::vec3 getSunScreenPosition() const { return sunScreenPosition; }
        glm::vec3 getSunColor() const { return sunColor; }
        float getSunIntensity() const { return sunIntensity; }

        glm::vec3 getSkyTint() const { return skyTint; }

    private:
        glm::vec3 sunScreenPosition;
        glm::vec3 sunColor;
        float sunIntensity;
        glm::vec3 skyTint;

        void updateSunPosition(float time);
        void updateLighting(float elevation);
    };
}