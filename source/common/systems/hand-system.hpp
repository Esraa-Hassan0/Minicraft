#pragma once

#include "../ecs/world.hpp"
#include "../components/hand.hpp"
#include "../components/mesh-renderer.hpp"
#include "../voxel/world.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>

namespace our {

    // HandSystem manages first-person hand animation and interaction
    // Handles idle animations, hit detection, targeting, and collision avoidance
    class HandSystem {
    public:
        
        // Update hand animation based on camera and scene state
        static void update(
            Entity* handEntity,
            HandComponent* handComp,
            const glm::vec3& cameraPosition,
            const glm::vec3& cameraDirection,
            const glm::mat4& cameraMatrix,
            double deltaTime,
            bool isHittingNow,
            bool hasTargetInRange,
            const glm::vec3& targetPosition,
            const voxel::World* terrainWorld = nullptr
        ) {
            if (!handEntity || !handComp) return;
            
            static bool debugOnce = false;
            if (!debugOnce) {
                std::cout << "DEBUG HandSystem: update() called\n";
                std::cout << "  handEntity: " << handEntity << "\n";
                std::cout << "  handComp: " << handComp << "\n";
                debugOnce = true;
            }
            
            // Update animation timers
            handComp->idleAnimationTimer += (float)deltaTime;
            
            // Handle hit animation state
            if (isHittingNow) {
                handComp->isHitting = true;
                handComp->hitAnimationTimer = 0.0f;
            }
            
            if (handComp->isHitting) {
                handComp->hitAnimationTimer += (float)deltaTime * handComp->hitAnimationSpeed;
                
                // Check if hit animation is complete
                if (handComp->hitAnimationTimer > glm::pi<float>()) {
                    handComp->hitAnimationTimer = 0.0f;
                    if (!isHittingNow) {
                        handComp->isHitting = false;
                    }
                }
            }
            
            // Calculate base position and rotation
            glm::vec3 targetPos = handComp->basePosition;
            glm::vec3 targetRot = handComp->baseRotation;
            
            // Apply idle animation (subtle breathing/swaying)
            applyIdleAnimation(handComp, targetPos, targetRot);
            
            // Apply hit animation (punch forward)
            if (handComp->isHitting) {
                applyHitAnimation(handComp, targetPos, targetRot, hasTargetInRange, targetPosition, cameraMatrix);
            }
            
            // Apply targeting animation (align hand toward target)
            if (hasTargetInRange && !handComp->isHitting) {
                applyTargetingAnimation(handComp, cameraMatrix, targetPosition, handComp->basePosition, targetRot);
            } else {
                handComp->hasActiveTarget = false;
            }
            
            // Apply collision avoidance
            if (terrainWorld) {
                applyCollisionAvoidance(handComp, cameraPosition, cameraDirection, terrainWorld, targetPos);
            }
            
            // Clamp position to viewport constraints
            constrainPosition(handComp, targetPos);
            
            // Smoothly interpolate to target position and rotation
            float lerpSpeed = 10.0f * (float)deltaTime;
            handComp->currentPosition = glm::mix(handComp->currentPosition, targetPos, lerpSpeed);
            handComp->currentRotation = glm::mix(handComp->currentRotation, targetRot, lerpSpeed);
            
            // Apply to entity transform
            handEntity->localTransform.position = handComp->currentPosition;
            handEntity->localTransform.rotation = handComp->currentRotation;
        }
        
    private:
        
        // Apply subtle idle animation (breathing/swaying motion)
        static void applyIdleAnimation(HandComponent* handComp, glm::vec3& position, glm::vec3& rotation) {
            float time = handComp->idleAnimationTimer;
            
            // Subtle X sway (side-to-side)
            position.x += std::sin(time * handComp->idleSwaySpeedX) * handComp->idleSwayAmplitude;
            
            // Subtle Y sway (up-down breathing)
            position.y += std::cos(time * handComp->idleSwaySpeedY) * handComp->idleSwayAmplitude;
            
            // Subtle rotation sway
            rotation.x += std::sin(time * handComp->idleSwaySpeedX) * handComp->idleRotationAmplitude;
        }
        
        // Apply hit animation (punch forward motion)
        static void applyHitAnimation(HandComponent* handComp, glm::vec3& position, glm::vec3& rotation, bool hasTarget, const glm::vec3& targetPosWorld, const glm::mat4& cameraMatrix) {
            float time = handComp->hitAnimationTimer;
            float punch = std::sin(time);
            
            if (hasTarget) {
                // Convert target world position to local camera space
                glm::mat4 invCam = glm::inverse(cameraMatrix);
                glm::vec4 localTarget4 = invCam * glm::vec4(targetPosWorld, 1.0f);
                glm::vec3 localTarget = glm::vec3(localTarget4) / localTarget4.w;
                
                // Vector from hand base to target in local space
                glm::vec3 toTarget = localTarget - handComp->basePosition;
                
                // Limit the stretch distance so it doesn't go too far
                float maxStretch = 0.5f; 
                float stretchAmount = glm::min(glm::length(toTarget) - 0.2f, maxStretch);
                if (stretchAmount < 0.0f) stretchAmount = 0.0f;
                
                glm::vec3 stretchDir = glm::normalize(toTarget);
                position += stretchDir * punch * stretchAmount;
            } else {
                position.z -= punch * handComp->hitForwardThrust;
            }
            
            // Side sway during punch
            position.x -= punch * handComp->hitSideMotion;
            
            // Rotation during punch
            rotation.y += punch * handComp->hitRotationMotion;
        }
        
        // Apply targeting animation (hand aims at target)
        static void applyTargetingAnimation(
            HandComponent* handComp,
            const glm::mat4& cameraMatrix,
            const glm::vec3& hitWorldPos,
            const glm::vec3& handBasePos,
            glm::vec3& targetRot
        ) {
            // Convert world position to camera-relative position
            glm::mat4 invCam = glm::inverse(cameraMatrix);
            glm::vec4 localTarget4 = invCam * glm::vec4(hitWorldPos, 1.0f);
            glm::vec3 localTarget = glm::vec3(localTarget4) / localTarget4.w;
            
            // Calculate direction from hand base to target
            glm::vec3 dirToTarget = glm::normalize(localTarget - handBasePos);
            
            // Apply slight rotation bias towards target
            // X rotation based on vertical offset
            targetRot.x += dirToTarget.y * handComp->targetingRotationInfluence;
            
            // Y rotation based on horizontal offset
            targetRot.y -= dirToTarget.x * handComp->targetingRotationInfluence;
            
            handComp->hasActiveTarget = true;
        }
        
        // Helper function to check if a point in world space contains a solid block
        static bool isBlockSolid(const voxel::World* terrainWorld, const glm::vec3& worldPos) {
            int bx = static_cast<int>(std::floor(worldPos.x));
            int by = static_cast<int>(std::floor(worldPos.y));
            int bz = static_cast<int>(std::floor(worldPos.z));
            int blockType = terrainWorld->getBlock(bx, by, bz);
            return (blockType != voxel::AIR && blockType != voxel::WATER);
        }
        
        // Helper function to check if a path is clear (multiple samples)
        static bool isPathClear(
            const voxel::World* terrainWorld,
            const glm::vec3& startPos,
            const glm::vec3& endPos,
            int samples = 3
        ) {
            for (int i = 1; i <= samples; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(samples + 1);
                glm::vec3 checkPos = glm::mix(startPos, endPos, t);
                if (isBlockSolid(terrainWorld, checkPos)) {
                    return false;
                }
            }
            return true;
        }
        
        // Detect and avoid collision with nearby objects by moving hand laterally (left/right/up/down)
        static void applyCollisionAvoidance(
            HandComponent* handComp,
            const glm::vec3& cameraPosition,
            const glm::vec3& cameraDirection,
            const voxel::World* terrainWorld,
            glm::vec3& position
        ) {
            if (!terrainWorld) return;
            
            float checkDistance = handComp->collisionAvoidanceDistance * 1.5f;  // Increased lookahead
            
            // Build local coordinate system for camera
            glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 cameraRight = glm::normalize(glm::cross(cameraDirection, cameraUp));
            glm::vec3 cameraActualUp = glm::normalize(glm::cross(cameraRight, cameraDirection));
            
            // Calculate hand position in world space
            glm::vec3 handWorldPos = cameraPosition + position;
            
            // Check for collision ahead
            glm::vec3 checkPointAhead = handWorldPos + cameraDirection * checkDistance;
            bool hasCollision = isBlockSolid(terrainWorld, checkPointAhead);
            
            // If no collision ahead, check slightly offset to catch nearby walls
            if (!hasCollision) {
                glm::vec3 checkPointAheadLeft = handWorldPos + cameraDirection * checkDistance - cameraRight * 0.2f;
                glm::vec3 checkPointAheadRight = handWorldPos + cameraDirection * checkDistance + cameraRight * 0.2f;
                hasCollision = isBlockSolid(terrainWorld, checkPointAheadLeft) || 
                              isBlockSolid(terrainWorld, checkPointAheadRight);
            }
            
            glm::vec3 targetOffset = glm::vec3(0.0f);
            
            if (hasCollision) {
                float offsetAmount = 0.35f;  // Larger offset to ensure clear movement
                
                // Try different directions with priority
                // Priority 1: Left
                glm::vec3 leftPos = cameraPosition + (position - cameraRight * offsetAmount);
                if (isPathClear(terrainWorld, cameraPosition + position, leftPos)) {
                    targetOffset = -cameraRight * offsetAmount;
                }
                // Priority 2: Right
                else {
                    glm::vec3 rightPos = cameraPosition + (position + cameraRight * offsetAmount);
                    if (isPathClear(terrainWorld, cameraPosition + position, rightPos)) {
                        targetOffset = cameraRight * offsetAmount;
                    }
                    // Priority 3: Up
                    else {
                        glm::vec3 upPos = cameraPosition + (position + cameraActualUp * offsetAmount);
                        if (isPathClear(terrainWorld, cameraPosition + position, upPos)) {
                            targetOffset = cameraActualUp * offsetAmount;
                        }
                        // Priority 4: Down (for when hand is high up)
                        else {
                            glm::vec3 downPos = cameraPosition + (position - cameraActualUp * offsetAmount);
                            if (isPathClear(terrainWorld, cameraPosition + position, downPos)) {
                                targetOffset = -cameraActualUp * offsetAmount;
                            }
                            // Priority 5: Backward (away from forward direction)
                            else {
                                glm::vec3 backPos = cameraPosition + (position - cameraDirection * 0.3f);
                                if (isPathClear(terrainWorld, cameraPosition + position, backPos)) {
                                    targetOffset = -cameraDirection * 0.3f;
                                }
                            }
                        }
                    }
                }
            }
            
            handComp->hasCollisionAhead = hasCollision;
            
            // Fast interpolation to target offset (more responsive collision avoidance)
            float lerpFactor = glm::min(handComp->collisionSmoothing * 0.032f, 1.0f);  // Faster response
            handComp->collisionAvoidanceOffset = glm::mix(
                handComp->collisionAvoidanceOffset,
                targetOffset,
                lerpFactor
            );
            
            // Apply avoidance offset to position (in camera space)
            position += handComp->collisionAvoidanceOffset;
        }
        
        // Constrain hand position to viewport boundaries
        static void constrainPosition(HandComponent* handComp, glm::vec3& position) {
            position.x = std::clamp(position.x, handComp->minPositionX, handComp->maxPositionX);
            position.y = std::clamp(position.y, handComp->minPositionY, handComp->maxPositionY);
            position.z = std::clamp(position.z, handComp->minPositionZ, handComp->maxPositionZ);
        }
    };
}
