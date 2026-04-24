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
        
        // Detect and avoid collision with nearby objects
        static void applyCollisionAvoidance(
            HandComponent* handComp,
            const glm::vec3& cameraPosition,
            const glm::vec3& cameraDirection,
            const voxel::World* terrainWorld,
            glm::vec3& position
        ) {
            // Cast a ray ahead of the hand to detect collisions
            // We need to transform hand position to world space first
            // For now, check if there's terrain in front
            
            float checkDistance = handComp->collisionAvoidanceDistance;
            
            // Calculate multiple check points around the hand
            std::vector<glm::vec3> checkPoints = {
                cameraPosition + cameraDirection * checkDistance,
                cameraPosition + cameraDirection * checkDistance + glm::vec3(0.1f, 0.0f, 0.0f),
                cameraPosition + cameraDirection * checkDistance + glm::vec3(-0.1f, 0.0f, 0.0f),
                cameraPosition + cameraDirection * checkDistance + glm::vec3(0.0f, 0.1f, 0.0f),
                cameraPosition + cameraDirection * checkDistance + glm::vec3(0.0f, -0.1f, 0.0f)
            };
            
            // Check for terrain blocks at these positions
            glm::vec3 avoidanceOffset = glm::vec3(0.0f);
            int collisionCount = 0;
            
            for (const auto& checkPoint : checkPoints) {
                int bx = static_cast<int>(std::floor(checkPoint.x));
                int by = static_cast<int>(std::floor(checkPoint.y));
                int bz = static_cast<int>(std::floor(checkPoint.z));
                
                int blockType = terrainWorld->getBlock(bx, by, bz);
                
                // If solid block detected, push hand away
                if (blockType != voxel::AIR && blockType != voxel::WATER) {
                    avoidanceOffset -= cameraDirection * 0.05f;
                    collisionCount++;
                }
            }
            
            // Smooth collision response
            handComp->collisionAvoidanceOffset = glm::mix(
                handComp->collisionAvoidanceOffset,
                avoidanceOffset,
                handComp->collisionSmoothing * 0.016f  // Assume 60fps, adjust as needed
            );
            
            // Apply avoidance offset to position
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
