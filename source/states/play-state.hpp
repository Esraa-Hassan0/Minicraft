#pragma once

#include <application.hpp>
#include <ecs/world.hpp>
#include <mesh/mesh-utils.hpp>
#include <systems/forward-renderer.hpp>
#include <systems/free-camera-controller.hpp>
#include <systems/movement.hpp>
#include <systems/player-controller.hpp>
#include <systems/collision-system.hpp>
#include <systems/block-interaction.hpp>
#include <systems/light.hpp>
#include <systems/time-system.hpp>
#include <asset-loader.hpp>
#include <texture/texture2d.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <components/player.hpp>
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
    our::BlockInteractionSystem blockInteraction;
    our::LightSystem lightSystem;
    our::TimeSystem timeSystem;

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

    our::Material* getMaterialForBlockType(int blockType) {
        switch (blockType) {
            case voxel::STONE: return our::AssetLoader<our::Material>::get("stone");
            case voxel::GRASS:
            case voxel::DIRT:  return our::AssetLoader<our::Material>::get("grass");
            case voxel::SAND:  return our::AssetLoader<our::Material>::get("sand");
            case voxel::WATER: return our::AssetLoader<our::Material>::get("water");
            case voxel::WOOD:  return our::AssetLoader<our::Material>::get("wood-block");
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
            voxel::STONE, voxel::GRASS, voxel::DIRT, voxel::SAND, 
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

        if(hotbarSlot == 0) { tex = our::AssetLoader<our::Texture2D>::get("grass"); fallbackColor = IM_COL32(72, 130, 58, 255); }
        else if(hotbarSlot == 1) fallbackColor = IM_COL32(115, 77, 51, 255);
        else if(hotbarSlot == 2) { tex = our::AssetLoader<our::Texture2D>::get("wood"); fallbackColor = IM_COL32(130, 85, 48, 255); }
        else if(hotbarSlot == 3) fallbackColor = IM_COL32(118, 118, 118, 255);
        else if(hotbarSlot == 4) fallbackColor = IM_COL32(204, 190, 72, 255);

        if (tex) dl->AddImage((ImTextureID)(intptr_t)tex->getOpenGLName(), iconMin, iconMax);
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
        blockInteraction.enter(getApp());
        timeSystem.initialize(&engineWorld);
        renderer.initialize(getApp()->getFrameBufferSize(), config["renderer"]);

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

        // 2. Draw Crosshair
        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
        drawList->AddLine(ImVec2(center.x - 8, center.y), ImVec2(center.x + 8, center.y), IM_COL32(255, 255, 255, 220), 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 8), ImVec2(center.x, center.y + 8), IM_COL32(255, 255, 255, 220), 2.0f);
    }

    void onDraw(double deltaTime) override {
        movementSystem.update(&engineWorld, (float)deltaTime);
        playerController.update(&engineWorld, (float)deltaTime);
        
        our::Entity *playerEntity = findPlayerEntity();
        if (playerEntity) streamChunksAroundPlayer(playerEntity->localTransform.position);

        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        lightSystem.update(&engineWorld, (float)deltaTime);
        timeSystem.update(&engineWorld, (float)deltaTime);

        auto &mouse = getApp()->getMouse();
        if (playerEntity) {
            glm::mat4 camMat = playerEntity->localTransform.toMat4();
            glm::vec3 camPos = playerEntity->localTransform.position;
            glm::vec3 camDir = glm::vec3(camMat * glm::vec4(0, 0, -1, 0));
            our::PlayerComponent* player = playerEntity->getComponent<our::PlayerComponent>();

            // Break Block
            if (mouse.justPressed(0)) {
                RayHit hit = terrainWorld.castRay(camPos, camDir);
                if (hit.hit) {
                    int type = terrainWorld.getBlock(hit.x, hit.y, hit.z);
                    if (type == voxel::GRASS || type == voxel::DIRT) our::AudioSystem::playSound("assets/sounds/Grass.wav");
                    else if (type == voxel::WOOD) our::AudioSystem::playSound("assets/sounds/Wood.wav");
                    else our::AudioSystem::playSound("assets/sounds/Hit.wav");

                    if (player) registerCollectedBlock(player, type);
                    terrainWorld.breakBlock(hit);
                    terrainMeshDirty = true;
                }
            }
            // Place Block
            if (mouse.justPressed(1) && player) {
                RayHit hit = terrainWorld.castRay(camPos, camDir);
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
        clearAllChunkRenderGroups();
        engineWorld.deleteMarkedEntities();
        renderer.destroy();
        cameraController.exit();
        playerController.exit();
        blockInteraction.exit();
        engineWorld.clear();
        our::clearAllAssets();
    }
};