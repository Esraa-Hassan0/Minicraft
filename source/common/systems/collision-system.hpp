#pragma once

#include "../ecs/world.hpp"
#include "../components/aabb-collider.hpp"
#include "../components/player.hpp"
#include "../components/npc-movement.hpp"
#include "../voxel/world.hpp"
#include "../audio/audio.hpp"
#include <glm/glm.hpp>

namespace our {

    // Collision system handles physics, gravity, and AABB collision detection
    class CollisionSystem {
    public:
        // Update function called each frame
        void update(World* world, voxel::World* terrain, float deltaTime) {
            // Cap deltaTime to prevent tunneling during lag spikes
            if (deltaTime > 0.05f) deltaTime = 0.05f;

            // Update velocities (apply gravity)
            for (auto entity : world->getEntities()) {
                if (!entity) continue;
                AABBColliderComponent* collider = entity->getComponent<AABBColliderComponent>();
                
                // Get physics state from either Player or NPC component
                PlayerComponent* player = entity->getComponent<PlayerComponent>();
                NPCMovementComponent* npc = entity->getComponent<NPCMovementComponent>();
                
                glm::vec3* velocity = nullptr;
                bool* isUnderwater = nullptr;
                float gravityAcceleration = 9.8f;
                float waterGravityMultiplier = 0.5f;

                if (player) {
                    velocity = &player->velocity;
                    isUnderwater = &player->isUnderwater;
                    gravityAcceleration = player->gravityAcceleration;
                    waterGravityMultiplier = player->waterGravityMultiplier;
                } else if (npc) {
                    velocity = &npc->velocity;
                    // For NPCs we don't have isUnderwater yet, but we can add it if needed
                    // For now, let's just use a default or dummy
                }

                if (collider && velocity) {
                    // Detect if entity will be underwater this frame
                    if (terrain) {
                        glm::vec3 checkPos = entity->localTransform.position + (*velocity) * deltaTime;
                        glm::vec3 colliderMin = collider->getMinCorner(checkPos);
                        glm::vec3 colliderMax = collider->getMaxCorner(checkPos);
                        
                        bool inWater = false;
                        for (int cx = static_cast<int>(std::floor(colliderMin.x)); cx <= static_cast<int>(std::floor(colliderMax.x)) && !inWater; ++cx) {
                            for (int cy = static_cast<int>(std::floor(colliderMin.y)); cy <= static_cast<int>(std::floor(colliderMax.y)) && !inWater; ++cy) {
                                for (int cz = static_cast<int>(std::floor(colliderMin.z)); cz <= static_cast<int>(std::floor(colliderMax.z)) && !inWater; ++cz) {
                                    if (terrain->getBlock(cx, cy, cz) == voxel::WATER) {
                                        inWater = true;
                                    }
                                }
                            }
                        }
                        if (isUnderwater) {
                            if (inWater && !(*isUnderwater) && player) {
                                our::AudioSystem::playSound("assets/sounds/Splash.wav");
                            }
                            (*isUnderwater) = inWater;
                        }
                    }

                    // Apply gravity (reduced when underwater)
                    float gravityMult = (isUnderwater && *isUnderwater) ? waterGravityMultiplier : 1.0f;
                    velocity->y -= gravityAcceleration * gravityMult * deltaTime;
                    
                    // Cap falling speed to prevent instability
                    const float maxFallSpeed = 50.0f;
                    if (velocity->y < -maxFallSpeed) {
                        velocity->y = -maxFallSpeed;
                    }
                }
            }

            // Move entities and handle collisions
            for (auto entity : world->getEntities()) {
                if (!entity) continue;
                AABBColliderComponent* collider = entity->getComponent<AABBColliderComponent>();
                
                if (collider && collider->isPhysical) {
                    PlayerComponent* player = entity->getComponent<PlayerComponent>();
                    NPCMovementComponent* npc = entity->getComponent<NPCMovementComponent>();
                    
                    glm::vec3* velocity = nullptr;
                    bool* isGrounded = nullptr;
                    if (player) {
                        velocity = &player->velocity;
                        isGrounded = &player->isGrounded;
                    } else if (npc) {
                        velocity = &npc->velocity;
                        isGrounded = &npc->isGrounded;
                    }

                    // Update position based on velocity
                    glm::vec3 newPosition = entity->localTransform.position;
                    if (velocity) {
                        newPosition += (*velocity) * deltaTime;
                    }

                    // Check collisions with other entities
                    bool collidedVertically = false;

                    if (terrain) {
                        // Use a small epsilon to avoid being blocked by the block we are standing on or touching
                        const float epsilon = 0.001f;
                        glm::vec3 minCorner = collider->getMinCorner(newPosition);
                        glm::vec3 maxCorner = collider->getMaxCorner(newPosition);

                        int minX = static_cast<int>(std::floor(minCorner.x + epsilon));
                        int maxX = static_cast<int>(std::floor(maxCorner.x - epsilon));
                        int minY = static_cast<int>(std::floor(minCorner.y + epsilon));
                        int maxY = static_cast<int>(std::floor(maxCorner.y - epsilon));
                        int minZ = static_cast<int>(std::floor(minCorner.z + epsilon));
                        int maxZ = static_cast<int>(std::floor(maxCorner.z - epsilon));

                        for (int x = minX; x <= maxX; ++x) {
                            for (int y = minY; y <= maxY; ++y) {
                                for (int z = minZ; z <= maxZ; ++z) {
                                    int blockType = terrain->getBlock(x, y, z);
                                    if (blockType != 0 && blockType != voxel::WATER) { // Air is 0, WATER is non-solid
                                        AABBColliderComponent blockCollider;
                                        blockCollider.center = glm::vec3(0.0f);
                                        blockCollider.halfSize = glm::vec3(0.5f);
                                        glm::vec3 blockPos(x + 0.5f, y + 0.5f, z + 0.5f);

                                        glm::vec3 penetration = collider->getPenetration(newPosition, blockCollider, blockPos);
                                        
                                        float minPenetrationType = glm::min(glm::abs(penetration.x), 
                                                                            glm::min(glm::abs(penetration.y), glm::abs(penetration.z)));
                                        
                                        if (glm::abs(penetration.y) == minPenetrationType && penetration.y != 0) {
                                            newPosition.y += penetration.y;
                                            if (velocity && penetration.y > 0) {
                                                velocity->y = 0;
                                                if (isGrounded) *isGrounded = true;
                                                collidedVertically = true;
                                            } else if (velocity && penetration.y < 0) {
                                                velocity->y = 0;
                                            }
                                        } else if (glm::abs(penetration.x) == minPenetrationType && penetration.x != 0) {
                                            newPosition.x += penetration.x;
                                            if (velocity) velocity->x = 0;
                                        } else if (glm::abs(penetration.z) == minPenetrationType && penetration.z != 0) {
                                            newPosition.z += penetration.z;
                                            if (velocity) velocity->z = 0;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    for (auto otherEntity : world->getEntities()) {
                        if (!otherEntity || otherEntity == entity) continue;

                        AABBColliderComponent* otherCollider = otherEntity->getComponent<AABBColliderComponent>();
                        if (!otherCollider) continue;

                        // Check collision layer compatibility
                        if ((collider->collisionMask & (1 << otherCollider->collisionLayer)) == 0) {
                            continue;
                        }

                        // Check if overlapping
                        if (collider->overlaps(newPosition, *otherCollider, otherEntity->localTransform.position)) {
                            if (otherCollider->isTrigger) {
                                // Trigger collision - just detect, don't resolve
                                handleTriggerCollision(entity, otherEntity, player);
                            } else if (otherCollider->isPhysical) {
                                // Physical collision - resolve by pushing out
                                glm::vec3 penetration = collider->getPenetration(newPosition, *otherCollider, otherEntity->localTransform.position);
                                
                                // Resolve collision by moving in the direction of smallest penetration
                                float minPenetrationType = glm::min(glm::abs(penetration.x), 
                                                                    glm::min(glm::abs(penetration.y), glm::abs(penetration.z)));
                                
                                if (glm::abs(penetration.y) == minPenetrationType && penetration.y != 0) {
                                    // Vertical collision
                                    newPosition.y += penetration.y;
                                    if (velocity && penetration.y > 0) {
                                        // Landing on something
                                        velocity->y = 0;
                                        if (isGrounded) *isGrounded = true;
                                        collidedVertically = true;
                                    } else if (velocity && penetration.y < 0) {
                                        // Hit head
                                        velocity->y = 0;
                                    }
                                } else if (glm::abs(penetration.x) == minPenetrationType && penetration.x != 0) {
                                    // Horizontal collision (X)
                                    newPosition.x += penetration.x;
                                    if (velocity) velocity->x = 0;
                                } else if (glm::abs(penetration.z) == minPenetrationType && penetration.z != 0) {
                                    // Horizontal collision (Z)
                                    newPosition.z += penetration.z;
                                    if (velocity) velocity->z = 0;
                                }
                            }
                        }
                    }

                    // Update entity position
                    entity->localTransform.position = newPosition;

                    // Handle grounding (check if entity is on the ground)
                    if (isGrounded) {
                        // If we didn't collide vertically this frame, we're falling
                        if (!collidedVertically && (*isGrounded)) {
                            // Do one more check below to see if we're still on the ground
                            glm::vec3 checkPosition = entity->localTransform.position;
                            checkPosition.y -= 0.01f; // Small offset below
                            
                            bool stillGrounded = false;
                            
                            // Check ground against terrain precisely
                            if (terrain) {
                                glm::vec3 minCorner = collider->getMinCorner(checkPosition);
                                glm::vec3 maxCorner = collider->getMaxCorner(checkPosition);
                                int minX = static_cast<int>(std::floor(minCorner.x + 0.01f));
                                int maxX = static_cast<int>(std::floor(maxCorner.x - 0.01f));
                                int minY = static_cast<int>(std::floor(minCorner.y + 0.01f));
                                int maxY = static_cast<int>(std::floor(maxCorner.y - 0.01f));
                                int minZ = static_cast<int>(std::floor(minCorner.z + 0.01f));
                                int maxZ = static_cast<int>(std::floor(maxCorner.z - 0.01f));

                                for (int x = minX; x <= maxX; ++x) {
                                     for (int y = minY; y <= maxY; ++y) {
                                         for (int z = minZ; z <= maxZ; ++z) {
                                             int blockType = terrain->getBlock(x, y, z);
                                             if (blockType != 0 && blockType != voxel::WATER) { // Air is 0, WATER is non-solid
                                                 stillGrounded = true;
                                                 break;
                                             }
                                         }
                                        if (stillGrounded) break;
                                    }
                                    if (stillGrounded) break;
                                }
                            }

                            if (!stillGrounded) {
                                for (auto otherEntity : world->getEntities()) {
                                    if (!otherEntity || otherEntity == entity) continue;
                                    
                                    AABBColliderComponent* otherCollider = otherEntity->getComponent<AABBColliderComponent>();
                                    if (!otherCollider || otherCollider->isTrigger) continue;

                                    if (collider->overlaps(checkPosition, *otherCollider, otherEntity->localTransform.position)) {
                                        stillGrounded = true;
                                        break;
                                    }
                                }
                            }
                            
                            (*isGrounded) = stillGrounded;
                        }
                    }
                }
            }
        }

    private:
        // Handle trigger collisions (resource pickups, exit zone)
        void handleTriggerCollision(Entity* entity, Entity* triggerEntity, PlayerComponent* player) {
            if (!player) return;
            // todo
        }
    };

}
