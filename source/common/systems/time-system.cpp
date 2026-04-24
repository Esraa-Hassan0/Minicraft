#include "time-system.hpp"
#include <ecs/entity.hpp>
#include <components/mesh-renderer.hpp>

namespace our
{
    void TimeSystem::initialize(World *world, float dayDuration)
    {
        this->dayDuration = dayDuration;
        this->currentTime = 0.25f;
        this->totalDaysPassed = 0;

        this->sunriseStart = 0.2f;
        this->sunriseEnd = 0.3f;
        this->sunsetStart = 0.7f;
        this->sunsetEnd = 0.8f;

        this->orbitRadius = 50.0f;
        this->orbitHeight = 10.0f;

        sunEntity = nullptr;
        sunLightEntity = nullptr;
        dayLightEntity = nullptr;
        nightLightEntity = nullptr;
        sunLight = nullptr;
        dayLight = nullptr;
        nightLight = nullptr;

        for (auto entity : world->getEntities())
        {
            if (!entity) continue;
            if (entity->name == "sun")
            {
                sunEntity = entity;
                sunLight = entity->getComponent<LightComponent>();
            }
            else if (entity->name == "daylight")
            {
                dayLightEntity = entity;
                dayLight = entity->getComponent<LightComponent>();
            }
            else if (entity->name == "nightlight")
            {
                nightLightEntity = entity;
                nightLight = entity->getComponent<LightComponent>();
            }
        }
    }

    void TimeSystem::update(World *world, float deltaTime)
    {
        currentTime += deltaTime / dayDuration;
        if (currentTime >= 1.0f) {
            currentTime -= 1.0f;
            totalDaysPassed++;
        }

        float elevation = getSunElevation();
        updateSunPosition(currentTime);
        updateLighting(elevation);
    }

    float TimeSystem::getSunElevation() const
    {
        float t = currentTime;
        if (t < 0.25f)
            t += 0.75f;
        else
            t -= 0.25f;
        return glm::sin(t * glm::pi<float>() * 2.0f);
    }

    void TimeSystem::updateSunPosition(float time)
    {
        if (!sunEntity)
            return;

        float angle = time * glm::pi<float>() * 2.0f - glm::pi<float>() * 0.5f;
        float x = glm::cos(angle) * orbitRadius;
        float y = glm::sin(angle) * orbitRadius + orbitHeight;
        float z = -50.0f;

        sunEntity->localTransform.position = glm::vec3(x, y, z);

        float elevation = getSunElevation();
        float scale = 2.0f;
        if (elevation < 0.3f)
        {
            scale = 2.0f * (elevation / 0.3f);
            if (scale < 0.5f) scale = 0.5f;
        }
        sunEntity->localTransform.scale = glm::vec3(scale, scale, scale);
    }

    void TimeSystem::updateLighting(float elevation)
    {
        bool isDay = elevation > 0.0f;
        bool isSunrise = elevation > -0.2f && elevation < 0.2f;
        bool isSunset = false;

        sunColor = glm::vec3(1.0f, 1.0f, 0.9f);
        sunIntensity = 1.0f;
        skyTint = glm::vec3(0.3f, 0.6f, 1.0f);

        if (elevation < -0.3f)
        {
            sunColor = glm::vec3(0.1f, 0.1f, 0.3f);
            sunIntensity = 0.1f;
            skyTint = glm::vec3(0.05f, 0.05f, 0.15f);
        }
        else if (elevation < 0.0f)
        {
            float t = (elevation + 0.3f) / 0.3f;
            sunColor = glm::mix(glm::vec3(0.1f, 0.1f, 0.3f), glm::vec3(1.0f, 0.6f, 0.3f), t);
            sunIntensity = glm::mix(0.1f, 0.8f, t);
            skyTint = glm::mix(glm::vec3(0.05f, 0.05f, 0.15f), glm::vec3(0.4f, 0.3f, 0.2f), t);
        }
        else if (elevation < 0.3f)
        {
            float t = elevation / 0.3f;
            sunColor = glm::mix(glm::vec3(1.0f, 0.6f, 0.3f), glm::vec3(1.0f, 1.0f, 0.9f), t);
            sunIntensity = glm::mix(0.8f, 1.0f, t);
            skyTint = glm::mix(glm::vec3(0.4f, 0.3f, 0.2f), glm::vec3(0.3f, 0.6f, 1.0f), t);
        }

        if (sunLight)
        {
            sunLight->color = sunColor;
            // Ensure a baseline ambient light even at night
            float minAmbient = 0.6f; // Increased significantly for better night visibility
            sunLight->ambient = glm::max(sunColor * sunIntensity * 0.5f, glm::vec3(minAmbient));
            sunLight->enabled = sunIntensity > 0.2f;
        }

        if (dayLight)
        {
            dayLight->enabled = isDay;
            if (isDay && elevation < 0.5f)
            {
                dayLight->color = glm::vec3(0.95f, 0.98f, 1.0f) * (0.5f + elevation);
                dayLight->ambient = glm::vec3(1.0f, 0.9f, 0.8f) * (0.5f + elevation);
            }
        }

        if (nightLight)
        {
            nightLight->enabled = !isDay;
            if (!isDay) {
                // nightLight properties are mainly from config, but we can ensure they are active
            }
        }
    }
}