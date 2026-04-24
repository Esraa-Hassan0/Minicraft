#pragma once

#include "../ecs/component.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace our {

    // HandComponent manages first-person hand animation and interaction state
    // Provides idle sway, hit animation, targeting alignment, and collision avoidance
    class HandComponent : public Component {
    public:
        // Base position relative to camera (lower-right viewport)
        glm::vec3 basePosition = glm::vec3(0.38f, -0.5f, -2.0f);
        
        // Base rotation (hand angle relative to camera)
        glm::vec3 baseRotation = glm::vec3(glm::radians(110.0f), glm::radians(-15.0f), glm::radians(-20.0f));

        // Interaction reach range (how far the hand can interact with objects)
        float interactionRange = 2.0f;
        
        // Animation timing
        float idleAnimationTimer = 0.0f;
        float hitAnimationTimer = 0.0f;
        bool isHitting = false;
        
        // Idle animation parameters (subtle breathing/swaying)
        float idleSwayAmplitude = 0.01f;
        float idleSwaySpeedX = 1.5f;
        float idleSwaySpeedY = 2.0f;
        float idleRotationAmplitude = 0.02f;
        
        // Hit animation parameters (punch forward motion)
        float hitAnimationSpeed = 15.0f;
        float hitForwardThrust = 0.15f;
        float hitSideMotion = 0.05f;
        float hitRotationMotion = 0.2f;
        
        // Targeting parameters
        float targetingRotationInfluence = 0.5f;
        bool hasActiveTarget = false;
        glm::vec3 targetPosition = glm::vec3(0.0f);
        
        // Collision avoidance
        float collisionAvoidanceDistance = 0.25f;  // Distance to detect collision ahead
        glm::vec3 collisionAvoidanceOffset = glm::vec3(0.0f);  // Applied offset to avoid collisions
        float collisionSmoothing = 5.0f;  // Smoothing factor for collision response
        
        // Positioning constraints
        float minPositionX = 0.2f;   // Minimum X (left boundary)
        float maxPositionX = 0.5f;   // Maximum X (right boundary)
        float minPositionY = -0.5f;  // Minimum Y (bottom boundary)
        float maxPositionY = -0.2f;  // Maximum Y (top boundary)
        float minPositionZ = -0.8f;  // Minimum Z (back boundary)
        float maxPositionZ = -0.4f;  // Maximum Z (forward boundary)
        
        // Current animated state
        glm::vec3 currentPosition = basePosition;
        glm::vec3 currentRotation = baseRotation;
        
        // Get component type identifier for deserialization
        static std::string getID() { return "Hand"; }
        
        // Deserializes hand properties from JSON configuration
        void deserialize(const nlohmann::json& data) override;
    };
}
