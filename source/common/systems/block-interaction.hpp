#pragma once

#include "../ecs/world.hpp"
#include "../components/player.hpp"
#include "../components/mesh-renderer.hpp"
#include "../components/aabb-collider.hpp"
#include "../application.hpp"

#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <optional>

namespace our {

    // Information about a block that was hit by a ray cast
    struct BlockHitInfo {
        Entity* entity;         // The entity that was hit
        glm::vec3 hitPoint;     // The world position where the ray hit
        glm::vec3 hitNormal;    // The normal of the face that was hit
        float distance;         // Distance from ray origin to hit point
    };

    // BlockInteractionSystem handles mining and placing blocks
    class BlockInteractionSystem {
    private:
        Application* app;

    public:
        void enter(Application* app) {
            this->app = app;
        }

        void update(World* world, float deltaTime) {
            if (!app) return;

            // Find the player
            PlayerComponent* player = nullptr;
            Entity* playerEntity = nullptr;

            for (auto entity : world->getEntities()) {
                PlayerComponent* p = entity->getComponent<PlayerComponent>();
                if (p) {
                    player = p;
                    playerEntity = entity;
                    break;
                }
            }

            if (!player || !playerEntity) return;

            // Get camera direction
            glm::mat4 cameraMatrix = playerEntity->localTransform.toMat4();
            glm::vec3 cameraPos = glm::vec3(playerEntity->localTransform.position);
            glm::vec3 cameraForward = glm::normalize(glm::vec3(cameraMatrix * glm::vec4(0, 0, -1, 0)));

            // Cast a ray from player to see what's targeted
            std::optional<BlockHitInfo> hitInfo = raycastBlocks(world, cameraPos, cameraForward, 
                                                                player->interactionRange, playerEntity);

            // Handle left click (mining)
            if (app->getKeyboard().isPressed(GLFW_MOUSE_BUTTON_1) && hitInfo) {
                mineBlock(hitInfo->entity, player, world);
            }

            // Handle right click (placing)
            if (app->getKeyboard().justPressed(GLFW_MOUSE_BUTTON_2) && player->timeSinceLastPlacement <= 0) {
                if (hitInfo) {
                    placeBlock(hitInfo.value(), world, player);
                    player->timeSinceLastPlacement = player->blockPlacementCooldown;
                }
            }
        }

        void exit() {
            // Cleanup
        }

    private:
        // Ray cast to find blocks
        std::optional<BlockHitInfo> raycastBlocks(World* world, const glm::vec3& rayOrigin, 
                                                  const glm::vec3& rayDirection, float maxDistance, 
                                                  Entity* playerEntity) {
            std::optional<BlockHitInfo> closestHit;
            float closestDistance = maxDistance;

            for (auto entity : world->getEntities()) {
                if (entity == playerEntity) continue; // Skip the player

                AABBColliderComponent* collider = entity->getComponent<AABBColliderComponent>();
                if (!collider || collider->isTrigger) continue; // Only check physical colliders

                // Check if ray hits this entity's AABB
                std::optional<BlockHitInfo> hit = rayAABBIntersection(
                    rayOrigin, rayDirection, 
                    entity->localTransform.position, *collider
                );

                if (hit && hit->distance < closestDistance) {
                    closestDistance = hit->distance;
                    hit->entity = entity;
                    closestHit = hit;
                }
            }

            return closestHit;
        }

        // Check if a ray intersects with an AABB
        std::optional<BlockHitInfo> rayAABBIntersection(
            const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
            const glm::vec3& boxPosition, const AABBColliderComponent& box) {
            
            glm::vec3 boxMin = box.getMinCorner(boxPosition);
            glm::vec3 boxMax = box.getMaxCorner(boxPosition);

            // Ray-AABB intersection using slab method
            glm::vec3 invDir = 1.0f / rayDirection;
            glm::vec3 t0 = (boxMin - rayOrigin) * invDir;
            glm::vec3 t1 = (boxMax - rayOrigin) * invDir;

            glm::vec3 tmin = glm::min(t0, t1);
            glm::vec3 tmax = glm::max(t0, t1);

            float tEnter = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
            float tExit  = glm::min(glm::min(tmax.x, tmax.y), tmax.z);

            if (tEnter < tExit && tExit > 0 && tEnter > 0) {
                // Hit -> Calculate which face was hit
                glm::vec3 hitPoint = rayOrigin + rayDirection * tEnter;
                glm::vec3 hitNormal = glm::vec3(0.0f);

                // Determine which face was hit based on which component of tmin is largest
                if (tmin.x == tEnter) {
                    hitNormal = glm::vec3(t0.x < t1.x ? -1.0f : 1.0f, 0.0f, 0.0f);
                } else if (tmin.y == tEnter) {
                    hitNormal = glm::vec3(0.0f, t0.y < t1.y ? -1.0f : 1.0f, 0.0f);
                } else {
                    hitNormal = glm::vec3(0.0f, 0.0f, t0.z < t1.z ? -1.0f : 1.0f);
                }

                return BlockHitInfo{nullptr, hitPoint, hitNormal, tEnter};
            }

            return std::nullopt;
        }

        // Mine a block (remove it from the world)
        void mineBlock(Entity* blockEntity, PlayerComponent* player, World* world) {
            if (!blockEntity) return;

            // Remove the block from the world
            world->markForRemoval(blockEntity);
            
            // Gain resources when mining
            player->resourcesCollected++;
            if (player->resourcesCollected >= player->resourcesRequired) {
                player->gameState = GameState::WIN;
            }
        }

        // Place a block in the world
        void placeBlock(const BlockHitInfo& hitInfo, World* world, PlayerComponent* player) {
            if (!hitInfo.entity) return;

            // TODO
            // 1. Find the adjacent empty space where the block should be placed
            // 2. Create a new entity with the block components
            // 3. Update the terrain/block data structure
        }
    };

}
