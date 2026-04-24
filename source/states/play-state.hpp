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
#include <asset-loader.hpp>
#include <texture/texture2d.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <components/player.hpp>
#include <components/enemy-component.hpp>
#include <audio/audio.hpp>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <limits>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdio>

class Playstate : public our::State {
    struct ChunkRenderGroup {
        std::vector<our::Entity*> entities;
        std::vector<our::Mesh*> meshes;
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
    our::Entity* highlightEntity = nullptr;      // Semi-transparent fill
    our::Entity* highlightEdgesEntity = nullptr; // Clean square borders
    our::Mesh* highlightEdgesMesh = nullptr;     // Line-based mesh for borders
    our::Entity* portalEntity = nullptr;         // Win-condition portal
    our::Entity* firstPersonRightArm = nullptr;
    our::Entity* firstPersonLeftArm = nullptr;
    float firstPersonAnimTime = 0.0f;
    float firstPersonAttackTimer = 0.0f;
    float meleeCooldown = 0.0f;
    const float meleeCooldownDuration = 0.24f;
    const float meleeRange = 3.0f;
    const float meleeDamage = 8.0f;
    BlockInteractionSystem blockInteraction;
    our::EnemySystem enemySystem;

    // Chunk Streaming State
    int chunkLoadRadius = 2;
    int chunkUnloadRadius = 3;
    int currentCenterChunkX = std::numeric_limits<int>::min();
    int currentCenterChunkZ = std::numeric_limits<int>::min();
    bool terrainMeshDirty = true;
    std::unordered_map<std::string, ChunkRenderGroup> chunkRenderGroups;

    // --- Inventory & Hotbar Helpers ---
    static constexpr int kHotbarSlots = 5;

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

    void registerCollectedBlock(our::PlayerComponent* player, int blockType) {
        int* slot = inventoryCountForType(player, blockType);
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

    // --- Chunk Rendering Management (from world/chunks) ---
    void clearChunkRenderGroup(const std::string& chunkKey) {
        auto it = chunkRenderGroups.find(chunkKey);
        if (it == chunkRenderGroups.end()) return;
        for (auto* entity : it->second.entities) engineWorld.markForRemoval(entity);
        for (auto* mesh : it->second.meshes) delete mesh;
        chunkRenderGroups.erase(it);
    }

    void clearAllChunkRenderGroups() {
        for (auto& entry : chunkRenderGroups) {
            for (auto* entity : entry.second.entities) engineWorld.markForRemoval(entity);
            for (auto* mesh : entry.second.meshes) delete mesh;
        }
        chunkRenderGroups.clear();
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

    // --- UI Drawing (Combined) ---
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

        cameraController.enter(getApp());
        playerController.enter(getApp());
        timeSystem.initialize(&engineWorld);
        renderer.initialize(getApp()->getFrameBufferSize(), config["renderer"]);

        highlightEntity = engineWorld.add();
        auto* meshRenderer = highlightEntity->addComponent<our::MeshRendererComponent>();
        meshRenderer->mesh = our::AssetLoader<our::Mesh>::get("cube");
        meshRenderer->material = our::AssetLoader<our::Material>::get("highlight-fill");
        highlightEntity->localTransform.scale = glm::vec3(0.502f); // Slightly larger than a block

        highlightEdgesEntity = engineWorld.add();
        auto* edgesRenderer = highlightEdgesEntity->addComponent<our::MeshRendererComponent>();
        highlightEdgesMesh = our::mesh_utils::cubeEdges();
        edgesRenderer->mesh = highlightEdgesMesh;
        edgesRenderer->material = our::AssetLoader<our::Material>::get("wireframe");
        highlightEdgesEntity->localTransform.scale = glm::vec3(0.505f); // Slightly larger than the fill to avoid z-fighting

        blockInteraction.initialize(&engineWorld);
        enemySystem.initialize();
        our::AudioSystem::startLoopingSound("water_ambient", "assets/sounds/water_flowing.wav");
        our::AudioSystem::setLoopingSoundVolume("water_ambient", 0.0f);

        our::Entity* playerEntity = findPlayerEntity();
        if (playerEntity) {
            streamChunksAroundPlayer(playerEntity->localTransform.position);
            // Spawn player on top of terrain
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
            // Find ground at portal location
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
    }

    void onImmediateGui() override {
        our::Entity* playerEntity = findPlayerEntity();
        if (!playerEntity) return;

        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        
        // 1. Draw Hotbar
        our::PlayerComponent* player = playerEntity->getComponent<our::PlayerComponent>();
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
        if (heartsTex) {
            ImDrawList* fgDl = ImGui::GetForegroundDrawList();
            float heartSize = 28.0f;
            float heartGap = 2.0f;
            float maxHearts = 10;
            float totalW = maxHearts * heartSize + (maxHearts - 1) * heartGap;
            float startX = displaySize.x * 0.5f - totalW * 0.5f;
            float invH = 60.0f + 26.0f; // hotbar frame height
            float startY = displaySize.y - invH - 16.0f - heartSize - 12.0f;
                
            ImTextureID texID = (ImTextureID)(intptr_t)heartsTex->getOpenGLName();
            float texW = 45.0f;

            int fullHearts = static_cast<int>(player->health / 10.0f);
            float partialHeart = (player->health / 10.0f) - fullHearts;

            ImVec2 uvFull0((27.0f + 0.5f) / texW, 1.0f);
            ImVec2 uvFull1((36.0f - 0.5f) / texW, 0.0f);
            ImVec2 uvEmpty0((0.0f + 0.5f) / texW, 1.0f);
            ImVec2 uvEmpty1((9.0f - 0.5f) / texW, 0.0f);

            for (int i = 0; i < maxHearts; ++i) {
                ImVec2 pMin(startX + i * (heartSize + heartGap), startY);
                ImVec2 pMax(pMin.x + heartSize, pMin.y + heartSize);
                if (i < fullHearts) {
                    fgDl->AddImage(texID, pMin, pMax, uvFull0, uvFull1);
                } else if (i == fullHearts && partialHeart > 0.0f) {
                    ImVec2 splitX(pMin.x + heartSize * partialHeart, pMin.y);
                    ImVec2 splitX1(pMin.x + heartSize * partialHeart, pMax.y);
                    fgDl->AddImage(texID, pMin, splitX, uvFull0, uvFull1);
                    fgDl->AddImage(texID, splitX1, pMax, uvEmpty0, uvEmpty1);
                } else {
                    fgDl->AddImage(texID, pMin, pMax, uvEmpty0, uvEmpty1);
                }
            }
        }

        // 3. Draw Crosshair
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
        drawList->AddLine(ImVec2(center.x - 8, center.y), ImVec2(center.x + 8, center.y), IM_COL32(255, 255, 255, 220), 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 8), ImVec2(center.x, center.y + 8), IM_COL32(255, 255, 255, 220), 2.0f);

        // 4. Objective Progress
        if (player) {
            float invH = 60.0f + 26.0f;
            float heartH = 28.0f;
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
            ImDrawList* fgdl = ImGui::GetForegroundDrawList();
            fgdl->AddText(ImVec2(objX + 1, objY + 1), IM_COL32(0,0,0,180), objBuf);
            fgdl->AddText(ImVec2(objX, objY), IM_COL32(255, 220, 80, 255), objBuf);

            // Food count indicator
            if (player->foodCount > 0) {
                char foodBuf[32];
                std::snprintf(foodBuf, sizeof(foodBuf), "Food: %d (F to heal)", player->foodCount);
                ImVec2 foodSize = ImGui::CalcTextSize(foodBuf);
                float foodX = displaySize.x * 0.5f - foodSize.x * 0.5f;
                fgdl->AddText(ImVec2(foodX + 1, objY - 22.0f + 1), IM_COL32(0,0,0,180), foodBuf);
                fgdl->AddText(ImVec2(foodX, objY - 22.0f), IM_COL32(120, 255, 120, 255), foodBuf);
            }
        }

        // 5. Damage Flash Overlay
        if (player && player->damageFlashTimer > 0) {
            float alpha = glm::clamp(player->damageFlashTimer / 0.3f, 0.0f, 1.0f) * 0.35f;
            ImU32 flashCol = IM_COL32(255, 0, 0, (int)(alpha * 255));
            ImGui::GetForegroundDrawList()->AddRectFilled(
                ImVec2(0, 0), ImVec2(displaySize.x, displaySize.y), flashCol);
        }

        // Game Over / Win overlays have been relocated to the menu system.
    }

    void onDraw(double deltaTime) override {
        our::Entity *playerEntity = findPlayerEntity();
        our::PlayerComponent* currentPlayer = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;

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

        if (playerEntity) {
            if (currentPlayer) {
                currentPlayer->timeSinceDamage += (float)deltaTime;
                // Damage flash countdown
                if (currentPlayer->damageFlashTimer > 0)
                    currentPlayer->damageFlashTimer -= (float)deltaTime;
                // Heal cooldown
                if (currentPlayer->healCooldown > 0)
                    currentPlayer->healCooldown -= (float)deltaTime;
            }
            streamChunksAroundPlayer(playerEntity->localTransform.position);
            updateFirstPersonRig(playerEntity, currentPlayer, (float)deltaTime);
        }

        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        lightSystem.update(&engineWorld, (float)deltaTime);
        timeSystem.update(&engineWorld, (float)deltaTime);

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
            // Portal bobbing animation
            static float portalTime = 0.0f;
            portalTime += (float)deltaTime;
            portalEntity->localTransform.rotation.y += (float)deltaTime * 2.0f;
            portalEntity->localTransform.position.y += std::sin(portalTime * 3.0f) * 0.01f;
        }

        // ── Food healing (F key) ──
        if (currentPlayer && getApp()->getKeyboard().justPressed(GLFW_KEY_F)) {
            if (currentPlayer->foodCount > 0 && currentPlayer->healCooldown <= 0 &&
                currentPlayer->health < currentPlayer->maxHealth) {
                currentPlayer->foodCount--;
                currentPlayer->health = std::min(currentPlayer->health + currentPlayer->healPerFood, currentPlayer->maxHealth);
                currentPlayer->healCooldown = 1.0f;
            }
        }

        // Handle water damage
        our::PlayerComponent* player = nullptr;
        if (playerEntity) {
            player = playerEntity->getComponent<our::PlayerComponent>();
        }
        if (player) {
            float targetVolume = player->isUnderwater ? 0.2f : 1.0f;
            our::AudioSystem::setGlobalVolume(targetVolume);

            if (player->isUnderwater) {
                player->waterDamageTimer += (float)deltaTime;
                while (player->waterDamageTimer >= player->waterDamageInterval) {
                    player->health -= player->waterDamageAmount;
                    player->waterDamageTimer -= player->waterDamageInterval;
                    
                    if (player->health <= 0.0f) {
                        player->health = 0.0f;
                        player->isAlive = false;
                        player->gameState = our::GameState::LOSE;
                        break;
                    }
                }
            } else if (player->waterDamageTimer > 0.0f) {
                player->waterDamageTimer = 0.0f;
            }

            // Dynamic ambient sound based on distance to nearest water
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
            our::PlayerComponent* player = playerEntity->getComponent<our::PlayerComponent>();

            // Highlight Hovered Block
            voxel::RayHit hoverHit = terrainWorld.castRay(camPos, camDir);
            if (hoverHit.hit && highlightEntity && highlightEdgesEntity) {
                glm::vec3 pos(hoverHit.x + 0.5f, hoverHit.y + 0.5f, hoverHit.z + 0.5f);
                highlightEntity->localTransform.position = pos;
                highlightEdgesEntity->localTransform.position = pos;
            } else {
                if (highlightEntity) highlightEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
                if (highlightEdgesEntity) highlightEdgesEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
            }

            // Break Block (disabled underwater)
            if (mouse.isPressed(0) && player && !player->isUnderwater) {
                if (!tryMeleeAttack(camPos, camDir)) {
                    voxel::RayHit hit = terrainWorld.castRay(camPos, camDir);
                    if (hit.hit) {
                        int type = terrainWorld.getBlock(hit.x, hit.y, hit.z);
                        bool blockBroken = blockInteraction.processHold(
                            hit, type, terrainWorld, &engineWorld, terrainMeshDirty, (float)deltaTime
                        );

                        if (blockBroken) {
                            // The block was completely broken
                            our::AudioSystem::playSound("assets/sounds/Hit.wav");

                            if (player) {
                                registerCollectedBlock(player, type);
                                // LEAF blocks give food
                                if (type == voxel::LEAF) {
                                    player->foodCount++;
                                }
                            }
                        } else if (mouse.justPressed(0)) {
                            // The block was hit but not broken
                            if (type == voxel::GRASS) our::AudioSystem::playSound("assets/sounds/Grass.wav");
                            else if (type == voxel::DIRT) our::AudioSystem::playSound("assets/sounds/Dirt.wav");
                            else if (type == voxel::SAND) our::AudioSystem::playSound("assets/sounds/Sand.wav");
                            else if (type == voxel::STONE) our::AudioSystem::playSound("assets/sounds/Stone.wav");
                            else if (type == voxel::Glass) our::AudioSystem::playSound("assets/sounds/Glass.wav");
                            else if (type == voxel::WOOD) our::AudioSystem::playSound("assets/sounds/Wood.wav");
                            else our::AudioSystem::playSound("assets/sounds/Hit.wav");
                        }
                    }
                }
            }
            if (mouse.justReleased(0)) {
                blockInteraction.currentTargetContext = {-1, -1, -1};
                blockInteraction.accumulatedBreakTime = 0.0f;
                blockInteraction.particleSpawnTimer = 0.0f;
            }
            // Place Block (disabled underwater)
            if (mouse.justPressed(1) && player && !player->isUnderwater) {
                voxel::RayHit hit = terrainWorld.castRay(camPos, camDir);
                int placeType = hotbarBlockType(player->inventoryHotbarSlot);
                int* stack = inventoryCountForType(player, placeType);
                if (hit.hit && stack && *stack > 0) {
                    terrainWorld.placeBlock(hit, placeType);
                    (*stack)--;
                    terrainMeshDirty = true;
                }
            }
        }

        if (terrainMeshDirty) rebuildMesh();
        renderer.render(&engineWorld);

        // Delete particles or hit blocks that have expired outside of chunk builds
        engineWorld.deleteMarkedEntities();

        if (getApp()->getKeyboard().justPressed(GLFW_KEY_ESCAPE)) getApp()->changeState("menu");
    }

    void onKeyEvent(int key, int scancode, int action, int mods) override {
        if (action != GLFW_PRESS) return;
        our::Entity* playerEntity = findPlayerEntity();
        our::PlayerComponent* player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        if (!player) return;
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5) player->inventoryHotbarSlot = key - GLFW_KEY_1;
    }

    void onScrollEvent(double x, double y) override {
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