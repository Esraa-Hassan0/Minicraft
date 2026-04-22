#pragma once
#include "../ecs/component.hpp"
#include <string>
#include <glm/glm.hpp>

namespace our
{
    class NPCMovementComponent : public Component
    {
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

        // Set to true by the movement system on the first frame so that
        // startPosition / targetPosition are seeded from the entity's actual
        // world position rather than the (0,0,0) default.
        bool initialized = false;

        enum class MovementType
        {
            IDLE,
            PATROL,
            RANDOM_WALK,
            FOLLOW_PLAYER
        };
        MovementType movementType = MovementType::RANDOM_WALK;

        // Optional patrol end-point offset read from JSON (patrol mode only).
        // The system converts it to an absolute targetPosition on first frame.
        glm::vec3 patrolOffset = glm::vec3(5.0f, 0.0f, 0.0f);

        static std::string getID() { return "NPCMovementComponent"; }

        void deserialize(const nlohmann::json &data) override
        {
            if (!data.is_object())
                return;
            speed = data.value("speed", speed);
            moveRadius = data.value("moveRadius", moveRadius);
            waitTime = data.value("waitTime", waitTime);

            std::string typeStr = data.value("movementType", "random_walk");
            if (typeStr == "idle")
                movementType = MovementType::IDLE;
            else if (typeStr == "patrol")
                movementType = MovementType::PATROL;
            else if (typeStr == "follow_player")
                movementType = MovementType::FOLLOW_PLAYER;
            else
                movementType = MovementType::RANDOM_WALK;

            // Optional patrol offset (e.g. "patrolOffset": [10, 0, 0])
            if (data.contains("patrolOffset") && data["patrolOffset"].is_array())
            {
                patrolOffset = glm::vec3(
                    data["patrolOffset"][0].get<float>(),
                    data["patrolOffset"][1].get<float>(),
                    data["patrolOffset"][2].get<float>());
            }
        }
    };
}