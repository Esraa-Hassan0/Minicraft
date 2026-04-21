#pragma once
#include "../ecs/component.hpp"
#include <string>
#include <glm/glm.hpp>

namespace our {
    class NPCMovementComponent : public Component {
    public:
        float speed = 1.0f;
        float moveRadius = 5.0f;
        float waitTime = 2.0f;
        float waitTimer = 0.0f;
        glm::vec3 startPosition = glm::vec3(0.0f);
        glm::vec3 targetPosition = glm::vec3(0.0f);
        bool isMoving = false;
        int blockedAttempts = 0;
        
        glm::vec3 velocity = glm::vec3(0.0f);
        bool isGrounded = false;
        enum class MovementType {
            IDLE,
            PATROL,
            RANDOM_WALK,
            FOLLOW_PLAYER
        };
        MovementType movementType = MovementType::RANDOM_WALK;

        static std::string getID() { return "NPCMovementComponent"; }

        void deserialize(const nlohmann::json& data) override {
            if (!data.is_object()) return;
            speed = data.value("speed", speed);
            moveRadius = data.value("moveRadius", moveRadius);
            waitTime = data.value("waitTime", waitTime);
            
            std::string typeStr = data.value("movementType", "random_walk");
            if (typeStr == "idle") movementType = MovementType::IDLE;
            else if (typeStr == "patrol") movementType = MovementType::PATROL;
            else if (typeStr == "follow_player") movementType = MovementType::FOLLOW_PLAYER;
            else movementType = MovementType::RANDOM_WALK;
        }
    };
}