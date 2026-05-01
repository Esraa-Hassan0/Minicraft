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
        float interactionRange = 100.0f;  // Can hit blocks/animals from far distance
        
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
        float collisionAvoidanceDistance = 1.2f;  // Increased distance to detect collisions earlier (allows hand to move before hitting)
        glm::vec3 collisionAvoidanceOffset = glm::vec3(0.0f);  // Applied offset to avoid collisions
        float collisionSmoothing = 12.0f;  // Faster response to collisions (higher = faster)
        bool hasCollisionAhead = false;  // Track if collision is currently active
        
        // Positioning constraints - expanded to allow more lateral movement for collision avoidance
        float minPositionX = 0.1f;   // Minimum X (left boundary) - expanded
        float maxPositionX = 0.6f;   // Maximum X (right boundary) - expanded
        float minPositionY = -0.7f;  // Minimum Y (bottom boundary) - expanded
        float maxPositionY = -0.1f;  // Maximum Y (top boundary) - expanded
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
