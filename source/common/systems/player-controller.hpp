#pragma once

#include "../ecs/world.hpp"
#include "../components/player.hpp"
#include "../components/aabb-collider.hpp"
#include "../components/camera.hpp"
#include "../application.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <GLFW/glfw3.h>

namespace our {

    // PlayerControllerSystem handles player input and movement
    // includes WASD movement, jumping, and camera control
    class PlayerControllerSystem {
    private:
        Application* app;
        bool isFirstUpdate = true;

    public:
        // Initialize the system with the application instance
        void enter(Application* app) {
            this->app = app;
            isFirstUpdate = true;
        }

        // Update player based on input each frame
        void update(World* world, float deltaTime) {
            if (!app) return;

            // Find the player entity
            PlayerComponent* player = nullptr;
            Entity* playerEntity = nullptr;
            CameraComponent* camera = nullptr;

            for (auto entity : world->getEntities()) {
                PlayerComponent* p = entity->getComponent<PlayerComponent>();
                if (p) {
                    player = p;
                    playerEntity = entity;
                    camera = entity->getComponent<CameraComponent>();
                    break;
                }
            }

            if (!player || !playerEntity) return;

            // Handle mouse input for camera rotation
            handleCameraInput(playerEntity, camera, deltaTime);

            // Handle WASD movement input
            handleMovementInput(playerEntity, player, deltaTime);

            // Handle jump input
            handleJumpInput(player);

            // Update block interaction timers
            if (player->timeSinceLastPlacement > 0) {
                player->timeSinceLastPlacement -= static_cast<int>(deltaTime * 1000);
            }
        }

        void exit() {
            // Cleanup
        }

    private:
        // Handle camera rotation based on mouse movement
        void handleCameraInput(Entity* entity, CameraComponent* camera, float deltaTime) {
            if (!camera) return;

            // Get mouse delta
            glm::vec2 mouseDelta = app->getMouse().getMouseDelta();

            // Mouse sensitivity
            float rotationSensitivity = 0.005f;

            // Update camera rotation
            glm::vec3& rotation = entity->localTransform.rotation;
            rotation.x += -mouseDelta.y * rotationSensitivity; // Pitch (up/down)
            rotation.y += -mouseDelta.x * rotationSensitivity; // Yaw (left/right)

            // Clamp pitch to prevent flipping
            const float maxPitch = glm::half_pi<float>() * 0.99f;
            if (rotation.x < -maxPitch) rotation.x = -maxPitch;
            if (rotation.x > maxPitch) rotation.x = maxPitch;

            // Wrap yaw around 0-2π
            rotation.y = glm::wrapAngle(rotation.y);

            // Lock mouse for first-person view
            if (!isFirstUpdate && app->getMouse().isPressed(GLFW_MOUSE_BUTTON_1)) {
                app->getMouse().lockMouse(app->getWindow());
            } else if (!isFirstUpdate && app->getMouse().isPressed(GLFW_MOUSE_BUTTON_2)) {
                app->getMouse().unlockMouse(app->getWindow());
            }

            isFirstUpdate = false;
        }

        // Handle WASD movement input
        void handleMovementInput(Entity* entity, PlayerComponent* player, float deltaTime) {
            Keyboard& keyboard = app->getKeyboard();
            
            // Get camera forward, right, and up directions
            glm::mat4 cameraMatrix = entity->localTransform.toMat4();
            glm::vec3 forward = glm::vec3(cameraMatrix * glm::vec4(0, 0, -1, 0));
            glm::vec3 right = glm::vec3(cameraMatrix * glm::vec4(1, 0, 0, 0));

            // Flatten the directions so we don't move slower or not at all when looking up/down
            forward.y = 0;
            right.y = 0;
            if (glm::length(forward) > 0.0f) forward = glm::normalize(forward);
            if (glm::length(right) > 0.0f) right = glm::normalize(right);

            // Calculate movement direction based on WASD input
            glm::vec3 moveDirection = glm::vec3(0.0f);

            if (keyboard.isPressed(GLFW_KEY_W)) {
                moveDirection += forward;
            }
            if (keyboard.isPressed(GLFW_KEY_S)) {
                moveDirection -= forward;
            }
            if (keyboard.isPressed(GLFW_KEY_D)) {
                moveDirection += right;
            }
            if (keyboard.isPressed(GLFW_KEY_A)) {
                moveDirection -= right;
            }

            // Normalize movement direction to prevent faster diagonal movement
            if (glm::length(moveDirection) > 0.0f) {
                moveDirection = glm::normalize(moveDirection);
            }

            glm::vec3 currentMovement = moveDirection * player->speed;

            // Update horizontal velocity
            player->velocity.x = currentMovement.x;
            player->velocity.z = currentMovement.z;

            // Handle sprinting with shift key
            if (keyboard.isPressed(GLFW_KEY_LEFT_SHIFT)) {
                player->velocity.x *= 1.5f;
                player->velocity.z *= 1.5f;
            }
        }

        // Handle jump input
        void handleJumpInput(PlayerComponent* player) {
            Keyboard& keyboard = app->getKeyboard();

            // Jump when space is pressed and player is on the ground
            if (keyboard.justPressed(GLFW_KEY_SPACE) && player->isGrounded) {
                player->velocity.y = player->jumpForce;
                player->isGrounded = false;
            }
        }
    };

}
