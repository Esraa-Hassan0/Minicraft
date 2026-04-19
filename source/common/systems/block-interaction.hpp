#pragma once

#include <ecs/world.hpp>
#include <components/mesh-renderer.hpp>
#include <components/movement.hpp>
#include <voxel/world.hpp>
#include <asset-loader.hpp>
#include <material/material.hpp>
#include <string>
#include <vector>
#include <cstdlib>

struct Particle {
    our::Entity* entity;
    float timeToLive;
};

class BlockInteractionSystem {
public:
    glm::ivec3 currentTargetContext = {-1, -1, -1};
    int currentHits = 0;
    float timeSinceLastHit = 0.0f;
    std::vector<Particle> particles;
    our::World* worldContext = nullptr;
    
    // Configurable hit durations
    const float hitTimeout = 1.0f; 

    void initialize(our::World* engineWorld) {
    }

    int getMaxHits(int blockType) {
        switch (blockType) {
            case voxel::GRASS:
            case voxel::DIRT:
            case voxel::SAND:  return 4;
            case voxel::WOOD:  return 6;
            case voxel::STONE: return 8;
            case voxel::LEAF:  
            case voxel::Glass: return 2;
            default:           return 1;
        }
    }

    void processClick(const RayHit& hit, int blockType, voxel::World& terrainWorld, our::World* engineWorld, bool& terrainMeshDirty) {
        glm::ivec3 hitPos(hit.x, hit.y, hit.z);
        if (hitPos != currentTargetContext) {
            currentTargetContext = hitPos;
            currentHits = 1;
            timeSinceLastHit = 0.0f;
        } else {
            currentHits++;
            timeSinceLastHit = 0.0f;
        }

        int maxHits = getMaxHits(blockType);
        if (currentHits >= maxHits) {
            // Break block
            spawnParticles(hitPos, blockType, engineWorld, true);
            terrainWorld.breakBlock(hit);
            terrainMeshDirty = true;
            currentTargetContext = {-1, -1, -1};
            currentHits = 0;
            return;
        } else {
            spawnParticles(hitPos, blockType, engineWorld, false);
        }
    }

    void spawnParticles(const glm::ivec3& pos, int blockType, our::World* engineWorld, bool isBroken) {
        // Map the broken block type back to an established visual material
        std::string matName = "stone";
        if (blockType == voxel::DIRT) matName = "dirt";
        else if (blockType == voxel::GRASS) matName = "dirt";
        else if (blockType == voxel::SAND) matName = "sand";
        else if (blockType == voxel::WOOD) matName = "wood";
        else if (blockType == voxel::Glass) matName = "glass";

        our::Material* material = our::AssetLoader<our::Material>::get(matName);
        our::Mesh* mesh = our::AssetLoader<our::Mesh>::get("cube");
        
        if (!material || !mesh) return; // Fallback safeguards

        int count = isBroken ? 12 : 5;
        for (int i = 0; i < count; ++i) {
            our::Entity* p = engineWorld->add();
            p->localTransform.position = glm::vec3(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f);
            
            // Random small offset so they aren't exactly overlapping
            p->localTransform.position.x += ((rand() % 100) / 100.0f - 0.5f) * 0.8f;
            p->localTransform.position.y += ((rand() % 100) / 100.0f - 0.5f) * 0.8f;
            p->localTransform.position.z += ((rand() % 100) / 100.0f - 0.5f) * 0.8f;

            if (isBroken) {
                p->localTransform.scale = glm::vec3(0.15f + ((rand() % 100) / 100.0f) * 0.15f);
            } else {
                p->localTransform.scale = glm::vec3(0.05f + ((rand() % 100) / 100.0f) * 0.05f);
            }
            
            auto* mr = p->addComponent<our::MeshRendererComponent>();
            mr->mesh = mesh;
            mr->material = material;

            auto* mov = p->addComponent<our::MovementComponent>();
            
            // Give them a random burst velocity
            float rx = ((rand() % 100) / 100.0f) * 2.0f - 1.0f;
            float ry = ((rand() % 100) / 100.0f) * 2.0f - 0.5f; // mostly upwards slightly
            float rz = ((rand() % 100) / 100.0f) * 2.0f - 1.0f;
            glm::vec3 dir = glm::normalize(glm::vec3(rx, ry, rz));
            
            if (isBroken) {
                mov->linearVelocity = dir * (2.0f + ((rand() % 100) / 100.0f) * 3.0f);
                mov->angularVelocity = dir * 10.0f;
                // They live for about 1.5 to 2.5 seconds
                particles.push_back({p, 1.5f + ((rand() % 100) / 100.0f) * 1.5f}); 
            } else {
                mov->linearVelocity = dir * (1.0f + ((rand() % 100) / 100.0f) * 2.0f);
                mov->angularVelocity = dir * 5.0f;
                // They live very short
                particles.push_back({p, 0.2f + ((rand() % 100) / 100.0f) * 0.3f}); 
            }
        }
    }

    void update(float deltaTime, our::World* engineWorld) {
        // Reset the broken state if the player stops hitting it for a little bit
        if (currentTargetContext.x != -1) {
            timeSinceLastHit += deltaTime;
            if (timeSinceLastHit > 1.0f) { // 1 second timeout
                currentTargetContext = {-1, -1, -1};
                currentHits = 0;
            }
        }

        // Process existing particles and clean them up when life ends
        for (auto it = particles.begin(); it != particles.end();) {
            it->timeToLive -= deltaTime;
            if (it->timeToLive <= 0) {
                engineWorld->markForRemoval(it->entity);
                it = particles.erase(it);
            } else {
                auto* mov = it->entity->getComponent<our::MovementComponent>();
                if (mov) mov->linearVelocity.y -= 9.8f * deltaTime;
                ++it;
            }
        }
    }
};
