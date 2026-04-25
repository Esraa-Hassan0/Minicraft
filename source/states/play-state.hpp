#pragma once

#include "menu-state.hpp"
#include <application.hpp>
#include <ecs/world.hpp>
#include <mesh/mesh-utils.hpp>
#include <systems/forward-renderer.hpp>
#include <systems/free-camera-controller.hpp>
#include <systems/movement.hpp>
#include <systems/player-controller.hpp>
#include <systems/collision-system.hpp>
#include <systems/light.hpp>
#include <systems/time-system.hpp>
#include <systems/block-interaction.hpp>
#include <systems/enemy-system.hpp>
#include <systems/npc-movement-system.hpp>
#include <systems/hand-system.hpp>
#include <asset-loader.hpp>
#include <texture/texture2d.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <components/player.hpp>
#include <components/enemy-component.hpp>
#include <components/killable-npc.hpp>
#include <components/npc-movement.hpp>
#include <components/hand.hpp>
#include <audio/audio.hpp>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <limits>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

class Playstate : public our::State {
    struct ChunkRenderGroup {
        std::vector<our::Entity*> entities;
        std::vector<our::Mesh*> meshes;
        std::vector<NPCSpawnData> npcs;
    };

    // Terrain and ECS Systems
    voxel::World terrainWorld;
    our::World engineWorld;
    our::ForwardRenderer renderer;
    our::FreeCameraControllerSystem cameraController;
    our::MovementSystem movementSystem;
    our::PlayerControllerSystem playerController;
    our::CollisionSystem collisionSystem;
    our::LightSystem lightSystem;
    our::TimeSystem timeSystem;
    our::EnemySystem enemySystem;
    our::NPCMovementSystem npcMovementSystem;
    BlockInteractionSystem blockInteraction;

    our::Entity* highlightEntity = nullptr;      // Semi-transparent fill
    our::Entity* highlightEdgesEntity = nullptr; // Clean square borders
    our::Mesh* highlightEdgesMesh = nullptr;     // Line-based mesh for borders
    our::Entity* portalEntity = nullptr;         // Win-condition portal
    
    // First-person Steve rig
    our::Entity* firstPersonRightArm = nullptr;
    our::Entity* firstPersonLeftArm = nullptr;
    float firstPersonAnimTime = 0.0f;
    float firstPersonAttackTimer = 0.0f;
    
    // Combat mechanics
    float meleeCooldown = 0.0f;
    const float meleeCooldownDuration = 0.24f;
    const float meleeRange = 3.0f;
    const float meleeDamage = 8.0f;

    // Hand Animation & UI State
    float handAnimTime = 0.0f;
    float hitAnimTime = 0.0f;
    bool isHitting = false;
    bool isInventoryOpen = false;

    // Chunk Streaming State
    int chunkLoadRadius = 2;
    int chunkUnloadRadius = 3;
    int currentCenterChunkX = std::numeric_limits<int>::min();
    int currentCenterChunkZ = std::numeric_limits<int>::min();
    bool terrainMeshDirty = true;
    std::unordered_map<std::string, ChunkRenderGroup> chunkRenderGroups;
    std::vector<NPCSpawnData> npcTemplates;

    // --- Inventory, Hotbar & Level Helpers ---
    static constexpr int kHotbarSlots = 5;
    float levelUpNotificationTime = 0.0f;
    int displayedLevel = 1;

    static int hotbarBlockType(int slot) {
        static const int types[kHotbarSlots] = {
            voxel::GRASS, voxel::DIRT, voxel::WOOD, voxel::STONE, voxel::SAND};
        return types[std::clamp(slot, 0, kHotbarSlots - 1)];
    }

    static int* inventoryCountForType(our::PlayerComponent* player, int blockType) {
        switch (blockType) {
            case voxel::GRASS: return &player->inventoryGrass;
            case voxel::DIRT:  return &player->inventoryDirt;
            case voxel::WOOD:  return &player->inventoryWood;
            case voxel::STONE: return &player->inventoryStone;
            case voxel::SAND:  return &player->inventorySand;
            default:           return nullptr;
        }
    }

    void registerCollectedBlock(our::PlayerComponent *player, int blockType) {
        // Level 3: Collecting Diamond fills XP to complete leveling
        if (player && player->level == 3 && blockType == voxel::Diamond) {
            player->currentXP = 1.0f;
            player->level = 4;
            levelUpNotificationTime = 3.0f;
            displayedLevel = 4;
            our::AudioSystem::playSound("assets/sounds/levelup.wav");
        }

        int *slot = inventoryCountForType(player, blockType);
        if (slot) {
            (*slot)++;
            player->resourcesCollected++;
            if (player->resourcesCollected >= player->resourcesRequired) {
                player->gameState = our::GameState::WIN;
            }
        }
    }

    // --- Utility & Search ---
    static int worldToChunkCoordinate(float worldCoord) {
        return static_cast<int>(std::floor(worldCoord / static_cast<float>(voxel::Chunk::CHUNK_SIZE)));
    }

    our::Entity* findPlayerEntity() {
        for (auto entity : engineWorld.getEntities()) {
            auto* camera = entity->getComponent<our::CameraComponent>();
            auto* player = entity->getComponent<our::PlayerComponent>();
            if (camera && player) return entity;
        }
        return nullptr;
    }

    static bool rayIntersectsAABB(const glm::vec3& origin, const glm::vec3& dir,
                                  const glm::vec3& boxMin, const glm::vec3& boxMax,
                                  float maxDistance, float& outT) {
        float tMin = 0.0f;
        float tMax = maxDistance;

        for (int axis = 0; axis < 3; ++axis) {
            float o = origin[axis];
            float d = dir[axis];
            float mn = boxMin[axis];
            float mx = boxMax[axis];

            if (std::abs(d) < 1e-6f) {
                if (o < mn || o > mx) return false;
                continue;
            }

            float invD = 1.0f / d;
            float t1 = (mn - o) * invD;
            float t2 = (mx - o) * invD;
            if (t1 > t2) std::swap(t1, t2);

            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMax < tMin) return false;
        }

        outT = tMin;
        return tMin <= maxDistance;
    }

    our::Entity* findEnemyInCrosshair(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                                      float maxDistance, float& hitDistance) {
        our::Entity* nearest = nullptr;
        hitDistance = maxDistance;

        for (auto entity : engineWorld.getEntities()) {
            auto* enemy = entity->getComponent<our::EnemyComponent>();
            if (!enemy || enemy->state == our::EnemyState::DEAD || enemy->health <= 0.0f) continue;

            glm::vec3 basePos = entity->localTransform.position;
            glm::vec3 boxMin = basePos + enemy->colliderCenter - enemy->colliderHalfSize;
            glm::vec3 boxMax = basePos + enemy->colliderCenter + enemy->colliderHalfSize;

            float t = 0.0f;
            if (rayIntersectsAABB(rayOrigin, rayDir, boxMin, boxMax, maxDistance, t) && t < hitDistance) {
                nearest = entity;
                hitDistance = t;
            }
        }

        return nearest;
    }

    bool tryMeleeAttack(const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
        if (meleeCooldown > 0.0f) return false;

        float hitDistance = meleeRange;
        our::Entity* enemyEntity = findEnemyInCrosshair(rayOrigin, glm::normalize(rayDir), meleeRange, hitDistance);
        if (!enemyEntity) return false;

        auto* enemy = enemyEntity->getComponent<our::EnemyComponent>();
        if (!enemy) return false;

        enemy->health -= meleeDamage;
        enemy->hurtFlashTimer = enemy->hurtFlashDuration;
        enemy->state = our::EnemyState::CHASE;

        glm::vec3 knockDir = glm::normalize(glm::vec3(rayDir.x, 0.0f, rayDir.z));
        if (glm::length(knockDir) > 0.0001f) {
            enemy->velocity.x += knockDir.x * 3.0f;
            enemy->velocity.z += knockDir.z * 3.0f;
        }

        firstPersonAttackTimer = 0.22f;
        meleeCooldown = meleeCooldownDuration;
        our::AudioSystem::playSound("assets/sounds/Hit.wav");
        return true;
    }

    void killNPCAndAwardMeat(our::Entity *npcEntity, our::PlayerComponent *player) {
        if (!npcEntity || !player) return;
        auto *killable = npcEntity->getComponent<our::KillableNPCComponent>();
        if (killable) {
            if (killable->npcType == "chest") {
                // Increase health for chests, capped at max health
                player->health = std::min(player->health + killable->foodReward, player->maxHealth);
            } else {
                // Original behavior for other NPCs (increase meat count)
                player->meatCount += killable->foodReward;

                // Level 2: Add 1/4 XP per enemy killed
                if (player->level == 2) {
                    player->currentXP += 1.0f / 4.0f;
                    if (player->currentXP >= 1.0f) {
                        player->level = 3;
                        player->currentXP = 0.0f;
                        levelUpNotificationTime = 3.0f;
                        displayedLevel = 3;
                        our::AudioSystem::playSound("assets/sounds/levelup.wav");
                    }
                }
            }
        }
        engineWorld.markForRemoval(npcEntity);
    }

    our::Entity *findHitNPC(const glm::vec3 &camPos, const glm::vec3 &camDir, float maxDist) {
        float closestDist = maxDist;
        our::Entity *closestNPC = nullptr;

        for (auto entity : engineWorld.getEntities()) {
            if (!entity) continue;
            auto *killable = entity->getComponent<our::KillableNPCComponent>();
            if (!killable) continue;

            glm::vec3 npcPos = entity->localTransform.position;
            glm::vec3 toNPC = npcPos - camPos;
            float t = glm::dot(toNPC, camDir);
            if (t < 0.0f) continue;

            glm::vec3 closestPoint = camPos + camDir * t;
            glm::vec3 diff = closestPoint - npcPos;
            float distSq = glm::dot(diff, diff);
            float radius = 0.5f;

            if (distSq < radius * radius && t < closestDist) {
                closestDist = t;
                closestNPC = entity;
            }
        }
        return closestNPC;
    }

    void setupFirstPersonRig(our::Entity* playerEntity) {
        if (!playerEntity) return;

        auto* cube = our::AssetLoader<our::Mesh>::get("cube");
        auto* skin = our::AssetLoader<our::Material>::get("steve-skin");

        firstPersonRightArm = engineWorld.add();
        firstPersonRightArm->name = "fp-right-arm";
        firstPersonRightArm->parent = playerEntity;
        firstPersonRightArm->localTransform.position = glm::vec3(0.27f, -0.24f, -0.38f);
        firstPersonRightArm->localTransform.rotation = glm::vec3(-0.55f, 0.20f, 0.05f);
        firstPersonRightArm->localTransform.scale = glm::vec3(0.09f, 0.28f, 0.09f);
        auto* rightRenderer = firstPersonRightArm->addComponent<our::MeshRendererComponent>();
        rightRenderer->mesh = cube;
        rightRenderer->material = skin ? skin : our::AssetLoader<our::Material>::get("default");

        firstPersonLeftArm = engineWorld.add();
        firstPersonLeftArm->name = "fp-left-arm";
        firstPersonLeftArm->parent = playerEntity;
        firstPersonLeftArm->localTransform.position = glm::vec3(-0.27f, -0.25f, -0.40f);
        firstPersonLeftArm->localTransform.rotation = glm::vec3(-0.58f, -0.18f, -0.04f);
        firstPersonLeftArm->localTransform.scale = glm::vec3(0.09f, 0.26f, 0.09f);
        auto* leftRenderer = firstPersonLeftArm->addComponent<our::MeshRendererComponent>();
        leftRenderer->mesh = cube;
        leftRenderer->material = skin ? skin : our::AssetLoader<our::Material>::get("default");
    }

    void updateFirstPersonRig(our::Entity* playerEntity, our::PlayerComponent* player, float dt) {
        if (!playerEntity || !player || !firstPersonRightArm || !firstPersonLeftArm) return;

        firstPersonAnimTime += dt;
        float speed = glm::length(glm::vec2(player->velocity.x, player->velocity.z));
        float walkFactor = glm::clamp(speed / 4.5f, 0.0f, 1.0f);
        float bob = std::sin(firstPersonAnimTime * 8.0f) * 0.012f * walkFactor;

        float punch = 0.0f;
        if (firstPersonAttackTimer > 0.0f) {
            float ratio = 1.0f - (firstPersonAttackTimer / 0.22f);
            punch = std::sin(ratio * 3.14159f);
            firstPersonAttackTimer -= dt;
            if (firstPersonAttackTimer < 0.0f) firstPersonAttackTimer = 0.0f;
        }

        firstPersonRightArm->localTransform.position = glm::vec3(0.27f, -0.24f + bob - punch * 0.09f, -0.38f + punch * 0.05f);
        firstPersonRightArm->localTransform.rotation = glm::vec3(-0.55f - punch * 1.25f, 0.20f + punch * 0.18f, 0.05f + punch * 0.15f);

        firstPersonLeftArm->localTransform.position = glm::vec3(-0.27f, -0.25f + bob * 0.8f, -0.40f + punch * 0.01f);
        firstPersonLeftArm->localTransform.rotation = glm::vec3(-0.58f - punch * 0.35f, -0.18f, -0.04f - punch * 0.06f);
    }

    our::Material* getMaterialForBlockType(int blockType) {
        switch (blockType) {
            case voxel::STONE: return our::AssetLoader<our::Material>::get("stone");
            case voxel::GRASS: return nullptr; // Grass is handled manually per face
            case voxel::DIRT:  return our::AssetLoader<our::Material>::get("dirt");
            case voxel::SAND:  return our::AssetLoader<our::Material>::get("sand");
            case voxel::WATER: return our::AssetLoader<our::Material>::get("water");
            case voxel::WOOD:  return our::AssetLoader<our::Material>::get("wood");
            default:           return our::AssetLoader<our::Material>::get("default");
        }
    }

    // --- Chunk Rendering & NPCs ---
    void clearChunkRenderGroup(const std::string& chunkKey) {
        auto it = chunkRenderGroups.find(chunkKey);
        if (it == chunkRenderGroups.end())
            return;
        for (auto *entity : it->second.entities)
            engineWorld.markForRemoval(entity);
        for (auto *mesh : it->second.meshes)
            delete mesh;
        // NPCs are tracked but not stored as entities in renderGroup, they use engineWorld directly
        chunkRenderGroups.erase(it);
    }

    void clearAllChunkRenderGroups() {
        for (auto& entry : chunkRenderGroups) {
            for (auto* entity : entry.second.entities) engineWorld.markForRemoval(entity);
            for (auto* mesh : entry.second.meshes) delete mesh;
        }
        chunkRenderGroups.clear();
    }

    void spawnNPCsInChunk(int cx, int cz) {
        std::string chunkKey = std::to_string(cx) + "_" + std::to_string(cz);

        // Already spawned NPCs in this chunk
        if (chunkRenderGroups.find(chunkKey) != chunkRenderGroups.end() &&
            !chunkRenderGroups[chunkKey].npcs.empty())
            return;

        if (npcTemplates.empty()) return;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> npcCountDist(0, 1);
        std::uniform_int_distribution<> templateDist(0, static_cast<int>(npcTemplates.size()) - 1);

        int numNPCs = npcCountDist(gen);

        for (int i = 0; i < numNPCs; ++i) {
            std::uniform_int_distribution<> localDist(0, voxel::Chunk::CHUNK_SIZE - 1);
            int lx = localDist(gen);
            int ly = terrainWorld.height - 1;
            int lz = localDist(gen);

            while (ly >= 0 && terrainWorld.getBlock(cx * voxel::Chunk::CHUNK_SIZE + lx, ly, cz * voxel::Chunk::CHUNK_SIZE + lz) == 0) {
                ly--;
            }
            if (ly < 0) continue;

            // Check if water is nearby (within 3 blocks)
            bool nearWater = false;
            int checkX = cx * voxel::Chunk::CHUNK_SIZE + lx;
            int checkZ = cz * voxel::Chunk::CHUNK_SIZE + lz;
            for (int dx = -3; dx <= 3 && !nearWater; dx++) {
                for (int dz = -3; dz <= 3 && !nearWater; dz++) {
                    for (int dy = -2; dy <= 2; dy++) {
                        if (terrainWorld.getBlock(checkX + dx, ly + dy, checkZ + dz) == voxel::WATER) {
                            nearWater = true;
                            break;
                        }
                    }
                }
            }
            if (nearWater) continue;

            float worldX = cx * voxel::Chunk::CHUNK_SIZE + lx + 0.5f;
            float worldZ = cz * voxel::Chunk::CHUNK_SIZE + lz + 0.5f;

            const auto &templateData = npcTemplates[templateDist(gen)];

            float blockTopY = ly + 1.0f;
            float colliderBottomLocal = templateData.aabbCenter.y - templateData.aabbHalfSize.y;
            float desiredEntityY = blockTopY - colliderBottomLocal + 0.3f;

            our::Entity *npcEntity = engineWorld.add();
            npcEntity->localTransform.position = glm::vec3(worldX, desiredEntityY, worldZ);
            npcEntity->localTransform.scale = glm::vec3(templateData.scaleX, templateData.scaleY, templateData.scaleZ);

            auto *meshRenderer = npcEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = our::AssetLoader<our::Mesh>::get(templateData.meshName);
            meshRenderer->material = our::AssetLoader<our::Material>::get(templateData.matName);

            auto *aabb = npcEntity->addComponent<our::AABBColliderComponent>();
            aabb->center = templateData.aabbCenter;
            aabb->halfSize = templateData.aabbHalfSize;

            auto *movement = npcEntity->addComponent<our::NPCMovementComponent>();
            movement->startPosition = npcEntity->localTransform.position;
            movement->speed = templateData.speed;
            movement->moveRadius = templateData.moveRadius;
            movement->waitTime = templateData.waitTime;

            if (templateData.movementType == "idle")
                movement->movementType = our::NPCMovementComponent::MovementType::IDLE;
            else if (templateData.movementType == "patrol")
                movement->movementType = our::NPCMovementComponent::MovementType::PATROL;
            else if (templateData.movementType == "follow_player")
                movement->movementType = our::NPCMovementComponent::MovementType::FOLLOW_PLAYER;
            else
                movement->movementType = our::NPCMovementComponent::MovementType::RANDOM_WALK;

            auto *killable = npcEntity->addComponent<our::KillableNPCComponent>();
            killable->foodReward = templateData.foodReward;
            killable->npcType = templateData.npcType;

            // Store NPC spawn data in chunk render group
            NPCSpawnData spawnData = templateData;
            spawnData.chunkX = cx; spawnData.chunkZ = cz;
            spawnData.localX = lx; spawnData.localY = ly; spawnData.localZ = lz;
            chunkRenderGroups[chunkKey].npcs.push_back(spawnData);
        }
    }

    void spawnNPCs() {
        if (npcTemplates.empty()) return;
        our::Entity *playerEntity = findPlayerEntity();
        if (!playerEntity) return;

        glm::vec3 playerPos = playerEntity->localTransform.position;
        int cx = (int)std::floor(playerPos.x / (float)voxel::Chunk::CHUNK_SIZE);
        int cz = (int)std::floor(playerPos.z / (float)voxel::Chunk::CHUNK_SIZE);
        spawnNPCsInChunk(cx, cz);
    }

    void buildChunkRenderGroup(const std::string& chunkKey, const voxel::Chunk& chunk) {
        ChunkRenderGroup renderGroup;
        const int meshBlockTypes[] = {
            voxel::STONE, voxel::DIRT, voxel::SAND, 
            voxel::WATER, voxel::WOOD, voxel::LEAF, voxel::Diamond, voxel::Glass
        };

        glm::vec3 chunkOrigin(chunk.chunkX * voxel::Chunk::CHUNK_SIZE, 0.0f, chunk.chunkZ * voxel::Chunk::CHUNK_SIZE);

        for (int blockType : meshBlockTypes) {
            our::Material* material = getMaterialForBlockType(blockType);
            if (!material) continue;

            our::Mesh* chunkMesh = our::mesh_utils::buildChunkMesh(chunk, terrainWorld, blockType);
            if (!chunkMesh) continue;

            our::Entity* chunkEntity = engineWorld.add();
            chunkEntity->localTransform.position = chunkOrigin;

            auto* meshRenderer = chunkEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = chunkMesh;
            meshRenderer->material = material;

            renderGroup.entities.push_back(chunkEntity);
            renderGroup.meshes.push_back(chunkMesh);
        }

        // Handle GRASS blocks manually to support different textures per face
        auto addGrassFaces = [&](our::mesh_utils::FaceCategory faceCat, const char* matName) {
            our::Material* material = our::AssetLoader<our::Material>::get(matName);
            if (!material) return;
            our::Mesh* chunkMesh = our::mesh_utils::buildChunkMesh(chunk, terrainWorld, voxel::GRASS, faceCat);
            if (!chunkMesh) return;

            our::Entity* chunkEntity = engineWorld.add();
            chunkEntity->localTransform.position = chunkOrigin;

            auto* meshRenderer = chunkEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = chunkMesh;
            meshRenderer->material = material;

            renderGroup.entities.push_back(chunkEntity);
            renderGroup.meshes.push_back(chunkMesh);
        };

        addGrassFaces(our::mesh_utils::FaceCategory::TOP, "grass-top");
        addGrassFaces(our::mesh_utils::FaceCategory::BOTTOM, "dirt");
        addGrassFaces(our::mesh_utils::FaceCategory::SIDES, "grass-side");

        spawnNPCsInChunk(chunk.chunkX, chunk.chunkZ);
        chunkRenderGroups[chunkKey] = std::move(renderGroup);
    }

    void rebuildMesh() {
        int chunksUpdatedThisFrame = 0;
        bool stillHasDirtyChunks = false;

        for (auto& entry : terrainWorld.activeChunks) {
            voxel::Chunk& chunk = entry.second;
            bool hasGroup = chunkRenderGroups.count(entry.first);
            if (!chunk.isDirty && hasGroup) continue;

            if (chunksUpdatedThisFrame == 0) {
                clearChunkRenderGroup(entry.first);
                buildChunkRenderGroup(entry.first, chunk);
                chunk.isDirty = false;
                chunksUpdatedThisFrame++;
            } else {
                stillHasDirtyChunks = true;
                break;
            }
        }
        engineWorld.deleteMarkedEntities();
        terrainMeshDirty = stillHasDirtyChunks;
    }

    // --- Streaming Logic ---
    void updateLoadedChunks(int centerChunkX, int centerChunkZ) {
        bool chunkSetChanged = false;
        for (int dz = -chunkLoadRadius; dz <= chunkLoadRadius; ++dz) {
            for (int dx = -chunkLoadRadius; dx <= chunkLoadRadius; ++dx) {
                int cx = centerChunkX + dx, cz = centerChunkZ + dz;
                std::string key = std::to_string(cx) + "_" + std::to_string(cz);
                if (terrainWorld.activeChunks.find(key) == terrainWorld.activeChunks.end()) {
                    terrainWorld.generateChunk(cx, cz);
                    chunkSetChanged = true;
                    spawnNPCsInChunk(cx, cz);
                }
            }
        }

        for (auto it = terrainWorld.activeChunks.begin(); it != terrainWorld.activeChunks.end();) {
            if (std::abs(it->second.chunkX - centerChunkX) > chunkUnloadRadius ||
                std::abs(it->second.chunkZ - centerChunkZ) > chunkUnloadRadius) {
                clearChunkRenderGroup(it->first);
                it = terrainWorld.activeChunks.erase(it);
                chunkSetChanged = true;
            } else ++it;
        }
        if (chunkSetChanged) terrainMeshDirty = true;
    }

    void streamChunksAroundPlayer(const glm::vec3& position) {
        int px = worldToChunkCoordinate(position.x);
        int pz = worldToChunkCoordinate(position.z);
        if (px != currentCenterChunkX || pz != currentCenterChunkZ) {
            currentCenterChunkX = px; currentCenterChunkZ = pz;
            updateLoadedChunks(px, pz);
        }
    }

    // --- UI Drawing Helpers ---
    static void drawHotbarResourceIcon(ImDrawList* dl, int hotbarSlot, const ImVec2& iconMin, const ImVec2& iconMax) {
        const ImU32 outline = IM_COL32(18, 18, 22, 220);
        our::Texture2D* tex = nullptr;
        ImU32 fallbackColor = IM_COL32(60, 60, 65, 255);

        if(hotbarSlot == 0) { tex = our::AssetLoader<our::Texture2D>::get("grass-side"); fallbackColor = IM_COL32(72, 130, 58, 255); }
        else if(hotbarSlot == 1) { tex = our::AssetLoader<our::Texture2D>::get("dirt"); fallbackColor = IM_COL32(115, 77, 51, 255); }
        else if(hotbarSlot == 2) { tex = our::AssetLoader<our::Texture2D>::get("wood"); fallbackColor = IM_COL32(130, 85, 48, 255); }
        else if(hotbarSlot == 3) { tex = our::AssetLoader<our::Texture2D>::get("stone"); fallbackColor = IM_COL32(118, 118, 118, 255); }
        else if(hotbarSlot == 4) { tex = our::AssetLoader<our::Texture2D>::get("sand"); fallbackColor = IM_COL32(204, 190, 72, 255); }

        if (tex) dl->AddImage((ImTextureID)(intptr_t)tex->getOpenGLName(), iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0));  // to fix the inverted texture
        else dl->AddRectFilled(iconMin, iconMax, fallbackColor, 4.0f);
        dl->AddRect(iconMin, iconMax, outline, 4.0f, ImDrawCornerFlags_All, 1.25f);
    }

    // --- State Overrides ---
    void onInitialize() override {
        auto &config = getApp()->getConfig()["scene"];
        if (config.contains("assets")) our::deserializeAllAssets(config["assets"]);
        if (config.contains("world")) engineWorld.deserialize(config["world"]);
        if (config.contains("terrain")) {
            auto& terrainConfig = config["terrain"];
            terrainWorld.deserialize(terrainConfig);
            chunkLoadRadius = terrainConfig.value("chunk-load-radius", chunkLoadRadius);
            chunkUnloadRadius = std::max(terrainConfig.value("chunk-unload-radius", chunkLoadRadius + 1), chunkLoadRadius);
        }

        if (config.contains("npcTypes") && config["npcTypes"].is_array()) {
            for (const auto &npcTypeJson : config["npcTypes"]) {
                if (!npcTypeJson.is_object()) continue;
                NPCSpawnData templateData;
                templateData.meshName = npcTypeJson.value("mesh", "cube");
                templateData.matName = npcTypeJson.value("material", "default");

                if (npcTypeJson.contains("scale") && npcTypeJson["scale"].is_array() && npcTypeJson["scale"].size() >= 3) {
                    templateData.scaleX = npcTypeJson["scale"][0].get<float>();
                    templateData.scaleY = npcTypeJson["scale"][1].get<float>();
                    templateData.scaleZ = npcTypeJson["scale"][2].get<float>();
                } else {
                    templateData.scaleX = templateData.scaleY = templateData.scaleZ = 0.5f;
                }

                if (npcTypeJson.contains("components") && npcTypeJson["components"].is_object()) {
                    const auto &comp = npcTypeJson["components"];
                    templateData.foodReward = comp.value("foodReward", 1);
                    templateData.npcType = comp.value("npcType", "default");

                    if (comp.contains("AABBCollider") && comp["AABBCollider"].is_object()) {
                        const auto &aabbJson = comp["AABBCollider"];
                        templateData.aabbCenter = glm::vec3(0.0f);
                        templateData.aabbHalfSize = glm::vec3(0.2f);
                        if (aabbJson.contains("center") && aabbJson["center"].is_array() && aabbJson["center"].size() >= 3) {
                            templateData.aabbCenter = glm::vec3(aabbJson["center"][0].get<float>(), aabbJson["center"][1].get<float>(), aabbJson["center"][2].get<float>());
                        }
                        if (aabbJson.contains("halfSize") && aabbJson["halfSize"].is_array() && aabbJson["halfSize"].size() >= 3) {
                            templateData.aabbHalfSize = glm::vec3(aabbJson["halfSize"][0].get<float>(), aabbJson["halfSize"][1].get<float>(), aabbJson["halfSize"][2].get<float>());
                        }
                    }
                } else {
                    templateData.foodReward = 1;
                    templateData.npcType = "default";
                }

                if (npcTypeJson.contains("movement") && npcTypeJson["movement"].is_object()) {
                    const auto &mov = npcTypeJson["movement"];
                    templateData.speed = mov.value("speed", 1.0f);
                    templateData.moveRadius = mov.value("moveRadius", 5.0f);
                    templateData.waitTime = mov.value("waitTime", 2.0f);
                    templateData.movementType = mov.value("movementType", "random_walk");
                } else {
                    templateData.speed = 1.0f;
                    templateData.moveRadius = 5.0f;
                    templateData.waitTime = 2.0f;
                    templateData.movementType = "random_walk";
                }
                npcTemplates.push_back(templateData);
            }
        }

        cameraController.enter(getApp());
        playerController.enter(getApp());
        timeSystem.initialize(&engineWorld);
        renderer.initialize(getApp()->getFrameBufferSize(), config["renderer"]);

        highlightEntity = engineWorld.add();
        auto* meshRenderer = highlightEntity->addComponent<our::MeshRendererComponent>();
        meshRenderer->mesh = our::AssetLoader<our::Mesh>::get("cube");
        meshRenderer->material = our::AssetLoader<our::Material>::get("highlight-fill");
        highlightEntity->localTransform.scale = glm::vec3(0.502f);

        highlightEdgesEntity = engineWorld.add();
        auto* edgesRenderer = highlightEdgesEntity->addComponent<our::MeshRendererComponent>();
        highlightEdgesMesh = our::mesh_utils::cubeEdges();
        edgesRenderer->mesh = highlightEdgesMesh;
        edgesRenderer->material = our::AssetLoader<our::Material>::get("wireframe");
        highlightEdgesEntity->localTransform.scale = glm::vec3(0.505f);

        blockInteraction.initialize(&engineWorld);
        enemySystem.initialize();
        our::AudioSystem::startLoopingSound("water_ambient", "assets/sounds/water_flowing.wav");
        our::AudioSystem::setLoopingSoundVolume("water_ambient", 0.0f);

        our::Entity* playerEntity = findPlayerEntity();
        if (playerEntity) {
            streamChunksAroundPlayer(playerEntity->localTransform.position);
            auto& pos = playerEntity->localTransform.position;
            for (int y = terrainWorld.height - 1; y >= 0; --y) {
                if (terrainWorld.getBlock(pos.x, y, pos.z) != 0) {
                    pos.y = y + 2.5f; break;
                }
            }
            setupFirstPersonRig(playerEntity);
        }

        // Spawn portal entity for win condition
        {
            glm::vec3 portalPos(30.0f, 1.0f, 30.0f);
            for (int y = terrainWorld.height - 1; y >= 0; --y) {
                if (terrainWorld.getBlock(30, y, 30) != 0 && terrainWorld.getBlock(30, y, 30) != voxel::WATER) {
                    portalPos.y = (float)(y + 1) + 0.5f;
                    break;
                }
            }
            portalEntity = engineWorld.add();
            portalEntity->name = "portal";
            portalEntity->localTransform.position = portalPos;
            portalEntity->localTransform.scale = glm::vec3(1.0f);
            auto* portalMR = portalEntity->addComponent<our::MeshRendererComponent>();
            portalMR->mesh = our::AssetLoader<our::Mesh>::get("cube");
            portalMR->material = our::AssetLoader<our::Material>::get("sun-material");
        }

        if (terrainMeshDirty) rebuildMesh();

        // Initialize NPC Movement System
        npcMovementSystem.initialize(playerEntity, &terrainWorld);
        spawnNPCs();
        for (auto entity : engineWorld.getEntities()) {
            if (!entity) continue;
            auto *npcMove = entity->getComponent<our::NPCMovementComponent>();
            if (npcMove) npcMove->startPosition = entity->localTransform.position;
        }
    }

    void onImmediateGui() override {
        our::Entity *playerEntity = findPlayerEntity();
        if (!playerEntity) return;

        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        our::PlayerComponent* player = playerEntity->getComponent<our::PlayerComponent>();

        // Draw Inventory Window Overlay
        if (isInventoryOpen) {
            ImVec2 windowSize(400, 360);
            ImGui::SetNextWindowPos(ImVec2((displaySize.x - windowSize.x) * 0.5f, (displaySize.y - windowSize.y) * 0.5f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.76f, 0.76f, 0.76f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

            ImGui::Begin("Inventory", &isInventoryOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

            // Crafting Section
            ImGui::SetCursorPos(ImVec2(180, 20));
            ImGui::BeginGroup();
            ImGui::Text("Crafting");
            for (int r = 0; r < 2; r++) {
                for (int c = 0; c < 2; c++) {
                    ImGui::Button(("##craft" + std::to_string(r) + "_" + std::to_string(c)).c_str(), ImVec2(36, 36));
                    if (c < 1) ImGui::SameLine();
                }
            }
            ImGui::EndGroup();

            ImGui::SameLine(0, 15);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25);
            ImGui::Text("->");
            ImGui::SameLine(0, 15);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);
            ImGui::Button("##craft_out", ImVec2(45, 45));

            ImGui::Spacing(); ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Main Inventory Section
            ImGui::SetCursorPosX(16);
            ImGui::Text("Inventory");

            ImGui::SetCursorPosX(16);
            for (int r = 0; r < 3; r++) {
                for (int c = 0; c < 9; c++) {
                    ImGui::Button(("##inv" + std::to_string(r) + "_" + std::to_string(c)).c_str(), ImVec2(36, 36));
                    if (c < 8) ImGui::SameLine();
                }
                if (r < 2) ImGui::SetCursorPosX(16);
            }

            ImGui::Spacing(); ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Hotbar Section (in inventory)
            ImGui::SetCursorPosX(16);
            for (int c = 0; c < 9; c++) {
                ImGui::Button(("##hotbar_inv" + std::to_string(c)).c_str(), ImVec2(36, 36));
                if (c < 8) ImGui::SameLine();
            }

            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        } else {
            // Gameplay HUD (Only draw when inventory is closed)
            ImDrawList *drawList = ImGui::GetForegroundDrawList();

            // 1. Draw Hotbar
            if (player) {
                ImGuiWindowFlags invFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;
                constexpr float box = 60.0f, gap = 8.0f, iconSize = 34.0f;
                ImVec2 invSize(kHotbarSlots * box + (kHotbarSlots - 1) * gap + 24.0f, box + 26.0f);
                ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f - invSize.x * 0.5f, displaySize.y - invSize.y - 16.0f), ImGuiCond_Always);
                ImGui::SetNextWindowSize(invSize, ImGuiCond_Always);
                if (ImGui::Begin("InventoryHotbar", nullptr, invFlags)) {
                    ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
                    int counts[] = {player->inventoryGrass, player->inventoryDirt, player->inventoryWood, player->inventoryStone, player->inventorySand};
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    for (int i = 0; i < kHotbarSlots; i++) {
                        if (i > 0) ImGui::SameLine(0.0f, gap);
                        ImGui::PushID(i);
                        const bool selected = (player->inventoryHotbarSlot == i);
                        ImVec2 p = ImGui::GetCursorScreenPos();
                        ImVec2 br(p.x + box, p.y + box);
                        dl->AddRectFilled(p, br, selected ? IM_COL32(55, 85, 130, 230) : IM_COL32(28, 28, 32, 220), 6.0f);
                        drawHotbarResourceIcon(dl, i, ImVec2(p.x + (box-iconSize)*0.5f, p.y + 5.0f), ImVec2(p.x + (box+iconSize)*0.5f, p.y + 5.0f + iconSize));
                        dl->AddRect(p, br, selected ? IM_COL32(240, 200, 90, 255) : IM_COL32(90, 90, 98, 255), 6.0f, ImDrawCornerFlags_All, selected ? 2.5f : 1.0f);
                        if (ImGui::InvisibleButton("slot", ImVec2(box, box))) player->inventoryHotbarSlot = i;
                        char cnt[12]; std::snprintf(cnt, sizeof(cnt), "x%d", counts[i]);
                        ImVec2 ts = ImGui::CalcTextSize(cnt);
                        dl->AddText(ImVec2(p.x + (box - ts.x) * 0.5f, p.y + box - ts.y - 4.0f), IM_COL32_WHITE, cnt);
                        ImGui::PopID();
                    }
                }
                ImGui::End();
            }

            // 2. Draw Health
            our::Texture2D* heartsTex = our::AssetLoader<our::Texture2D>::get("hearts");
            if (heartsTex && player) {
                float heartSize = 28.0f, heartGap = 2.0f, maxHearts = 10;
                float totalW = maxHearts * heartSize + (maxHearts - 1) * heartGap;
                float startX = displaySize.x * 0.5f - totalW * 0.5f;
                float invH = 60.0f + 26.0f;
                float startY = displaySize.y - invH - 16.0f - heartSize - 12.0f;
                ImTextureID texID = (ImTextureID)(intptr_t)heartsTex->getOpenGLName();
                float texW = 45.0f;
                int fullHearts = static_cast<int>(player->health / 10.0f);
                float partialHeart = (player->health / 10.0f) - fullHearts;

                ImVec2 uvFull0((27.0f + 0.5f) / texW, 1.0f), uvFull1((36.0f - 0.5f) / texW, 0.0f);
                ImVec2 uvEmpty0((0.0f + 0.5f) / texW, 1.0f), uvEmpty1((9.0f - 0.5f) / texW, 0.0f);

                for (int i = 0; i < maxHearts; ++i) {
                    ImVec2 pMin(startX + i * (heartSize + heartGap), startY);
                    ImVec2 pMax(pMin.x + heartSize, pMin.y + heartSize);
                    if (i < fullHearts) {
                        drawList->AddImage(texID, pMin, pMax, uvFull0, uvFull1);
                    } else if (i == fullHearts && partialHeart > 0.0f) {
                        ImVec2 splitX(pMin.x + heartSize * partialHeart, pMin.y);
                        ImVec2 splitX1(pMin.x + heartSize * partialHeart, pMax.y);
                        drawList->AddImage(texID, pMin, splitX, uvFull0, uvFull1);
                        drawList->AddImage(texID, splitX1, pMax, uvEmpty0, uvEmpty1);
                    } else {
                        drawList->AddImage(texID, pMin, pMax, uvEmpty0, uvEmpty1);
                    }
                }
            }

            // 3. Draw Crosshair
            ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
            drawList->AddLine(ImVec2(center.x - 8, center.y), ImVec2(center.x + 8, center.y), IM_COL32(255, 255, 255, 220), 2.0f);
            drawList->AddLine(ImVec2(center.x, center.y - 8), ImVec2(center.x, center.y + 8), IM_COL32(255, 255, 255, 220), 2.0f);

            // 4. Objective Progress
            if (player) {
                float invH = 60.0f + 26.0f, heartH = 28.0f;
                float objY = displaySize.y - invH - 16.0f - heartH - 12.0f - 26.0f;
                char objBuf[128];
                int days = timeSystem.getDaysPassed();
                if (player->currentLevel == 1) {
                    std::snprintf(objBuf, sizeof(objBuf), "Level 1 | Enemies: %d / 2 | Days: %d / 3", player->enemiesKilled, days);
                } else if (player->currentLevel == 2) {
                    std::snprintf(objBuf, sizeof(objBuf), "Level 2 | Enemies: %d / 5 | Days: %d / 5", player->enemiesKilled, days);
                } else {
                    std::snprintf(objBuf, sizeof(objBuf), "Level %d | Free Play", player->currentLevel);
                }

                ImVec2 objSize = ImGui::CalcTextSize(objBuf);
                float objX = displaySize.x * 0.5f - objSize.x * 0.5f;
                drawList->AddText(ImVec2(objX + 1, objY + 1), IM_COL32(0,0,0,180), objBuf);
                drawList->AddText(ImVec2(objX, objY), IM_COL32(255, 220, 80, 255), objBuf);

                if (player->foodCount > 0) {
                    char foodBuf[32];
                    std::snprintf(foodBuf, sizeof(foodBuf), "Food: %d (F to heal)", player->foodCount);
                    ImVec2 foodSize = ImGui::CalcTextSize(foodBuf);
                    float foodX = displaySize.x * 0.5f - foodSize.x * 0.5f;
                    drawList->AddText(ImVec2(foodX + 1, objY - 22.0f + 1), IM_COL32(0,0,0,180), foodBuf);
                    drawList->AddText(ImVec2(foodX, objY - 22.0f), IM_COL32(120, 255, 120, 255), foodBuf);
                }
            }

            // 5. XP Bar
            if (player) {
                float xpBarWidth = 200.0f, xpBarHeight = 16.0f;
                float xpBarX = displaySize.x - xpBarWidth - 16.0f, xpBarY = 16.0f;
                float xpProgress = std::min(player->currentXP, 1.0f);

                ImU32 bgColor = IM_COL32(30, 30, 35, 220);
                ImU32 fillColor = (player->level == 3) ? IM_COL32(0, 200, 150, 255) : IM_COL32(100, 200, 255, 255);

                drawList->AddRectFilled(ImVec2(xpBarX, xpBarY), ImVec2(xpBarX + xpBarWidth, xpBarY + xpBarHeight), bgColor, 4.0f);
                drawList->AddRectFilled(ImVec2(xpBarX, xpBarY), ImVec2(xpBarX + xpBarWidth * xpProgress, xpBarY + xpBarHeight), fillColor, 4.0f);
                drawList->AddRect(ImVec2(xpBarX, xpBarY), ImVec2(xpBarX + xpBarWidth, xpBarY + xpBarHeight), IM_COL32(180, 180, 190, 255), 4.0f, ImDrawCornerFlags_All, 1.5f);

                char levelText[32];
                std::snprintf(levelText, sizeof(levelText), "Lv.%d", player->level);
                ImVec2 textSize = ImGui::CalcTextSize(levelText);
                drawList->AddText(ImVec2(xpBarX + xpBarWidth * 0.5f - textSize.x * 0.5f, xpBarY + xpBarHeight * 0.5f - textSize.y * 0.5f), IM_COL32_WHITE, levelText);
            }

            // 6. Level Up Notification
            if (levelUpNotificationTime > 0.0f) {
                char levelUpText[64];
                std::snprintf(levelUpText, sizeof(levelUpText), "LEVEL UP! Lv.%d", displayedLevel);
                ImFont *font = ImGui::GetIO().FontDefault;
                float baseSize = font ? font->FontSize : 24.0f;
                float scale = 2.5f;
                float pulse = 1.0f + 0.1f * std::sin(levelUpNotificationTime * 8.0f);
                float fontSize = baseSize * scale * pulse;

                ImVec2 textPos((displaySize.x - 280.0f * pulse) * 0.5f, (displaySize.y - 60.0f * pulse) * 0.5f);
                drawList->AddText(font, fontSize, ImVec2(textPos.x - 1, textPos.y - 1), IM_COL32(0, 80, 40, 255), levelUpText);
                drawList->AddText(font, fontSize, ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 80, 40, 255), levelUpText);
                drawList->AddText(font, fontSize, textPos, IM_COL32(50, 255, 100, 255), levelUpText);
            }

            // 7. Damage Flash Overlay
            if (player && player->damageFlashTimer > 0) {
                float alpha = glm::clamp(player->damageFlashTimer / 0.3f, 0.0f, 1.0f) * 0.35f;
                ImU32 flashCol = IM_COL32(255, 0, 0, (int)(alpha * 255));
                drawList->AddRectFilled(ImVec2(0, 0), ImVec2(displaySize.x, displaySize.y), flashCol);
            }
        }
    }

    void onDraw(double deltaTime) override {
        our::Entity *playerEntity = findPlayerEntity();
        our::PlayerComponent* currentPlayer = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        auto &keyboard = getApp()->getKeyboard();

        if (currentPlayer && currentPlayer->health <= 0.0f) {
            currentPlayer->health = 0.0f;
            if (currentPlayer->isAlive) {
                our::AudioSystem::playSound("assets/sounds/Death.wav");
            }
            currentPlayer->isAlive = false;
            currentPlayer->gameState = our::GameState::LOSE;
        }

        if (meleeCooldown > 0.0f) {
            meleeCooldown -= (float)deltaTime;
            if (meleeCooldown < 0.0f) meleeCooldown = 0.0f;
        }

        // ── Inventory Toggle ──
        if (keyboard.justPressed(GLFW_KEY_E)) {
            isInventoryOpen = !isInventoryOpen;
            if (isInventoryOpen) cameraController.exit();
            else cameraController.enter(getApp());
        }

        // ── Level Progression Logic ──
        if (currentPlayer && currentPlayer->gameState == our::GameState::PLAYING) {
            int days = timeSystem.getDaysPassed();
            if (currentPlayer->currentLevel == 1) {
                if (currentPlayer->enemiesKilled >= 2 && days >= 3) {
                    currentPlayer->currentLevel = 2; // Level Up
                }
            } else if (currentPlayer->currentLevel == 2) {
                if (currentPlayer->enemiesKilled >= 5 && days >= 5) {
                    currentPlayer->gameState = our::GameState::WIN; // Win condition
                }
            }
        }

        // ── Game-over check: skip game logic when not playing ──
        if (currentPlayer && currentPlayer->gameState != our::GameState::PLAYING) {
            if (auto* menu = dynamic_cast<Menustate*>(getApp()->getState("menu"))) {
                if (currentPlayer->gameState == our::GameState::LOSE) {
                    menu->isGameOver = true;
                    menu->isWin = false;
                } else if (currentPlayer->gameState == our::GameState::WIN) {
                    menu->isGameOver = false;
                    menu->isWin = true;
                }
            }
            getApp()->changeState("menu");
            return;
        }

        movementSystem.update(&engineWorld, (float)deltaTime);
        playerController.update(&engineWorld, (float)deltaTime);
        npcMovementSystem.update(&engineWorld, (float)deltaTime);

        if (playerEntity) {
            if (currentPlayer) {
                currentPlayer->timeSinceDamage += (float)deltaTime;
                if (currentPlayer->damageFlashTimer > 0) currentPlayer->damageFlashTimer -= (float)deltaTime;
                if (currentPlayer->healCooldown > 0) currentPlayer->healCooldown -= (float)deltaTime;
            }
            streamChunksAroundPlayer(playerEntity->localTransform.position);
            updateFirstPersonRig(playerEntity, currentPlayer, (float)deltaTime);
        }

        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        lightSystem.update(&engineWorld, (float)deltaTime);
        timeSystem.update(&engineWorld, (float)deltaTime);

        // Update level up notification timer
        if (levelUpNotificationTime > 0.0f) levelUpNotificationTime -= (float)deltaTime;

        // Level 1 XP increment
        if (currentPlayer && currentPlayer->level == 1) {
            int daysPassed = timeSystem.getDaysPassed();
            if (daysPassed > currentPlayer->daysSurvived) {
                int daysDiff = daysPassed - currentPlayer->daysSurvived;
                currentPlayer->daysSurvived = daysPassed;
                currentPlayer->currentXP += daysDiff * (1.0f / 3.0f);
                if (currentPlayer->currentXP >= 1.0f) {
                    currentPlayer->currentXP = 1.0f;
                    currentPlayer->level = 2;
                    currentPlayer->currentXP = 0.0f;
                    levelUpNotificationTime = 3.0f;
                    displayedLevel = 2;
                    our::AudioSystem::playSound("assets/sounds/levelup.wav");
                }
            }
        }

        blockInteraction.update((float)deltaTime, &engineWorld);
        
        if (playerEntity) {
            enemySystem.update(&engineWorld, &terrainWorld, playerEntity->localTransform.position, (float)deltaTime, terrainMeshDirty);
        }

        // ── Portal win condition ──
        if (playerEntity && currentPlayer && portalEntity) {
            float portalDist = glm::distance(playerEntity->localTransform.position, portalEntity->localTransform.position);
            if (portalDist < 2.0f && currentPlayer->resourcesCollected >= currentPlayer->resourcesRequired) {
                currentPlayer->gameState = our::GameState::WIN;
                currentPlayer->hasReachedPortal = true;
            }
            static float portalTime = 0.0f;
            portalTime += (float)deltaTime;
            portalEntity->localTransform.rotation.y += (float)deltaTime * 2.0f;
            portalEntity->localTransform.position.y += std::sin(portalTime * 3.0f) * 0.01f;
        }

        // ── Food healing (F key) ──
        if (currentPlayer && keyboard.justPressed(GLFW_KEY_F)) {
            if (currentPlayer->foodCount > 0 && currentPlayer->healCooldown <= 0 &&
                currentPlayer->health < currentPlayer->maxHealth) {
                currentPlayer->foodCount--;
                currentPlayer->health = std::min(currentPlayer->health + currentPlayer->healPerFood, currentPlayer->maxHealth);
                currentPlayer->healCooldown = 1.0f;
            }
        }

        // Handle water damage and sound
        if (currentPlayer) {
            float targetVolume = currentPlayer->isUnderwater ? 0.2f : 1.0f;
            our::AudioSystem::setGlobalVolume(targetVolume);

            if (currentPlayer->isUnderwater) {
                currentPlayer->waterDamageTimer += (float)deltaTime;
                while (currentPlayer->waterDamageTimer >= currentPlayer->waterDamageInterval) {
                    currentPlayer->health -= currentPlayer->waterDamageAmount;
                    currentPlayer->shakeTimer = 0.5f;
                    currentPlayer->shakeIntensity = 0.2f;
                    currentPlayer->waterDamageTimer -= currentPlayer->waterDamageInterval;
                    
                    if (currentPlayer->health <= 0.0f) {
                        currentPlayer->health = 0.0f;
                        currentPlayer->isAlive = false;
                        currentPlayer->gameState = our::GameState::LOSE;
                        break;
                    }
                }
            } else if (currentPlayer->waterDamageTimer > 0.0f) {
                currentPlayer->waterDamageTimer = 0.0f;
            }

            float maxRadius = 10.0f;
            float minDistanceSq = maxRadius * maxRadius;
            bool waterFound = false;

            glm::vec3 pos = playerEntity->localTransform.position;
            int ix = static_cast<int>(std::floor(pos.x));
            int iy = static_cast<int>(std::floor(pos.y));
            int iz = static_cast<int>(std::floor(pos.z));

            for (int dx = -6; dx <= 6; ++dx) {
                for (int dy = -3; dy <= 3; ++dy) {
                    for (int dz = -6; dz <= 6; ++dz) {
                        if (terrainWorld.getBlock(ix + dx, iy + dy, iz + dz) == voxel::WATER) {
                            float distSq = (float)(dx*dx + dy*dy + dz*dz);
                            if (distSq < minDistanceSq) {
                                minDistanceSq = distSq;
                                waterFound = true;
                            }
                        }
                    }
                }
            }

            float volume = 0.0f;
            if (waterFound) {
                float distance = std::sqrt(minDistanceSq);
                volume = 1.0f - (distance / maxRadius);
                if (volume < 0.0f) volume = 0.0f;
            }
            our::AudioSystem::setLoopingSoundVolume("water_ambient", volume);
        }

        auto &mouse = getApp()->getMouse();
        if (playerEntity) {
            glm::mat4 camMat = playerEntity->localTransform.toMat4();
            glm::vec3 camPos = playerEntity->localTransform.position;
            glm::vec3 camDir = glm::vec3(camMat * glm::vec4(0, 0, -1, 0));

            bool triggerHitPulse = false;
            if (mouse.justPressed(0) || mouse.justPressed(1)) {
                triggerHitPulse = true;
            }

            // Highlight Hovered Block
            voxel::RayHit hoverHit = terrainWorld.castRay(camPos, camDir, 2.0f);
            if (hoverHit.hit && highlightEntity && highlightEdgesEntity) {
                glm::vec3 pos(hoverHit.x + 0.5f, hoverHit.y + 0.5f, hoverHit.z + 0.5f);
                highlightEntity->localTransform.position = pos;
                highlightEdgesEntity->localTransform.position = pos;
            } else {
                if (highlightEntity) highlightEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
                if (highlightEdgesEntity) highlightEdgesEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
            }

            // Attack / Break
            if (mouse.justPressed(0) && currentPlayer && !currentPlayer->isUnderwater && !isInventoryOpen) {
                if (!tryMeleeAttack(camPos, camDir)) {
                    our::Entity *hitNPC = findHitNPC(camPos, camDir, 2.0f);
                    if (hitNPC) {
                        killNPCAndAwardMeat(hitNPC, currentPlayer);
                        our::AudioSystem::playSound("assets/sounds/Death.wav");
                    }
                }
            }

            if (mouse.isPressed(0) && currentPlayer && !currentPlayer->isUnderwater && !isInventoryOpen) {
                voxel::RayHit hit = terrainWorld.castRay(camPos, camDir, 2.0f);
                if (hit.hit) {
                    int type = terrainWorld.getBlock(hit.x, hit.y, hit.z);
                    auto holdResult = blockInteraction.processHold(hit, type, terrainWorld, &engineWorld, terrainMeshDirty, (float)deltaTime);
                    if (holdResult == BlockInteractionSystem::HoldResult::Broken) {
                        our::AudioSystem::playSound("assets/sounds/Hit.wav");
                        registerCollectedBlock(currentPlayer, type);
                        if (type == voxel::LEAF) currentPlayer->foodCount++;
                    } else if (holdResult == BlockInteractionSystem::HoldResult::HitPulse) {
                        triggerHitPulse = true;
                        if (type == voxel::GRASS) our::AudioSystem::playSound("assets/sounds/Grass.wav");
                        else if (type == voxel::DIRT) our::AudioSystem::playSound("assets/sounds/Dirt.wav");
                        else if (type == voxel::SAND) our::AudioSystem::playSound("assets/sounds/Sand.wav");
                        else if (type == voxel::STONE) our::AudioSystem::playSound("assets/sounds/Stone.wav");
                        else if (type == voxel::Glass) our::AudioSystem::playSound("assets/sounds/Glass.wav");
                        else if (type == voxel::WOOD || type == voxel::LOG) our::AudioSystem::playSound("assets/sounds/Wood.wav");
                        else our::AudioSystem::playSound("assets/sounds/Hit.wav");
                    }
                }
            } else if (mouse.justReleased(0)) {
                blockInteraction.currentTargetContext = {-1, -1, -1};
                blockInteraction.accumulatedBreakTime = 0.0f;
                blockInteraction.particleSpawnTimer = 0.0f;
            }

            // Place Block
            if (mouse.justPressed(1) && currentPlayer && !currentPlayer->isUnderwater && !isInventoryOpen) {
                voxel::RayHit hit = terrainWorld.castRay(camPos, camDir, 2.0f);
                int placeType = hotbarBlockType(currentPlayer->inventoryHotbarSlot);
                int* stack = inventoryCountForType(currentPlayer, placeType);
                if (hit.hit && stack && *stack > 0) {
                    terrainWorld.placeBlock(hit, placeType);
                    (*stack)--;
                    terrainMeshDirty = true;
                }
            }

            // Hand Animation & Interaction Update
            our::Entity *handEntity = nullptr;
            our::HandComponent *handComp = nullptr;

            for (auto entity : engineWorld.getEntities()) {
                if (entity && entity->name == "player_hand") {
                    handEntity = entity;
                    handEntity->localTransform.scale = glm::vec3(0.15f, 0.2f, 0.1f);
                    handComp = entity->getComponent<our::HandComponent>();
                    break;
                }
            }

            if (handEntity && handComp) {
                float interactRange = handComp->interactionRange;
                voxel::RayHit localHoverHit = terrainWorld.castRay(camPos, camDir, interactRange);
                our::Entity *localHoverNPC = findHitNPC(camPos, camDir, interactRange);

                bool targetInRange = false;
                glm::vec3 targetPos(0.0f);

                if (localHoverNPC) {
                    targetInRange = true;
                    targetPos = localHoverNPC->localTransform.position;
                } else if (localHoverHit.hit) {
                    targetInRange = true;
                    targetPos = glm::vec3(localHoverHit.x + 0.5f, localHoverHit.y + 0.5f, localHoverHit.z + 0.5f);
                }

                our::HandSystem::update(
                    handEntity, handComp, camPos, camDir, camMat, deltaTime,
                    triggerHitPulse, targetInRange, targetPos, &terrainWorld
                );
            }
        }

        if (terrainMeshDirty) rebuildMesh();
        
        // Stop rendering main game logic if inventory is overlaid
        if (isInventoryOpen) {
            renderer.render(&engineWorld);
            return; 
        }

        renderer.render(&engineWorld);
        engineWorld.deleteMarkedEntities();

        if (keyboard.justPressed(GLFW_KEY_ESCAPE)) getApp()->changeState("menu");
    }

    void onKeyEvent(int key, int scancode, int action, int mods) override {
        if (action != GLFW_PRESS || isInventoryOpen) return;
        our::Entity* playerEntity = findPlayerEntity();
        our::PlayerComponent* player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        if (!player) return;
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5) player->inventoryHotbarSlot = key - GLFW_KEY_1;
    }

    void onScrollEvent(double x, double y) override {
        if (isInventoryOpen) return;
        our::Entity* playerEntity = findPlayerEntity();
        our::PlayerComponent* player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        if (!player || y == 0) return;
        player->inventoryHotbarSlot = (player->inventoryHotbarSlot + (y > 0 ? -1 : 1) + kHotbarSlots) % kHotbarSlots;
    }

    void onDestroy() override {
        our::AudioSystem::stopLoopingSound("water_ambient");
        enemySystem.destroy();
        clearAllChunkRenderGroups();
        engineWorld.deleteMarkedEntities();
        renderer.destroy();
        cameraController.exit();
        playerController.exit();
        engineWorld.clear();
        if (highlightEdgesMesh) delete highlightEdgesMesh;
        our::clearAllAssets();
    }
};