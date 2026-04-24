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
#include <iostream>

struct Particle {
    our::Entity* entity;
    float timeToLive;
};

class BlockInteractionSystem {
public:
    enum class HoldResult {
        None,
        HitPulse,
        Broken
    };
    glm::ivec3 currentTargetContext = {-1, -1, -1};
    int currentHits = 0;
    float accumulatedBreakTime = 0.0f; 
    float particleSpawnTimer = 0.0f;
    std::vector<Particle> particles;
    our::World* worldContext = nullptr;
    

    void initialize(our::World* engineWorld) {
    }

    float getBreakDuration(int blockType, int toolType = 0) {
        float break_duration=0.4;
        switch (blockType) {
            case voxel::SAND:  break_duration= 0.35f; break;  
            case voxel::LOG:  break_duration= 1.5f; break;
            case voxel::STONE: break_duration= 7.5; break;
            case voxel::Diamond: break_duration = 30; break;
            case voxel::Glass: break_duration= 0.2f; 
        }
        std::cout<<"breakDuration is"<<break_duration<<"\n";
        int multiplier = 1;
        if (toolType == static_cast<int>(voxel::ToolType::WoodenPickaxe)) {
            if (blockType == voxel::STONE || blockType == voxel::Diamond) multiplier = 2;
        } else if (toolType == static_cast<int>(voxel::ToolType::StonePickaxe)) {
            if (blockType == voxel::STONE || blockType == voxel::Diamond) multiplier = 4;
        } else if (toolType == static_cast<int>(voxel::ToolType::WoodenAxe)) {
            if (blockType == voxel::WOOD || blockType == voxel::LOG) multiplier = 2;
        } else if (toolType == static_cast<int>(voxel::ToolType::StoneAxe)) {
            if (blockType == voxel::WOOD || blockType == voxel::LOG) multiplier = 4;
        }
        break_duration /= multiplier;
                std::cout<<"breakDuration2 is"<<break_duration<<"\n";
        return break_duration;
    }

    HoldResult processHold(const voxel::RayHit& hit, int blockType, voxel::World& terrainWorld, our::World* engineWorld, bool& terrainMeshDirty, float deltaTime) {
        glm::ivec3 hitPos(hit.x, hit.y, hit.z);

        if (hitPos != currentTargetContext) {
            currentTargetContext = hitPos;
            accumulatedBreakTime = 0.0f;
            particleSpawnTimer = 0.0f;
        }

        accumulatedBreakTime += deltaTime;
        particleSpawnTimer += deltaTime;
        
        HoldResult result = HoldResult::None;

        if (particleSpawnTimer > 0.25f) {
            spawnParticles(hitPos, blockType, engineWorld, false);
            particleSpawnTimer = 0.0f;
            result = HoldResult::HitPulse;
        }

        float requiredTime = getBreakDuration(blockType);
        
        if (accumulatedBreakTime >= requiredTime) {
            spawnParticles(hitPos, blockType, engineWorld, true);
            terrainWorld.breakBlock(hit);
            terrainMeshDirty = true;
            
            currentTargetContext = {-1, -1, -1};
            accumulatedBreakTime = 0.0f;
            return HoldResult::Broken;
        }
        return result;
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
