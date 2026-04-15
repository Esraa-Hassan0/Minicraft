#pragma once

// LightComponent enables entities to emit or affect light in the scene.
// Supports three light types: directional (sun-like), point (omni-directional), and spot (cone-shaped).
// Used with LitMaterial in the forward renderer for Blinn-Phong lighting.

#include "../ecs/component.hpp"

#include <glm/vec3.hpp>

namespace our {

    // Defines the type of light source.
    // DIRECTIONAL: Infinite light source with parallel rays (e.g., sun)
    // POINT: Light radiating from a point in all directions
    // SPOT: Cone-shaped light with inner/outer angle control
    enum class LightType { DIRECTIONAL, POINT, SPOT };

    // ECS component representing a light source in the world.
    // Attach to an entity to make it emit light that affects lit objects.
    class LightComponent : public Component {
    public:
        // Type of light: directional, point, or spot
        LightType  type      = LightType::POINT;

        // Diffuse and specular color of the light
        glm::vec3  color     = {1.0f, 1.0f, 1.0f};
        // Ambient contribution (minimum light level even when unlit)
        glm::vec3  ambient   = {0.1f, 0.1f, 0.1f};

        // Attenuation coefficients for point/spot lights
        // Controls how light intensity decreases with distance.
        // Formula: 1 / (constant + linear * dist + quadratic * dist^2)
        float attenConstant  = 1.0f;
        float attenLinear    = 0.09f;
        float attenQuadratic = 0.032f;

        // Spot light angles (in degrees) - defines the cone shape
        // Inner cutoff: fully bright core
        // Outer cutoff: edge where light fades to zero
        float innerCutoffDeg = 12.5f;
        float outerCutoffDeg = 17.5f;

        // Visual feedback for damage/flash effects
        glm::vec3 flashColor  = {1.0f, 0.0f, 0.0f};  // Color when flashing (red)
        float     flashTimer  = 0.0f;  // Countdown timer for flash duration

        // Returns component type identifier for deserialization
        static std::string getID() { return "Light"; }
        // Deserializes light properties from JSON configuration
        void deserialize(const nlohmann::json& data) override;
    };
}