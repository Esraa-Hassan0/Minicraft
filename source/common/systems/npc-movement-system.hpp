#pragma once

#include "../ecs/world.hpp"
#include "../components/npc-movement.hpp"
#include "../components/killable-npc.hpp"
#include "../components/aabb-collider.hpp"
#include "../voxel/world.hpp"
#include "../voxel/types.hpp"

#include <cstdlib>
#include <ctime>

namespace our
{

    class NPCMovementSystem
    {
    private:
        our::Entity *playerEntity = nullptr;
        glm::vec3 playerPos = glm::vec3(0.0f);
        voxel::World *terrainWorld = nullptr;

        static constexpr float GRAVITY = -20.0f;
        static constexpr float AIRBORNE_GROUND_PROBE = 0.05f;
        static constexpr float GROUNDED_CONFIRM_PROBE = 0.5f;  // Must be > halfSize.y to probe below surface

        bool isValidNPCPosition(our::Entity *entity, const glm::vec3 &newPos)
        {
            auto *aabb = entity->getComponent<our::AABBColliderComponent>();
            if (!aabb)
            {
                if (!terrainWorld)
                    return true;
                int bx = static_cast<int>(std::floor(newPos.x));
                int by = static_cast<int>(std::floor(newPos.y + 0.5f));
                int bz = static_cast<int>(std::floor(newPos.z));
                return terrainWorld->getBlock(bx, by, bz) == 0;
            }

            glm::vec3 min = aabb->getMinCorner(newPos);
            glm::vec3 max = aabb->getMaxCorner(newPos);

            int minX = static_cast<int>(std::floor(min.x));
            int minY = static_cast<int>(std::floor(min.y));
            int minZ = static_cast<int>(std::floor(min.z));
            int maxX = static_cast<int>(std::floor(max.x));
            int maxY = static_cast<int>(std::floor(max.y));
            int maxZ = static_cast<int>(std::floor(max.z));

            if (!terrainWorld)
                return true;

            for (int x = minX; x <= maxX; ++x)
            {
                for (int y = minY; y <= maxY; ++y)
                {
                    for (int z = minZ; z <= maxZ; ++z)
                    {
                        int block = terrainWorld->getBlock(x, y, z);
                        // Treat any non-air block (including WATER) as blocking for NPCs
                        if (block != 0)
                            return false;
                    }
                }
            }
            return true;
        }

        bool isValidVerticalMove(our::Entity *entity, const glm::vec3 &newPos)
        {
            glm::vec3 verticalOnly;
            verticalOnly.x = entity->localTransform.position.x;
            verticalOnly.y = newPos.y;
            verticalOnly.z = entity->localTransform.position.z;
            return isValidNPCPosition(entity, verticalOnly);
        }

        bool isWaterNearby(our::Entity *entity, const glm::vec3 &pos)
        {
            if (!terrainWorld)
                return false;

            auto *aabb = entity->getComponent<our::AABBColliderComponent>();
            float checkY = pos.y - 0.5f;

            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dz = -1; dz <= 1; dz++)
                {
                    int bx = static_cast<int>(std::floor(pos.x)) + dx;
                    int by = static_cast<int>(std::floor(checkY));
                    int bz = static_cast<int>(std::floor(pos.z)) + dz;
                    int block = terrainWorld->getBlock(bx, by, bz);
                    if (block == voxel::WATER)
                        return true;
                }
            }
            return false;
        }

        bool isValidHorizontalMove(our::Entity *entity, const glm::vec3 &newPos)
        {
            if (isWaterNearby(entity, newPos))
                return false;

            glm::vec3 horizontalPos = newPos;
            horizontalPos.y = entity->localTransform.position.y;
            return isValidNPCPosition(entity, horizontalPos);
        }

        void applyGravity(our::Entity *entity, our::NPCMovementComponent *movement, float deltaTime)
        {
            // Clamp first - grounded NPCs should never accumulate gravity
            if (movement->isGrounded)
            {
                movement->velocity.y = 0.0f;
            }
            else
            {
                // Only accumulate gravity when airborne
                movement->velocity.y += GRAVITY * deltaTime;
                // Cap terminal velocity to prevent huge probe depths
                movement->velocity.y = std::max(movement->velocity.y, -20.0f);
            }

            if (movement->velocity.y != 0.0f)
            {
                glm::vec3 newPos = entity->localTransform.position;
                newPos.y += movement->velocity.y * deltaTime;

                if (isValidVerticalMove(entity, newPos))
                {
                    entity->localTransform.position = newPos;
                }
                else
                {
                    if (movement->velocity.y < 0.0f)
                        movement->isGrounded = true;
                    movement->velocity.y = 0.0f;
                }
            }

            if (!movement->isGrounded)
            {
                // Cap probe at 1.0f to prevent skipping more than one block
                float probeDepth = std::min(1.0f, std::max(0.1f, std::abs(movement->velocity.y) * deltaTime + 0.05f));
                glm::vec3 probePos = entity->localTransform.position;
                probePos.y -= probeDepth;
                printf("[PROBE] depth=%.2f checking y=%.2f\n", probeDepth, probePos.y);
                if (!isValidVerticalMove(entity, probePos))
                {
                    printf("[PROBE] grounded!\n");
                    auto *aabb = entity->getComponent<our::AABBColliderComponent>();
                    if (aabb)
                    {
                        // Calculate min Y of the AABB when it hits the block
                        float minY = probePos.y + aabb->center.y - aabb->halfSize.y;
                        float surfaceY = std::floor(minY) + 1.0f;
                        entity->localTransform.position.y = surfaceY - aabb->center.y + aabb->halfSize.y;
                        printf("[PROBE] snapped to y=%.2f\n", entity->localTransform.position.y);
                    }
                    movement->isGrounded = true;
                    movement->velocity.y = 0.0f;
                }
            }

            if (movement->isGrounded)
            {
                glm::vec3 probePos = entity->localTransform.position;
                probePos.y -= GROUNDED_CONFIRM_PROBE;
                if (isValidVerticalMove(entity, probePos))
                    movement->isGrounded = false;
            }
        }

    public:
        void initialize(our::Entity *player, voxel::World *terrain)
        {
            srand(time(NULL));
            playerEntity = player;
            terrainWorld = terrain;
            if (playerEntity)
                playerPos = playerEntity->localTransform.position;
        }

        void destroy()
        {
            playerEntity = nullptr;
            terrainWorld = nullptr;
            playerPos = glm::vec3(0.0f);
        }

        void update(our::World *world, float deltaTime)
        {
            if (!playerEntity)
            {
                for (auto entity : world->getEntities())
                {
                    if (!entity)
                        continue;
                    auto *player = entity->getComponent<our::PlayerComponent>();
                    auto *camera = entity->getComponent<our::CameraComponent>();
                    if (player && camera)
                    {
                        playerEntity = entity;
                        playerPos = entity->localTransform.position;
                        break;
                    }
                }
            }
            else
            {
                playerPos = playerEntity->localTransform.position;
            }

            if (!playerEntity || !terrainWorld)
                return;

            for (auto entity : world->getEntities())
            {
                if (!entity)
                    continue;
                auto *movement = entity->getComponent<our::NPCMovementComponent>();
                if (!movement)
                    continue;

                if (!movement->initialized)
                {
                    movement->startPosition = entity->localTransform.position;
                    if (movement->movementType == our::NPCMovementComponent::MovementType::PATROL)
                        movement->targetPosition = movement->startPosition + movement->patrolOffset;
                    else
                        movement->targetPosition = movement->startPosition;
                    // Zero Y on waypoints - gravity owns vertical position
                    movement->startPosition.y = 0.0f;
                    movement->targetPosition.y = 0.0f;
                    movement->initialized = true;
                }

                // Update jump timer and opportunistic jumping
                movement->jumpTimer += deltaTime;
                if (movement->isGrounded && movement->jumpTimer >= movement->jumpCooldown) {
                    float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
                    if (r < movement->jumpProbability) {
                        movement->velocity.y = movement->jumpForce;
                        movement->isGrounded = false;
                        movement->jumpTimer = 0.0f;
                    } else {
                        // reset timer even if not jumping to avoid constant checks
                        movement->jumpTimer = 0.0f;
                    }
                }

                applyGravity(entity, movement, deltaTime);

                switch (movement->movementType)
                {
                case our::NPCMovementComponent::MovementType::IDLE:
                    break;
                case our::NPCMovementComponent::MovementType::RANDOM_WALK:
                    updateRandomWalk(entity, movement, deltaTime);
                    break;
                case our::NPCMovementComponent::MovementType::PATROL:
                    updatePatrol(entity, movement, deltaTime);
                    break;
                case our::NPCMovementComponent::MovementType::FOLLOW_PLAYER:
                    updateFollowPlayer(entity, movement, deltaTime);
                    break;
                }
            }

            // Resolve NPC-vs-NPC overlaps after all movement
            resolveNPCOverlaps(world);
        }

        void resolveNPCOverlaps(our::World *world)
        {
            struct NPCCollider {
                our::Entity *entity;
                our::AABBColliderComponent *aabb;
                our::NPCMovementComponent *movement;
            };

            std::vector<NPCCollider> npcs;
            for (auto *entity : world->getEntities())
            {
                if (!entity)
                    continue;
                auto *mv = entity->getComponent<our::NPCMovementComponent>();
                auto *ab = entity->getComponent<our::AABBColliderComponent>();
                if (mv && ab)
                    npcs.push_back({entity, ab, mv});
            }

            for (int i = 0; i < (int)npcs.size(); i++)
            {
                for (int j = i + 1; j < (int)npcs.size(); j++)
                {
                    auto &a = npcs[i];
                    auto &b = npcs[j];

                    glm::vec3 posA = a.entity->localTransform.position + a.aabb->center;
                    glm::vec3 posB = b.entity->localTransform.position + b.aabb->center;

                    glm::vec3 delta = posA - posB;
                    glm::vec3 overlap;
                    overlap.x = (a.aabb->halfSize.x + b.aabb->halfSize.x) - std::abs(delta.x);
                    overlap.y = (a.aabb->halfSize.y + b.aabb->halfSize.y) - std::abs(delta.y);
                    overlap.z = (a.aabb->halfSize.z + b.aabb->halfSize.z) - std::abs(delta.z);

                    if (overlap.x <= 0 || overlap.y <= 0 || overlap.z <= 0)
                        continue;

                    glm::vec3 push = glm::vec3(0.0f);
                    if (overlap.x < overlap.z)
                        push.x = (delta.x > 0 ? 1.0f : -1.0f) * overlap.x * 0.5f;
                    else
                        push.z = (delta.z > 0 ? 1.0f : -1.0f) * overlap.z * 0.5f;

                    glm::vec3 newPosA = a.entity->localTransform.position + push;
                    glm::vec3 newPosB = b.entity->localTransform.position - push;

                    if (isValidNPCPosition(a.entity, newPosA))
                        a.entity->localTransform.position = newPosA;

                    if (isValidNPCPosition(b.entity, newPosB))
                        b.entity->localTransform.position = newPosB;
                }
            }
        }

    private:
        void updateRandomWalk(our::Entity *entity, our::NPCMovementComponent *movement, float deltaTime)
        {

            if (!movement->isMoving)
            {
                movement->waitTimer += deltaTime;
                if (movement->waitTimer >= movement->waitTime)
                {
                    float angle = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f * 3.14159265f;
                    float dist = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * movement->moveRadius;
                    glm::vec3 offset(std::cos(angle) * dist, 0.0f, std::sin(angle) * dist);
                    glm::vec3 newPos = movement->startPosition + offset;
                    if (isValidHorizontalMove(entity, newPos))
                    {
                        movement->targetPosition = newPos;
                        movement->isMoving = true;
                    }
                    movement->waitTimer = 0.0f;
                }
            }
            else
            {
                glm::vec3 dir = movement->targetPosition - entity->localTransform.position;
                dir.y = 0.0f;  // Flatten to XZ
                float dist = glm::length(dir);
                if (dist > 0.1f)
                {
                    glm::vec3 moveDir = glm::normalize(dir);
                    glm::vec3 newPos = entity->localTransform.position + moveDir * movement->speed * deltaTime;
                    newPos.y = entity->localTransform.position.y;  // Preserve Y
                    if (isValidHorizontalMove(entity, newPos))
                    {
                        entity->localTransform.position = newPos;
                        movement->blockedAttempts = 0;
                    }
                    else
                    {
                        movement->blockedAttempts++;
                        if (movement->isGrounded && movement->blockedAttempts <= 2)
                        {
                            movement->velocity.y = 8.0f; // Jump force
                            movement->isGrounded = false;
                        }
                        else if (movement->blockedAttempts > 3)
                        {
                            movement->isMoving = false;
                            movement->blockedAttempts = 0;
                            movement->waitTimer = movement->waitTime;
                        }
                    }
                }
                else
                {
                    movement->isMoving = false;
                }
            }
        }

        void updatePatrol(our::Entity *entity, our::NPCMovementComponent *movement, float deltaTime)
        {

            if (!movement->isMoving)
            {
                movement->waitTimer += deltaTime;
                if (movement->waitTimer >= movement->waitTime)
                {
                    std::swap(movement->startPosition, movement->targetPosition);
                    movement->isMoving = true;
                    movement->waitTimer = 0.0f;
                }
            }
            else
            {
                glm::vec3 dir = movement->targetPosition - entity->localTransform.position;
                dir.y = 0.0f;  // Flatten to XZ - gravity handles Y
                float dist = glm::length(dir);
                if (dist > 0.1f)
                {
                    glm::vec3 moveDir = glm::normalize(dir);
                    glm::vec3 newPos = entity->localTransform.position + moveDir * movement->speed * deltaTime;
                    newPos.y = entity->localTransform.position.y;  // Preserve grounded Y
                    if (isValidHorizontalMove(entity, newPos))
                    {
                        entity->localTransform.position = newPos;
                        movement->blockedAttempts = 0;
                    }
                    else
                    {
                        movement->blockedAttempts++;
                        if (movement->isGrounded && movement->blockedAttempts <= 2)
                        {
                            movement->velocity.y = 8.0f; // Jump force
                            movement->isGrounded = false;
                        }
                        else if (movement->blockedAttempts > 3)
                        {
                            movement->isMoving = false;
                            movement->blockedAttempts = 0;
                            movement->waitTimer = movement->waitTime;
                        }
                    }
                }
                else
                {
                    movement->isMoving = false;
                }
            }
        }

        void updateFollowPlayer(our::Entity *entity, our::NPCMovementComponent *movement, float deltaTime)
        {

            glm::vec3 toPlayer = playerPos - entity->localTransform.position;
            toPlayer.y = 0.0f;  // Flatten to XZ
            float distToPlayer = glm::length(toPlayer);
            if (distToPlayer > 2.0f)
            {
                glm::vec3 moveDir = glm::normalize(toPlayer);
                glm::vec3 newPos = entity->localTransform.position + moveDir * movement->speed * deltaTime;
                newPos.y = entity->localTransform.position.y;  // Preserve Y
                if (isValidHorizontalMove(entity, newPos))
                {
                    entity->localTransform.position = newPos;
                    movement->blockedAttempts = 0;
                }
                else
                {
                    movement->blockedAttempts++;
                    if (movement->isGrounded && movement->blockedAttempts <= 2)
                    {
                        movement->velocity.y = 8.0f; // Jump force
                        movement->isGrounded = false;
                    }
                }
            }
        }
    };

}