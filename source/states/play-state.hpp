#pragma once

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
#include <systems/npc-movement-system.hpp>
#include <systems/hand-system.hpp>
#include <systems/enemy-system.hpp>
#include <asset-loader.hpp>
#include <texture/texture2d.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <components/player.hpp>
#include <components/killable-npc.hpp>
#include <components/npc-movement.hpp>
#include <components/hand.hpp>
#include <components/enemy-component.hpp>
#include <audio/audio.hpp>
#include <unordered_map>
#include <vector>
#include <map>
#include <random>
#include <components/aabb-collider.hpp>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <limits>
#include <string>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

class Playstate : public our::State
{
    struct NPCSpawnData
    {
        int chunkX, chunkZ;
        int localX, localY, localZ;
        float scaleX, scaleY, scaleZ;
        std::string meshName, matName;
        int foodReward;
        std::string npcType;
        float speed, moveRadius, waitTime;
        std::string movementType;
        bool isFlying = false;
        glm::vec3 aabbCenter;
        glm::vec3 aabbHalfSize;
    };

    struct ChunkRenderGroup
    {
        std::vector<our::Entity *> entities;
        std::vector<our::Mesh *> meshes;
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
    our::NPCMovementSystem npcMovementSystem;
    our::EnemySystem enemySystem;
    our::Entity *highlightEntity = nullptr;      // Semi-transparent fill
    our::Entity *highlightEdgesEntity = nullptr; // Clean square borders
    our::Mesh *highlightEdgesMesh = nullptr;     // Line-based mesh for borders
    BlockInteractionSystem blockInteraction;

    // Hand Animation State
    float handAnimTime = 0.0f;
    float hitAnimTime = 0.0f;
    bool isHitting = false;

    // Chunk Streaming State
    int chunkLoadRadius = 2;
    int chunkUnloadRadius = 3;
    int currentCenterChunkX = std::numeric_limits<int>::min();
    int currentCenterChunkZ = std::numeric_limits<int>::min();
    bool terrainMeshDirty = true;
    std::unordered_map<std::string, ChunkRenderGroup> chunkRenderGroups;
    std::vector<NPCSpawnData> npcTemplates;

    // --- Inventory & Hotbar Helpers ---
    static constexpr int kHotbarSlots = 9;
    bool isInventoryOpen = false;
    bool beesAngry = false; // Set true when any bee is hit
    float waterAnimTimer = 0.0f; // Timer for periodic water mesh rebuild

    // Level up notification
    float levelUpNotificationTime = 0.0f;
    int displayedLevel = 1;

    // Level target notification
    float levelTargetPopupTime = 0.0f;
    int currentLevelTarget = 0;

    // Get the block type to place from the selected hotbar slot
    static int hotbarBlockType(our::PlayerComponent *player)
    {
        if (!player) return 0;
        int itemId = player->getSelectedItemId();
        return voxel::isBlockItem(itemId) ? itemId : 0;
    }

    void registerCollectedBlock(our::PlayerComponent *player, int blockType)
    {
        if (!player) return;
        // Collecting a Diamond block immediately triggers a WIN condition
        if (blockType == voxel::Diamond)
        {
            player->gameState = our::GameState::WIN;
            our::AudioSystem::playSound("assets/sounds/vectory.mp3");
            return;
        }

        // Level 3: Collecting any other block after reaching level 3
        if (player->level == 3)
        {
            player->currentXP = 1.0f;
            player->level = 4;
            levelUpNotificationTime = 3.0f;
            displayedLevel = 4;
            our::AudioSystem::playSound("assets/sounds/levelup.wav");
        }

        player->addItem(blockType, 1);
        player->resourcesCollected++;
    }

    // --- Crafting Recipes ---
    struct CraftingRecipe {
        int grid[4]; // [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right; 0=empty
        int resultId;
        int resultCount;
    };

    static const std::vector<CraftingRecipe>& getCraftingRecipes() {
        static const std::vector<CraftingRecipe> recipes = {
            // 1 Log -> 4 Wood (all positions)
            {{voxel::LOG, 0, 0, 0}, voxel::WOOD, 4},
            {{0, voxel::LOG, 0, 0}, voxel::WOOD, 4},
            {{0, 0, voxel::LOG, 0}, voxel::WOOD, 4},
            {{0, 0, 0, voxel::LOG}, voxel::WOOD, 4},
            
            // 1 Sand -> 1 Glass (all positions)
            {{voxel::SAND, 0, 0, 0}, voxel::Glass, 1},
            {{0, voxel::SAND, 0, 0}, voxel::Glass, 1},
            {{0, 0, voxel::SAND, 0}, voxel::Glass, 1},
            {{0, 0, 0, voxel::SAND}, voxel::Glass, 1},
            
            // 2 Wood vertical -> Wooden Axe
            {{voxel::WOOD, 0, voxel::WOOD, 0}, voxel::TOOL_WOODEN_AXE, 1},
            {{0, voxel::WOOD, 0, voxel::WOOD}, voxel::TOOL_WOODEN_AXE, 1},
            
            // 2 Wood horizontal -> Wooden Pickaxe
            {{voxel::WOOD, voxel::WOOD, 0, 0}, voxel::TOOL_WOODEN_PICKAXE, 1},
            {{0, 0, voxel::WOOD, voxel::WOOD}, voxel::TOOL_WOODEN_PICKAXE, 1},
            
            // 2 Stone vertical -> Stone Axe
            {{voxel::STONE, 0, voxel::STONE, 0}, voxel::TOOL_STONE_AXE, 1},
            {{0, voxel::STONE, 0, voxel::STONE}, voxel::TOOL_STONE_AXE, 1},
            
            // 2 Stone horizontal -> Stone Pickaxe
            {{voxel::STONE, voxel::STONE, 0, 0}, voxel::TOOL_STONE_PICKAXE, 1},
            {{0, 0, voxel::STONE, voxel::STONE}, voxel::TOOL_STONE_PICKAXE, 1},
        };
        return recipes;
    }

    void checkCraftingRecipe(our::PlayerComponent *player) {
        if (!player) return;
        player->craftingResult.clear();
        for (const auto& recipe : getCraftingRecipes()) {
            bool match = true;
            for (int i = 0; i < 4; i++) {
                int gridItem = player->craftingGrid[i].isEmpty() ? 0 : player->craftingGrid[i].itemId;
                if (gridItem != recipe.grid[i]) { match = false; break; }
            }
            if (match) {
                player->craftingResult = {recipe.resultId, recipe.resultCount};
                return;
            }
        }
    }

    void performCraft(our::PlayerComponent *player) {
        if (!player || player->craftingResult.isEmpty()) return;
        // Consume ingredients
        for (int i = 0; i < 4; i++) {
            if (!player->craftingGrid[i].isEmpty()) {
                player->craftingGrid[i].count--;
                if (player->craftingGrid[i].count <= 0) player->craftingGrid[i].clear();
            }
        }
        // Give result to cursor or add to inventory
        if (player->cursorItem.isEmpty()) {
            player->cursorItem = player->craftingResult;
        } else if (player->cursorItem.itemId == player->craftingResult.itemId) {
            player->cursorItem.count += player->craftingResult.count;
        } else {
            player->addItem(player->craftingResult.itemId, player->craftingResult.count);
        }
        player->craftingResult.clear();
        checkCraftingRecipe(player); // Re-check after consuming
    }

    // Click handler for inventory slot interaction
    void handleSlotClick(our::InventorySlot &slot, our::PlayerComponent *player, bool singleAction = false) {
        if (!player) return;
        if (player->cursorItem.isEmpty() && slot.isEmpty()) return;
        
        if (player->cursorItem.isEmpty()) {
            if (singleAction) {
                // Pick up half
                int half = slot.count / 2;
                int remain = slot.count - half;
                if (half > 0) {
                    player->cursorItem = {slot.itemId, half};
                    slot.count = remain;
                } else {
                    player->cursorItem = slot;
                    slot.clear();
                }
            } else {
                // Pick up all
                player->cursorItem = slot;
                slot.clear();
            }
        } else if (slot.isEmpty()) {
            if (singleAction) {
                // Place 1
                slot = {player->cursorItem.itemId, 1};
                player->cursorItem.count--;
                if (player->cursorItem.count <= 0) player->cursorItem.clear();
            } else {
                // Place all
                slot = player->cursorItem;
                player->cursorItem.clear();
            }
        } else if (slot.itemId == player->cursorItem.itemId) {
            if (singleAction) {
                // Place 1
                slot.count++;
                player->cursorItem.count--;
                if (player->cursorItem.count <= 0) player->cursorItem.clear();
            } else {
                // Stack all
                slot.count += player->cursorItem.count;
                player->cursorItem.clear();
            }
        } else {
            // Swap
            std::swap(slot, player->cursorItem);
        }
    }

    // Get texture name for an item ID (for drawing icons)
    static const char* getItemTextureName(int itemId) {
        switch(itemId) {
            case voxel::GRASS: return "grass-side";
            case voxel::DIRT: return "dirt";
            case voxel::WOOD: return "wood";
            case voxel::STONE: return "stone";
            case voxel::SAND: return "sand";
            case voxel::LOG: return "log";
            case voxel::LEAF: return "leaf";
            case voxel::Diamond: return "diamond";
            case voxel::Glass: return "glass";
            case voxel::TOOL_WOODEN_AXE: return "wooden_axe";
            case voxel::TOOL_STONE_AXE: return "stone_axe";
            case voxel::TOOL_WOODEN_PICKAXE: return "wooden_pickaxe";
            case voxel::TOOL_STONE_PICKAXE: return "stone_pickaxe";
            default: return nullptr;
        }
    }

    static ImU32 getItemFallbackColor(int itemId) {
        switch(itemId) {
            case voxel::GRASS: return IM_COL32(72, 130, 58, 255);
            case voxel::DIRT: return IM_COL32(115, 77, 51, 255);
            case voxel::WOOD: return IM_COL32(130, 85, 48, 255);
            case voxel::STONE: return IM_COL32(118, 118, 118, 255);
            case voxel::SAND: return IM_COL32(204, 190, 72, 255);
            case voxel::LOG: return IM_COL32(90, 60, 30, 255);
            case voxel::LEAF: return IM_COL32(50, 140, 50, 255);
            case voxel::Diamond: return IM_COL32(100, 220, 255, 255);
            case voxel::Glass: return IM_COL32(200, 220, 255, 200);
            case voxel::TOOL_WOODEN_AXE: return IM_COL32(160, 110, 50, 255);
            case voxel::TOOL_STONE_AXE: return IM_COL32(140, 140, 140, 255);
            case voxel::TOOL_WOODEN_PICKAXE: return IM_COL32(170, 120, 60, 255);
            case voxel::TOOL_STONE_PICKAXE: return IM_COL32(150, 150, 155, 255);
            default: return IM_COL32(60, 60, 65, 255);
        }
    }

    // --- Utility & Search ---
    static int worldToChunkCoordinate(float worldCoord)
    {
        return static_cast<int>(std::floor(worldCoord / static_cast<float>(voxel::Chunk::CHUNK_SIZE)));
    }

    our::Entity *findPlayerEntity()
    {
        for (auto entity : engineWorld.getEntities())
        {
            if (!entity)
                continue;
            auto *camera = entity->getComponent<our::CameraComponent>();
            auto *player = entity->getComponent<our::PlayerComponent>();
            if (camera && player)
                return entity;
        }
        return nullptr;
    }

    our::Material *getMaterialForBlockType(int blockType)
    {
        switch (blockType)
        {
        case voxel::STONE:
            return our::AssetLoader<our::Material>::get("stone");
        case voxel::GRASS:
            return nullptr; // Grass is handled manually per face
        case voxel::DIRT:
            return our::AssetLoader<our::Material>::get("dirt");
        case voxel::SAND:
            return our::AssetLoader<our::Material>::get("sand");
        case voxel::WATER:
            return our::AssetLoader<our::Material>::get("water");
        case voxel::WOOD:
            return our::AssetLoader<our::Material>::get("wood");
        case voxel::LOG:
            return our::AssetLoader<our::Material>::get("log");
        case voxel::LEAF:
            return our::AssetLoader<our::Material>::get("leaf");
        case voxel::Diamond:
            return our::AssetLoader<our::Material>::get("diamond");
        case voxel::Glass:
            return our::AssetLoader<our::Material>::get("glass");
        default:
            return our::AssetLoader<our::Material>::get("default");
        }
    }

    // --- Chunk Rendering Management (from world/chunks) ---
    void clearChunkRenderGroup(const std::string &chunkKey)
    {
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

    void clearAllChunkRenderGroups()
    {
        for (auto &entry : chunkRenderGroups)
        {
            for (auto *entity : entry.second.entities)
                engineWorld.markForRemoval(entity);
            for (auto *mesh : entry.second.meshes)
                delete mesh;
        }
        chunkRenderGroups.clear();
    }

    void buildChunkRenderGroup(const std::string &chunkKey, const voxel::Chunk &chunk)
    {
        ChunkRenderGroup renderGroup;
        const int meshBlockTypes[] = {
            voxel::STONE, voxel::DIRT, voxel::SAND, voxel::LOG,
            voxel::WATER, voxel::WOOD, voxel::LEAF, voxel::Diamond, voxel::Glass};

        glm::vec3 chunkOrigin(chunk.chunkX * voxel::Chunk::CHUNK_SIZE, 0.0f, chunk.chunkZ * voxel::Chunk::CHUNK_SIZE);

        for (int blockType : meshBlockTypes)
        {
            our::Material *material = getMaterialForBlockType(blockType);
            if (!material)
                continue;

            our::Mesh *chunkMesh = our::mesh_utils::buildChunkMesh(chunk, terrainWorld, blockType);
            if (!chunkMesh)
                continue;

            our::Entity *chunkEntity = engineWorld.add();
            chunkEntity->localTransform.position = chunkOrigin;

            auto *meshRenderer = chunkEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = chunkMesh;
            meshRenderer->material = material;

            renderGroup.entities.push_back(chunkEntity);
            renderGroup.meshes.push_back(chunkMesh);
        }

        // Handle GRASS blocks manually to support different textures per face
        auto addGrassFaces = [&](our::mesh_utils::FaceCategory faceCat, const char *matName)
        {
            our::Material *material = our::AssetLoader<our::Material>::get(matName);
            if (!material)
                return;
            our::Mesh *chunkMesh = our::mesh_utils::buildChunkMesh(chunk, terrainWorld, voxel::GRASS, faceCat);
            if (!chunkMesh)
                return;

            our::Entity *chunkEntity = engineWorld.add();
            chunkEntity->localTransform.position = chunkOrigin;

            auto *meshRenderer = chunkEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = chunkMesh;
            meshRenderer->material = material;

            renderGroup.entities.push_back(chunkEntity);
            renderGroup.meshes.push_back(chunkMesh);
        };

        addGrassFaces(our::mesh_utils::FaceCategory::TOP, "grass-top");
        addGrassFaces(our::mesh_utils::FaceCategory::BOTTOM, "dirt");
        addGrassFaces(our::mesh_utils::FaceCategory::SIDES, "grass-side");

        // Spawn NPCs for this chunk
        spawnNPCsInChunk(chunk.chunkX, chunk.chunkZ);

        chunkRenderGroups[chunkKey] = std::move(renderGroup);
    }

    void rebuildMesh()
    {
        int chunksUpdatedThisFrame = 0;
        bool stillHasDirtyChunks = false;

        for (auto &entry : terrainWorld.activeChunks)
        {
            voxel::Chunk &chunk = entry.second;
            bool hasGroup = chunkRenderGroups.count(entry.first);
            if (!chunk.isDirty && hasGroup)
                continue;

            if (chunksUpdatedThisFrame == 0)
            {
                clearChunkRenderGroup(entry.first);
                buildChunkRenderGroup(entry.first, chunk);
                chunk.isDirty = false;
                chunksUpdatedThisFrame++;
            }
            else
            {
                stillHasDirtyChunks = true;
                break;
            }
        }
        engineWorld.deleteMarkedEntities();
        terrainMeshDirty = stillHasDirtyChunks;
    }

    // --- Streaming Logic ---
    void updateLoadedChunks(int centerChunkX, int centerChunkZ)
    {
        bool chunkSetChanged = false;
        for (int dz = -chunkLoadRadius; dz <= chunkLoadRadius; ++dz)
        {
            for (int dx = -chunkLoadRadius; dx <= chunkLoadRadius; ++dx)
            {
                int cx = centerChunkX + dx, cz = centerChunkZ + dz;
                std::string key = std::to_string(cx) + "_" + std::to_string(cz);
                if (terrainWorld.activeChunks.find(key) == terrainWorld.activeChunks.end())
                {
                    terrainWorld.generateChunk(cx, cz);
                    chunkSetChanged = true;

                    // Spawn NPCs in new chunk
                    spawnNPCsInChunk(cx, cz);
                }
            }
        }

        for (auto it = terrainWorld.activeChunks.begin(); it != terrainWorld.activeChunks.end();)
        {
            if (std::abs(it->second.chunkX - centerChunkX) > chunkUnloadRadius ||
                std::abs(it->second.chunkZ - centerChunkZ) > chunkUnloadRadius)
            {
                clearChunkRenderGroup(it->first);
                it = terrainWorld.activeChunks.erase(it);
                chunkSetChanged = true;
            }
            else
                ++it;
        }
        if (chunkSetChanged)
            terrainMeshDirty = true;
    }

    void streamChunksAroundPlayer(const glm::vec3 &position)
    {
        int px = worldToChunkCoordinate(position.x);
        int pz = worldToChunkCoordinate(position.z);
        if (px != currentCenterChunkX || pz != currentCenterChunkZ)
        {
            currentCenterChunkX = px;
            currentCenterChunkZ = pz;
            updateLoadedChunks(px, pz);
        }
    }

    // --- UI Drawing (Combined) ---
    // Draw an item icon for any item ID (block texture or tool color)
    static void drawItemIcon(ImDrawList *dl, int itemId, const ImVec2 &iconMin, const ImVec2 &iconMax)
    {
        if (itemId == 0) return;
        const ImU32 outline = IM_COL32(18, 18, 22, 220);
        our::Texture2D *tex = nullptr;
        const char *texName = getItemTextureName(itemId);
        if (texName) tex = our::AssetLoader<our::Texture2D>::get(texName);

        if (tex)
            dl->AddImage((ImTextureID)(intptr_t)tex->getOpenGLName(), iconMin, iconMax, ImVec2(0, 1), ImVec2(1, 0));
        else {
            ImU32 fc = getItemFallbackColor(itemId);
            dl->AddRectFilled(iconMin, iconMax, fc, 4.0f);
            // For tools, draw a small label
            if (voxel::isToolItem(itemId)) {
                const char* name = voxel::getItemName(itemId);
                if (name && name[0]) {
                    ImVec2 ts = ImGui::CalcTextSize(name);
                    float cx = iconMin.x + (iconMax.x - iconMin.x - ts.x) * 0.5f;
                    float cy = iconMin.y + (iconMax.y - iconMin.y - ts.y) * 0.5f;
                    dl->AddText(ImVec2(cx, cy), IM_COL32_WHITE, name);
                }
            }
        }
        dl->AddRect(iconMin, iconMax, outline, 4.0f, ImDrawCornerFlags_All, 1.25f);
    }

    void awardLevel2KillXP(our::PlayerComponent *player)
    {
        if (!player || player->level != 2)
            return;

        player->currentXP += 1.0f / 4.0f;
        if (player->currentXP >= 1.0f)
        {
            player->level = 3;
            player->currentXP = 0.0f;
            levelUpNotificationTime = 3.0f;
            displayedLevel = 3;
            our::AudioSystem::playSound("assets/sounds/levelup.wav");
        }
    }

    void killNPCAndAwardMeat(our::Entity *npcEntity, our::PlayerComponent *player)
    {
        if (!npcEntity || !player)
            return;
        auto *killable = npcEntity->getComponent<our::KillableNPCComponent>();
        if (killable)
        {
            // If a bee is hit, make ALL bees angry
            if (killable->npcType == "bee")
            {
                beesAngry = true;
            }

            if (killable->npcType == "chest")
            {
                // Increase health for chests, capped at max health
                player->health = std::min(player->health + killable->foodReward, player->maxHealth);
            }
            else
            {
                // Original behavior for other NPCs (increase meat count)
                player->meatCount += killable->foodReward;

            }
        }
        engineWorld.markForRemoval(npcEntity);
    }

    bool damageEnemyAndAwardXP(our::Entity *enemyEntity, our::PlayerComponent *player)
    {
        if (!enemyEntity || !player)
            return false;

        auto *enemy = enemyEntity->getComponent<our::EnemyComponent>();
        if (!enemy || enemy->state == our::EnemyState::DEAD)
            return false;

        enemy->health -= 10.0f;
        enemy->hurtFlashTimer = enemy->hurtFlashDuration;

        if (enemy->health <= 0.0f)
        {
            enemy->health = 0.0f;
            enemy->state = our::EnemyState::DEAD;
            enemy->deadTimer = 0.0f;
            enemy->velocity = glm::vec3(0.0f);
            awardLevel2KillXP(player);
            our::AudioSystem::playSound("assets/sounds/kill.mp3");
        }
        else
        {
            our::AudioSystem::playSound("assets/sounds/Hit.wav");
        }

        return true;
    }

    our::Entity *findHitNPC(const glm::vec3 &camPos, const glm::vec3 &camDir, float maxDist)
    {
        float closestDist = maxDist;
        our::Entity *closestNPC = nullptr;

        for (auto entity : engineWorld.getEntities())
        {
            if (!entity)
                continue;
            auto *killable = entity->getComponent<our::KillableNPCComponent>();
            if (!killable)
                continue;

            if (killable->npcType == "cat" || killable->npcType == "frog")
                continue;

            glm::vec3 npcPos = entity->localTransform.position;
            glm::vec3 toNPC = npcPos - camPos;
            float t = glm::dot(toNPC, camDir);
            if (t < 0.0f)
                continue;

            glm::vec3 closestPoint = camPos + camDir * t;
            glm::vec3 diff = closestPoint - npcPos;
            float distSq = glm::dot(diff, diff);
            float radius = 0.5f;

            if (distSq < radius * radius && t < closestDist)
            {
                closestDist = t;
                closestNPC = entity;
            }
        }
        return closestNPC;
    }

    our::Entity *findHitEnemy(const glm::vec3 &camPos, const glm::vec3 &camDir, float maxDist)
    {
        float closestDist = maxDist;
        our::Entity *closestEnemy = nullptr;

        for (auto entity : engineWorld.getEntities())
        {
            if (!entity)
                continue;

            auto *enemy = entity->getComponent<our::EnemyComponent>();
            if (!enemy || enemy->state == our::EnemyState::DEAD)
                continue;

            glm::vec3 enemyCenter = entity->localTransform.position + enemy->colliderCenter;
            glm::vec3 toEnemy = enemyCenter - camPos;
            float t = glm::dot(toEnemy, camDir);
            if (t < 0.0f || t > closestDist)
                continue;

            glm::vec3 closestPoint = camPos + camDir * t;
            glm::vec3 diff = closestPoint - enemyCenter;
            float radius = std::max(enemy->colliderHalfSize.x, std::max(enemy->colliderHalfSize.y, enemy->colliderHalfSize.z)) + 0.2f;

            if (glm::dot(diff, diff) <= radius * radius)
            {
                closestDist = t;
                closestEnemy = entity;
            }
        }

        return closestEnemy;
    }

    void updateMeatDecay(our::PlayerComponent *player, float deltaTime)
    {
        if (!player)
            return;
        player->meatDecayTimer += deltaTime;
        if (player->meatDecayTimer >= 20.0f)
        {
            player->meatDecayTimer = 0.0f;
            if (player->meatCount > 0)
            {
                player->meatCount = std::max(0, player->meatCount - 1);
            }
            else
            {
                player->health = std::max(0.0f, player->health - 10.0f);
                our::AudioSystem::playSound("assets/sounds/life_loss.mp3");
                player->shakeTimer = 0.5f;
                player->shakeIntensity = 0.2f;
                if (player->health <= 0.0f)
                {
                    player->isAlive = false;
                    player->gameState = our::GameState::LOSE;
                }
            }
        }
    }

    bool isValidNPCPosition(our::Entity *entity, const glm::vec3 &newPos)
    {
        auto *aabb = entity->getComponent<our::AABBColliderComponent>();
        if (!aabb)
            return true;

        glm::vec3 min = aabb->getMinCorner(newPos);
        glm::vec3 max = aabb->getMaxCorner(newPos);

        int minX = static_cast<int>(std::floor(min.x));
        int minY = static_cast<int>(std::floor(min.y));
        int minZ = static_cast<int>(std::floor(min.z));
        int maxX = static_cast<int>(std::floor(max.x));
        int maxY = static_cast<int>(std::floor(max.y));
        int maxZ = static_cast<int>(std::floor(max.z));

        for (int x = minX; x <= maxX; ++x)
        {
            for (int y = minY; y <= maxY; ++y)
            {
                for (int z = minZ; z <= maxZ; ++z)
                {
                    int block = terrainWorld.getBlock(x, y, z);
                    if (block != 0)
                        return false;
                }
            }
        }
        return true;
    }

    void spawnNPCs()
    {
        if (npcTemplates.empty())
            return;

        // Use player position directly
        our::Entity *playerEntity = findPlayerEntity();
        if (!playerEntity)
            return;

        glm::vec3 playerPos = playerEntity->localTransform.position;
        int cx = (int)std::floor(playerPos.x / (float)voxel::Chunk::CHUNK_SIZE);
        int cz = (int)std::floor(playerPos.z / (float)voxel::Chunk::CHUNK_SIZE);

        spawnNPCsInChunk(cx, cz);
    }

    void spawnNPCsInChunk(int cx, int cz)
    {
        std::string chunkKey = std::to_string(cx) + "_" + std::to_string(cz);

        // Already spawned NPCs in this chunk
        if (chunkRenderGroups.find(chunkKey) != chunkRenderGroups.end() &&
            !chunkRenderGroups[chunkKey].npcs.empty())
            return;

        std::random_device rd;
        std::mt19937 gen(rd());
        // Increase NPCs per chunk to make distribution more dense
        std::uniform_int_distribution<> npcCountDist(0, 1);
        std::uniform_int_distribution<> templateDist(0, static_cast<int>(npcTemplates.size()) - 1);

        int numNPCs = npcCountDist(gen);

        for (int i = 0; i < numNPCs; ++i)
        {
            std::uniform_int_distribution<> localDist(0, voxel::Chunk::CHUNK_SIZE - 1);
            int lx = localDist(gen);
            int ly = terrainWorld.height - 1;
            int lz = localDist(gen);

            while (ly >= 0 && terrainWorld.getBlock(cx * voxel::Chunk::CHUNK_SIZE + lx, ly, cz * voxel::Chunk::CHUNK_SIZE + lz) == 0)
            {
                ly--;
            }
            if (ly < 0)
                continue;

            // Pick a template
            const auto &templateData = npcTemplates[templateDist(gen)];

            // Check if water is nearby (within 3 blocks)
            bool nearWater = false;
            int checkX = cx * voxel::Chunk::CHUNK_SIZE + lx;
            int checkZ = cz * voxel::Chunk::CHUNK_SIZE + lz;
            for (int dx = -3; dx <= 3 && !nearWater; dx++)
            {
                for (int dz = -3; dz <= 3 && !nearWater; dz++)
                {
                    for (int dy = -2; dy <= 2; dy++)
                    {
                        if (terrainWorld.getBlock(checkX + dx, ly + dy, checkZ + dz) == voxel::WATER)
                        {
                            nearWater = true;
                            break;
                        }
                    }
                }
            }

            int blockUnder = terrainWorld.getBlock(checkX, ly, checkZ);
            if (blockUnder == voxel::WATER)
                continue; // no one spawns IN water

            if (templateData.npcType == "frog") {
                if (!nearWater) continue; // frogs MUST spawn near water
            } else {
                if (nearWater) continue; // other NPCs avoid water
            }

            float worldX = cx * voxel::Chunk::CHUNK_SIZE + lx + 0.5f;
            float worldZ = cz * voxel::Chunk::CHUNK_SIZE + lz + 0.5f;

            // Compute Y so the collider bottom sits slightly above the block top
            float blockTopY = ly + 1.0f;
            float colliderBottomLocal = templateData.aabbCenter.y - templateData.aabbHalfSize.y;
            float desiredEntityY = blockTopY - colliderBottomLocal + 0.3f; // large offset for mesh geometry

            our::Entity *npcEntity = engineWorld.add();
            npcEntity->localTransform.position = glm::vec3(worldX, desiredEntityY, worldZ);
            npcEntity->localTransform.scale = glm::vec3(templateData.scaleX, templateData.scaleY, templateData.scaleZ);

            auto *meshRenderer = npcEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = our::AssetLoader<our::Mesh>::get(templateData.meshName);
            meshRenderer->material = our::AssetLoader<our::Material>::get(templateData.matName);

            auto *aabb = npcEntity->addComponent<our::AABBColliderComponent>();
            aabb->center = templateData.aabbCenter;
            aabb->halfSize = templateData.aabbHalfSize;
            printf("[SPAWN] NPC AABB center=(%.2f,%.2f,%.2f) halfSize=(%.2f,%.2f,%.2f)\n",
                   aabb->center.x, aabb->center.y, aabb->center.z,
                   aabb->halfSize.x, aabb->halfSize.y, aabb->halfSize.z);

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

            movement->isFlying = templateData.isFlying;

            auto *killable = npcEntity->addComponent<our::KillableNPCComponent>();
            killable->foodReward = templateData.foodReward;
            killable->npcType = templateData.npcType;

            // Store NPC spawn data in chunk render group
            std::string chunkKey = std::to_string(cx) + "_" + std::to_string(cz);
            NPCSpawnData spawnData;
            spawnData.chunkX = cx;
            spawnData.chunkZ = cz;
            spawnData.localX = lx;
            spawnData.localY = ly;
            spawnData.localZ = lz;
            spawnData.scaleX = templateData.scaleX;
            spawnData.scaleY = templateData.scaleY;
            spawnData.scaleZ = templateData.scaleZ;
            spawnData.meshName = templateData.meshName;
            spawnData.matName = templateData.matName;
            spawnData.foodReward = templateData.foodReward;
            spawnData.npcType = templateData.npcType;
            spawnData.speed = templateData.speed;
            spawnData.moveRadius = templateData.moveRadius;
            spawnData.waitTime = templateData.waitTime;
            spawnData.movementType = templateData.movementType;
            spawnData.aabbCenter = templateData.aabbCenter;
            spawnData.aabbHalfSize = templateData.aabbHalfSize;
            chunkRenderGroups[chunkKey].npcs.push_back(spawnData);
        }
    }

    // --- State Overrides ---
    void onInitialize() override
    {
        auto &config = getApp()->getConfig()["scene"];
        if (config.contains("assets"))
            our::deserializeAllAssets(config["assets"]);
        if (config.contains("world"))
            engineWorld.deserialize(config["world"]);
        if (config.contains("terrain"))
        {
            auto &terrainConfig = config["terrain"];
            terrainWorld.deserialize(terrainConfig);
            chunkLoadRadius = terrainConfig.value("chunk-load-radius", chunkLoadRadius);
            chunkUnloadRadius = std::max(terrainConfig.value("chunk-unload-radius", chunkLoadRadius + 1), chunkLoadRadius);
        }

        if (config.contains("npcTypes") && config["npcTypes"].is_array())
        {
            printf("[INIT] Found npcTypes array with %zu entries\n", config["npcTypes"].size());
            for (const auto &npcTypeJson : config["npcTypes"])
            {
                if (!npcTypeJson.is_object())
                    continue;

                NPCSpawnData templateData;
                templateData.meshName = npcTypeJson.value("mesh", "cube");
                templateData.matName = npcTypeJson.value("material", "default");

                if (npcTypeJson.contains("scale") && npcTypeJson["scale"].is_array() && npcTypeJson["scale"].size() >= 3)
                {
                    templateData.scaleX = npcTypeJson["scale"][0].get<float>();
                    templateData.scaleY = npcTypeJson["scale"][1].get<float>();
                    templateData.scaleZ = npcTypeJson["scale"][2].get<float>();
                }
                else
                {
                    templateData.scaleX = templateData.scaleY = templateData.scaleZ = 0.5f;
                }

                if (npcTypeJson.contains("components") && npcTypeJson["components"].is_object())
                {
                    const auto &comp = npcTypeJson["components"];
                    templateData.foodReward = comp.value("foodReward", 1);
                    templateData.npcType = comp.value("npcType", "default");

                    // Read AABB from components if present
                    if (comp.contains("AABBCollider") && comp["AABBCollider"].is_object())
                    {
                        const auto &aabbJson = comp["AABBCollider"];
                        templateData.aabbCenter = glm::vec3(0.0f);
                        templateData.aabbHalfSize = glm::vec3(0.2f);
                        if (aabbJson.contains("center") && aabbJson["center"].is_array() && aabbJson["center"].size() >= 3)
                        {
                            templateData.aabbCenter = glm::vec3(
                                aabbJson["center"][0].get<float>(),
                                aabbJson["center"][1].get<float>(),
                                aabbJson["center"][2].get<float>());
                        }
                        if (aabbJson.contains("halfSize") && aabbJson["halfSize"].is_array() && aabbJson["halfSize"].size() >= 3)
                        {
                            templateData.aabbHalfSize = glm::vec3(
                                aabbJson["halfSize"][0].get<float>(),
                                aabbJson["halfSize"][1].get<float>(),
                                aabbJson["halfSize"][2].get<float>());
                        }
                        printf("[INIT] NPC %s AABB center=(%.2f,%.2f,%.2f) halfSize=(%.2f,%.2f,%.2f)\n",
                               templateData.npcType.c_str(),
                               templateData.aabbCenter.x, templateData.aabbCenter.y, templateData.aabbCenter.z,
                               templateData.aabbHalfSize.x, templateData.aabbHalfSize.y, templateData.aabbHalfSize.z);
                    }
                }
                else
                {
                    templateData.foodReward = 1;
                    templateData.npcType = "default";
                }

                if (npcTypeJson.contains("movement") && npcTypeJson["movement"].is_object())
                {
                    const auto &mov = npcTypeJson["movement"];
                    templateData.speed = mov.value("speed", 1.0f);
                    templateData.moveRadius = mov.value("moveRadius", 5.0f);
                    templateData.waitTime = mov.value("waitTime", 2.0f);
                    templateData.movementType = mov.value("movementType", "random_walk");
                    templateData.isFlying = mov.value("isFlying", false);
                }
                else
                {
                    templateData.speed = 1.0f;
                    templateData.moveRadius = 5.0f;
                    templateData.waitTime = 2.0f;
                    templateData.movementType = "random_walk";
                }
                npcTemplates.push_back(templateData);
                printf("[INIT] Added NPC template: mesh=%s type=%s movementType=%s\n",
                       templateData.meshName.c_str(), templateData.npcType.c_str(),
                       templateData.movementType.c_str());
            }
            printf("[INIT] Finished parsing npcTypes. Total templates: %zu\n", npcTemplates.size());
        }
        else
        {
            printf("[INIT] No npcTypes found in config\n");
        }

        cameraController.enter(getApp());
        playerController.enter(getApp());
        timeSystem.initialize(&engineWorld);
        renderer.initialize(getApp()->getFrameBufferSize(), config["renderer"]);

        highlightEntity = engineWorld.add();
        auto *meshRenderer = highlightEntity->addComponent<our::MeshRendererComponent>();
        meshRenderer->mesh = our::AssetLoader<our::Mesh>::get("cube");
        meshRenderer->material = our::AssetLoader<our::Material>::get("highlight-fill");
        highlightEntity->localTransform.scale = glm::vec3(0.502f); // Slightly larger than a block

        highlightEdgesEntity = engineWorld.add();
        auto *edgesRenderer = highlightEdgesEntity->addComponent<our::MeshRendererComponent>();
        highlightEdgesMesh = our::mesh_utils::cubeEdges();
        edgesRenderer->mesh = highlightEdgesMesh;
        edgesRenderer->material = our::AssetLoader<our::Material>::get("wireframe");
        highlightEdgesEntity->localTransform.scale = glm::vec3(0.505f); // Slightly larger than the fill to avoid z-fighting

        blockInteraction.initialize(&engineWorld);
        enemySystem.initialize();
        our::AudioSystem::startLoopingSound("water_ambient", "assets/sounds/water_flowing.wav");
        our::AudioSystem::setLoopingSoundVolume("water_ambient", 0.0f);

        our::Entity *playerEntity = findPlayerEntity();
        if (playerEntity)
        {
            streamChunksAroundPlayer(playerEntity->localTransform.position);
            // Spawn player on top of terrain
            auto &pos = playerEntity->localTransform.position;
            for (int y = terrainWorld.height - 1; y >= 0; --y)
            {
                if (terrainWorld.getBlock(pos.x, y, pos.z) != 0)
                {
                    pos.y = y + 2.5f;
                    break;
                }
            }
        }
        if (terrainMeshDirty)
            rebuildMesh();

        // Initialize NPC Movement System with player entity and terrain
        npcMovementSystem.initialize(playerEntity, &terrainWorld);
        printf("[INIT] NPCMovementSystem initialized with playerEntity=%p and terrainWorld=%p\n",
               (void *)playerEntity, (void *)&terrainWorld);

        spawnNPCs();
        for (auto entity : engineWorld.getEntities())
        {
            if (!entity)
                continue;
            auto *npcMove = entity->getComponent<our::NPCMovementComponent>();
            if (npcMove)
                npcMove->startPosition = entity->localTransform.position;
        }
    }

    void drawGameOverlay() {
        our::Entity *playerEntity = findPlayerEntity();
        if (!playerEntity) return;
        auto *player = playerEntity->getComponent<our::PlayerComponent>();
        if (!player) return;
        if (player->gameState == our::GameState::PLAYING) return;

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

        // Full-screen dim overlay
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("##gameoverdim", nullptr, bgFlags);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({0,0}, io.DisplaySize, IM_COL32(0,0,0,160));
        ImGui::End();

        // Centered panel
        ImVec2 panelSize(520, 300);
        ImGui::SetNextWindowPos(ImVec2(center.x - panelSize.x*0.5f, center.y - panelSize.y*0.5f));
        ImGui::SetNextWindowSize(panelSize);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        if (player->gameState == our::GameState::WIN) {
            ImGui::SetNextWindowBgAlpha(0.92f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.25f, 0.05f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.2f,  0.8f,  0.2f, 1.0f));
        } else {
            ImGui::SetNextWindowBgAlpha(0.92f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.25f, 0.05f, 0.05f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.9f,  0.2f,  0.2f, 1.0f));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   12.0f);

        ImGui::Begin("##gameover", nullptr, flags);

        // Title
        ImGui::SetWindowFontScale(2.8f);
        const char* title = (player->gameState == our::GameState::WIN) ? "YOU WIN!" : "GAME OVER";
        ImVec4 titleColor = (player->gameState == our::GameState::WIN) ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::SetCursorPosX((panelSize.x - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::TextColored(titleColor, "%s", title);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Spacing();

        // Stats
        ImGui::SetWindowFontScale(1.3f);
        ImGui::SetCursorPosX(40.0f);
        if (player->gameState == our::GameState::WIN) {
            ImGui::TextColored({0.9f,0.9f,0.9f,1.0f}, "Resources Collected: %d / %d", player->resourcesCollected, player->resourcesRequired);
        } else {
            ImGui::TextColored({0.9f,0.9f,0.9f,1.0f}, "You were defeated!");
        }
        ImGui::Spacing();
        ImGui::SetCursorPosX(40.0f);
        ImGui::TextColored({0.85f,0.85f,0.5f,1.0f}, "Press  R  to restart");
        ImGui::Spacing();
        ImGui::SetCursorPosX(40.0f);
        ImGui::TextColored({0.7f,0.7f,0.7f,1.0f}, "Press  ESC  to return to the main menu");
        ImGui::SetWindowFontScale(1.0f);

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

    void onImmediateGui() override
    {
        our::Entity *playerEntity = findPlayerEntity();
        if (!playerEntity)
            return;

        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        our::PlayerComponent *player = playerEntity->getComponent<our::PlayerComponent>();
        // Draw inventory
        if (isInventoryOpen && player)
        {
            ImVec2 windowSize(420, 380);
            ImGui::SetNextWindowPos(ImVec2((displaySize.x - windowSize.x) * 0.5f, (displaySize.y - windowSize.y) * 0.5f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.76f, 0.76f, 0.76f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));

            ImGui::Begin("Inventory", &isInventoryOpen, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
            ImDrawList *invDl = ImGui::GetWindowDrawList();
            constexpr float slotSz = 36.0f, iconPad = 4.0f;

            // ==========================================
            // 1. Crafting Section (2x2 + Output)
            // ==========================================
            ImGui::SetCursorPos(ImVec2(130, 20));
            ImGui::BeginGroup();
            ImGui::Text("Crafting");
            for (int r = 0; r < 2; r++)
            {
                for (int c = 0; c < 2; c++)
                {
                    int idx = r * 2 + c;
                    auto &gridSlot = player->craftingGrid[idx];
                    std::string btnId = "##craft" + std::to_string(idx);
                    ImVec2 sp = ImGui::GetCursorScreenPos();
                    bool lClick = ImGui::Button(btnId.c_str(), ImVec2(slotSz, slotSz));
                    bool rClick = ImGui::IsItemClicked(1);
                    bool shiftClick = lClick && ImGui::GetIO().KeyShift;
                    if (lClick || rClick) {
                        bool singleAction = rClick || shiftClick;
                        handleSlotClick(gridSlot, player, singleAction);
                        checkCraftingRecipe(player);
                    }
                    if (!gridSlot.isEmpty()) {
                        drawItemIcon(invDl, gridSlot.itemId, ImVec2(sp.x+iconPad, sp.y+iconPad), ImVec2(sp.x+slotSz-iconPad, sp.y+slotSz-iconPad));
                        char cnt[12]; std::snprintf(cnt, sizeof(cnt), "%d", gridSlot.count);
                        ImVec2 ts = ImGui::CalcTextSize(cnt);
                        invDl->AddText(ImVec2(sp.x+slotSz-ts.x-2, sp.y+slotSz-ts.y), IM_COL32(50,50,50,255), cnt);
                    }
                    if (c < 1) ImGui::SameLine();
                }
            }
            ImGui::EndGroup();

            ImGui::SameLine(0, 15);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 25);
            ImGui::Text("->");
            ImGui::SameLine(0, 15);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);
            {
                ImVec2 sp = ImGui::GetCursorScreenPos();
                if (ImGui::Button("##craft_out", ImVec2(45, 45))) {
                    performCraft(player);
                }
                if (!player->craftingResult.isEmpty()) {
                    drawItemIcon(invDl, player->craftingResult.itemId, ImVec2(sp.x+4, sp.y+4), ImVec2(sp.x+41, sp.y+41));
                    char cnt[12]; std::snprintf(cnt, sizeof(cnt), "%d", player->craftingResult.count);
                    ImVec2 ts = ImGui::CalcTextSize(cnt);
                    invDl->AddText(ImVec2(sp.x+43-ts.x, sp.y+43-ts.y), IM_COL32(50,50,50,255), cnt);
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // ==========================================
            // 2. Main Inventory Section (3x9)
            // ==========================================
            ImGui::SetCursorPosX(16);
            ImGui::Text("Inventory");

            ImGui::SetCursorPosX(16);
            for (int r = 0; r < 3; r++)
            {
                for (int c = 0; c < 9; c++)
                {
                    int idx = r * 9 + c;
                    auto &invSlot = player->mainInventory[idx];
                    std::string btnId = "##inv" + std::to_string(idx);
                    ImVec2 sp = ImGui::GetCursorScreenPos();
                    bool lClick = ImGui::Button(btnId.c_str(), ImVec2(slotSz, slotSz));
                    bool rClick = ImGui::IsItemClicked(1);
                    bool shiftClick = lClick && ImGui::GetIO().KeyShift;
                    if (lClick || rClick) {
                        bool singleAction = rClick || shiftClick;
                        handleSlotClick(invSlot, player, singleAction);
                    }
                    if (!invSlot.isEmpty()) {
                        drawItemIcon(invDl, invSlot.itemId, ImVec2(sp.x+iconPad, sp.y+iconPad), ImVec2(sp.x+slotSz-iconPad, sp.y+slotSz-iconPad));
                        char cnt[12]; std::snprintf(cnt, sizeof(cnt), "%d", invSlot.count);
                        ImVec2 ts = ImGui::CalcTextSize(cnt);
                        invDl->AddText(ImVec2(sp.x+slotSz-ts.x-2, sp.y+slotSz-ts.y), IM_COL32(50,50,50,255), cnt);
                    }
                    if (c < 8) ImGui::SameLine();
                }
                if (r < 2) ImGui::SetCursorPosX(16);
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // ==========================================
            // 3. Hotbar Section (1x9) in inventory view
            // ==========================================
            ImGui::SetCursorPosX(16);
            for (int c = 0; c < 9; c++)
            {
                auto &hSlot = player->hotbar[c];
                std::string btnId = "##hotbar_inv" + std::to_string(c);
                bool isSel = (player->inventoryHotbarSlot == c);
                if (isSel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.5f, 0.8f, 1.0f));
                ImVec2 sp = ImGui::GetCursorScreenPos();
                bool lClick = ImGui::Button(btnId.c_str(), ImVec2(slotSz, slotSz));
                bool rClick = ImGui::IsItemClicked(1);
                bool shiftClick = lClick && ImGui::GetIO().KeyShift;
                if (lClick || rClick) {
                    bool singleAction = rClick || shiftClick;
                    handleSlotClick(hSlot, player, singleAction);
                }
                if (isSel) ImGui::PopStyleColor();
                if (!hSlot.isEmpty()) {
                    drawItemIcon(invDl, hSlot.itemId, ImVec2(sp.x+iconPad, sp.y+iconPad), ImVec2(sp.x+slotSz-iconPad, sp.y+slotSz-iconPad));
                    char cnt[12]; std::snprintf(cnt, sizeof(cnt), "%d", hSlot.count);
                    ImVec2 ts = ImGui::CalcTextSize(cnt);
                    invDl->AddText(ImVec2(sp.x+slotSz-ts.x-2, sp.y+slotSz-ts.y), IM_COL32(50,50,50,255), cnt);
                }
                if (c < 8) ImGui::SameLine();
            }

            // Draw cursor item following mouse
            if (!player->cursorItem.isEmpty()) {
                ImVec2 mpos = ImGui::GetMousePos();
                drawItemIcon(invDl, player->cursorItem.itemId, ImVec2(mpos.x-16, mpos.y-16), ImVec2(mpos.x+16, mpos.y+16));
                char cnt[12]; std::snprintf(cnt, sizeof(cnt), "%d", player->cursorItem.count);
                invDl->AddText(ImVec2(mpos.x+8, mpos.y+8), IM_COL32_WHITE, cnt);
            }

            ImGui::End();

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
        }
        else
        {
            ImDrawList *drawList = ImGui::GetForegroundDrawList();
            (void)drawList;
        }


        // 1. Draw Hotbar (always visible at bottom)
        if (!player) player = playerEntity->getComponent<our::PlayerComponent>();
        if (player && player->isUnderwater) {
            ImDrawList *bgList = ImGui::GetBackgroundDrawList();
            
            bgList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(10, 40, 120, 180)); 
        }
        if (player)
        {
            ImGuiWindowFlags invFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground;
            constexpr float box = 60.0f, gap = 8.0f, iconSize = 34.0f;
            ImVec2 invSize(kHotbarSlots * box + (kHotbarSlots - 1) * gap + 24.0f, box + 26.0f);
            ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f - invSize.x * 0.5f, displaySize.y - invSize.y - 16.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(invSize, ImGuiCond_Always);
            if (ImGui::Begin("InventoryHotbar", nullptr, invFlags))
            {
                ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
                ImDrawList *dl = ImGui::GetWindowDrawList();
                for (int i = 0; i < kHotbarSlots; i++)
                {
                    if (i > 0)
                        ImGui::SameLine(0.0f, gap);
                    ImGui::PushID(i);
                    const bool selected = (player->inventoryHotbarSlot == i);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImVec2 br(p.x + box, p.y + box);
                    dl->AddRectFilled(p, br, selected ? IM_COL32(55, 85, 130, 230) : IM_COL32(28, 28, 32, 220), 6.0f);
                    // Draw item icon from the hotbar slot
                    if (!player->hotbar[i].isEmpty()) {
                        drawItemIcon(dl, player->hotbar[i].itemId, ImVec2(p.x + (box - iconSize) * 0.5f, p.y + 5.0f), ImVec2(p.x + (box + iconSize) * 0.5f, p.y + 5.0f + iconSize));
                    }
                    dl->AddRect(p, br, selected ? IM_COL32(240, 200, 90, 255) : IM_COL32(90, 90, 98, 255), 6.0f, ImDrawCornerFlags_All, selected ? 2.5f : 1.0f);
                    if (ImGui::InvisibleButton("slot", ImVec2(box, box)))
                        player->inventoryHotbarSlot = i;
                    // Show count
                    int cnt_val = player->hotbar[i].count;
                    if (cnt_val > 0) {
                        char cnt[12];
                        std::snprintf(cnt, sizeof(cnt), "x%d", cnt_val);
                        ImVec2 ts = ImGui::CalcTextSize(cnt);
                        dl->AddText(ImVec2(p.x + (box - ts.x) * 0.5f, p.y + box - ts.y - 4.0f), IM_COL32_WHITE, cnt);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::End();
        }

        // 2 & 3. Draw Health and Meat Bars in one row, centered
        our::Texture2D *heartsTex = our::AssetLoader<our::Texture2D>::get("hearts");
        our::Texture2D *meatTex = our::AssetLoader<our::Texture2D>::get("meats");
        if (heartsTex && meatTex && player)
        {
            ImDrawList *barDl = ImGui::GetForegroundDrawList();
            float heartSize = 28.0f;
            float heartGap = 2.0f;
            float maxHearts = 10;
            float heartTotalW = maxHearts * heartSize + (maxHearts - 1) * heartGap;

            float meatSize = 28.0f;
            float meatGap = 2.0f;
            float maxMeat = player->meatMax;
            float meatTotalW = maxMeat * meatSize + (maxMeat - 1) * meatGap;

            float barSpacing = 12.0f; // Space between heart and meat bars
            float bothTotalW = heartTotalW + meatTotalW + barSpacing;

            float startY = displaySize.y - 60.0f - 26.0f - 16.0f - heartSize - 8.0f; // Position above hotbar
            float startX = displaySize.x * 0.5f - bothTotalW * 0.5f;

            // Draw Hearts Bar
            ImTextureID heartsTexID = (ImTextureID)(intptr_t)heartsTex->getOpenGLName();
            float texW = 45.0f;
            float texH = 9.0f;

            int fullHearts = static_cast<int>(player->health / 10.0f);
            float partialHeart = (player->health / 10.0f) - fullHearts;


            ImVec2 uvFull0((27.0f ) / texW, (9.0f - 0.5f) / texH);
            ImVec2 uvFull1((36.0f ) / texW, (0.0f + 0.5f) / texH);

            ImVec2 uvEmpty0((0.0f + 0.5f) / texW, (9.0f - 0.5f) / texH);
            ImVec2 uvEmpty1((9.0f - 0.5f) / texW, (0.0f + 0.5f) / texH);

            for (int i = 0; i < maxHearts; ++i)
            {
                ImVec2 pMin(startX + i * (heartSize + heartGap), startY);
                ImVec2 pMax(pMin.x + heartSize, pMin.y + heartSize);
                
                // Draw empty heart as background
                barDl->AddImage(heartsTexID, pMin, pMax, uvEmpty0, uvEmpty1);

                if (i < fullHearts)
                {
                    // Draw full heart on top
                    barDl->AddImage(heartsTexID, pMin, pMax, uvFull0, uvFull1);
                }
                else if (i == fullHearts && partialHeart > 0.0f)
                {
                    // Draw partial full heart over the empty heart
                    ImVec2 splitMax(pMin.x + heartSize * partialHeart, pMax.y);
                    ImVec2 uvFullSplit(uvFull0.x + (uvFull1.x - uvFull0.x) * partialHeart, uvFull1.y);
                    barDl->AddImage(heartsTexID, pMin, splitMax, uvFull0, uvFullSplit);
                }
            }

            // Draw Meat Bar (starts after hearts with spacing)
            ImTextureID meatTexID = (ImTextureID)(intptr_t)meatTex->getOpenGLName();

            ImVec2 mUvFull0((27.0f + 0.5f) / texW, 1.0f);
            ImVec2 mUvFull1((36.0f - 0.5f) / texW, 0.0f);
            ImVec2 mUvEmpty0((0.0f + 0.5f) / texW, 1.0f);
            ImVec2 mUvEmpty1((9.0f - 0.5f) / texW, 0.0f);

            float meatStartX = startX + heartTotalW + barSpacing;
            for (int i = 0; i < maxMeat; ++i)
            {
                ImVec2 pMin(meatStartX + i * (meatSize + meatGap), startY);
                ImVec2 pMax(pMin.x + meatSize, pMin.y + meatSize);
                
                // Draw empty meat as background
                barDl->AddImage(meatTexID, pMin, pMax, mUvEmpty0, mUvEmpty1);

                if (i < player->meatCount)
                {
                    // Draw full meat on top
                    barDl->AddImage(meatTexID, pMin, pMax, mUvFull0, mUvFull1);
                }
            }
        }

        // Draw Oxygen Bubbles
        if (player && player->oxygen < player->maxOxygen)
        {
            ImDrawList *barDl = ImGui::GetForegroundDrawList();
            float heartSize = 28.0f;
            float heartGap = 2.0f;
            float maxHearts = 10;
            float heartTotalW = maxHearts * heartSize + (maxHearts - 1) * heartGap;
            float meatSize = 28.0f;
            float meatGap = 2.0f;
            float maxMeat = player->meatMax;
            float meatTotalW = maxMeat * meatSize + (maxMeat - 1) * meatGap;
            float barSpacing = 12.0f;
            float bothTotalW = heartTotalW + meatTotalW + barSpacing;
            float startX = displaySize.x * 0.5f - bothTotalW * 0.5f;
            float startY = displaySize.y - 60.0f - 26.0f - 16.0f - heartSize - 8.0f;

            float bubbleSize = 16.0f;
            float bubbleGap = 4.0f;
            int maxBubbles = 4;
            float bubbleStartY = startY - bubbleSize - 8.0f;

            int currentBubbles = static_cast<int>(std::ceil((player->oxygen / player->maxOxygen) * maxBubbles));
            
            for (int i = 0; i < currentBubbles; ++i)
            {
                ImVec2 center(startX + i * (bubbleSize + bubbleGap) + bubbleSize * 0.5f, bubbleStartY + bubbleSize * 0.5f);
                barDl->AddCircleFilled(center, bubbleSize * 0.5f, IM_COL32(50, 150, 255, 200));
                barDl->AddCircle(center, bubbleSize * 0.5f, IM_COL32(20, 100, 200, 255), 12, 2.0f);
                // highlight
                barDl->AddCircleFilled(ImVec2(center.x - bubbleSize * 0.15f, center.y - bubbleSize * 0.15f), bubbleSize * 0.15f, IM_COL32(255, 255, 255, 150));
            }
        }

        // 4. Draw Crosshair
        ImDrawList *drawList = ImGui::GetForegroundDrawList();
        ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
        drawList->AddLine(ImVec2(center.x - 8, center.y), ImVec2(center.x + 8, center.y), IM_COL32(255, 255, 255, 220), 2.0f);
        drawList->AddLine(ImVec2(center.x, center.y - 8), ImVec2(center.x, center.y + 8), IM_COL32(255, 255, 255, 220), 2.0f);

        // 5. Draw XP Bar (top right)
        if (player)
        {
            float xpBarWidth = 200.0f;
            float xpBarHeight = 16.0f;
            float xpBarX = displaySize.x - xpBarWidth - 16.0f;
            float xpBarY = 16.0f;

            float xpProgress = player->currentXP;
            if (xpProgress > 1.0f)
                xpProgress = 1.0f;

            ImU32 bgColor = IM_COL32(30, 30, 35, 220);
            ImU32 fillColor = IM_COL32(100, 200, 255, 255);
            if (player->level == 3)
                fillColor = IM_COL32(0, 200, 150, 255);

            drawList->AddRectFilled(ImVec2(xpBarX, xpBarY), ImVec2(xpBarX + xpBarWidth, xpBarY + xpBarHeight), bgColor, 4.0f);
            drawList->AddRectFilled(ImVec2(xpBarX, xpBarY), ImVec2(xpBarX + xpBarWidth * xpProgress, xpBarY + xpBarHeight), fillColor, 4.0f);
            drawList->AddRect(ImVec2(xpBarX, xpBarY), ImVec2(xpBarX + xpBarWidth, xpBarY + xpBarHeight), IM_COL32(180, 180, 190, 255), 4.0f, ImDrawCornerFlags_All, 1.5f);

            char levelText[32];
            std::snprintf(levelText, sizeof(levelText), "Lv.%d", player->level);
            ImVec2 textSize = ImGui::CalcTextSize(levelText);
            drawList->AddText(ImVec2(xpBarX + xpBarWidth * 0.5f - textSize.x * 0.5f, xpBarY + xpBarHeight * 0.5f - textSize.y * 0.5f), IM_COL32_WHITE, levelText);
        }

        // Level Target Popup Notification
        if (levelTargetPopupTime > 0.0f && currentLevelTarget > 0)
        {
            ImDrawList *drawList = ImGui::GetForegroundDrawList();
            
            float offsetY = 0.0f;
            if (levelTargetPopupTime > 4.5f) {
                // Slide in from top
                offsetY = -150.0f * (levelTargetPopupTime - 4.5f) / 0.5f;
            } else if (levelTargetPopupTime < 0.5f) {
                // Slide out to top
                offsetY = -150.0f * (0.5f - levelTargetPopupTime) / 0.5f;
            }

            ImVec2 popupPos(16.0f, 16.0f + offsetY);
            ImVec2 popupSize(280.0f, 68.0f);
            
            ImU32 bgColor = IM_COL32(33, 33, 33, 240);
            ImU32 borderColor = IM_COL32(90, 100, 110, 255);
            drawList->AddRectFilled(popupPos, ImVec2(popupPos.x + popupSize.x, popupPos.y + popupSize.y), bgColor, 8.0f);
            drawList->AddRect(popupPos, ImVec2(popupPos.x + popupSize.x, popupPos.y + popupSize.y), borderColor, 8.0f, ImDrawCornerFlags_All, 2.0f);
            
            std::string title = "Level " + std::to_string(currentLevelTarget);
            std::string goal = "";
            our::Texture2D *iconTex = nullptr;

            if (currentLevelTarget == 1) {
                goal = "Goal: Survive 3 days";
                iconTex = our::AssetLoader<our::Texture2D>::get("meats"); 
            } else if (currentLevelTarget == 2) {
                goal = "Goal: Kill 4 enemies";
                iconTex = our::AssetLoader<our::Texture2D>::get("hearts");
            } else if (currentLevelTarget >= 3) {
                goal = "Goal: Get one diamond";
                // diamond block texture could be added here if available
            }
            
            ImVec2 textPos = ImVec2(popupPos.x + 64.0f, popupPos.y + 12.0f);
            ImVec2 iconPosMin = ImVec2(popupPos.x + 16.0f, popupPos.y + 18.0f);
            ImVec2 iconPosMax = ImVec2(popupPos.x + 48.0f, popupPos.y + 50.0f);
            
            if (iconTex) {
                float texW = 45.0f;
                // Specific UVs for hearts/meats sprite sheet
                ImVec2 uv0 = ImVec2((27.0f + 0.5f) / texW, 1.0f);
                ImVec2 uv1 = ImVec2((36.0f - 0.5f) / texW, 0.0f);
                
                ImTextureID texID = (ImTextureID)(intptr_t)iconTex->getOpenGLName();
                drawList->AddImage(texID, iconPosMin, iconPosMax, uv0, uv1);
            } else {
                drawList->AddRectFilled(iconPosMin, iconPosMax, IM_COL32(0, 200, 200, 255), 4.0f);
            }

            ImFont* font = ImGui::GetIO().FontDefault;
            drawList->AddText(font, 18.0f, textPos, IM_COL32(255, 255, 80, 255), title.c_str());
            drawList->AddText(font, 16.0f, ImVec2(textPos.x, textPos.y + 24.0f), IM_COL32(230, 230, 230, 255), goal.c_str());
        }

        // Level Up Notification
        if (levelUpNotificationTime > 0.0f)
        {
            char levelUpText[64];
            std::snprintf(levelUpText, sizeof(levelUpText), "LEVEL UP! Lv.%d", displayedLevel);
            ImFont *font = ImGui::GetIO().FontDefault;
            float baseSize = font ? font->FontSize : 24.0f;
            float scale = 2.5f;
            float pulse = 1.0f + 0.1f * std::sin(levelUpNotificationTime * 8.0f);
            float fontSize = baseSize * scale * pulse;

            ImVec2 textPos = ImVec2(
                (displaySize.x - 280.0f * pulse) * 0.5f,
                (displaySize.y - 60.0f * pulse) * 0.5f);

            drawList->AddText(font, fontSize, ImVec2(textPos.x - 1, textPos.y - 1), IM_COL32(0, 80, 40, 255), levelUpText);
            drawList->AddText(font, fontSize, ImVec2(textPos.x + 1, textPos.y + 1), IM_COL32(0, 80, 40, 255), levelUpText);
            drawList->AddText(font, fontSize, textPos, IM_COL32(50, 255, 100, 255), levelUpText);
        }

        drawGameOverlay();
    }

    void onDraw(double deltaTime) override
    {
        int i = 0;
        our::Entity *playerEntity = findPlayerEntity();
        our::PlayerComponent *player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        
        bool isPlaying = (!player || player->gameState == our::GameState::PLAYING);

        movementSystem.update(&engineWorld, (float)deltaTime);
        if (!isInventoryOpen) {
            playerController.update(&engineWorld, (float)deltaTime);
        } else if (player) {
            player->velocity.x = 0.0f;
            player->velocity.z = 0.0f;
        }
        
        if (!isPlaying && player) {
            player->velocity.x = 0.0f;
            player->velocity.z = 0.0f;
            if (player->velocity.y > 0.0f) {
                player->velocity.y = 0.0f;
            }
        }

        if (playerEntity)
            streamChunksAroundPlayer(playerEntity->localTransform.position);

        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        lightSystem.update(&engineWorld, (float)deltaTime);
        
        if (isPlaying) {
            timeSystem.update(&engineWorld, (float)deltaTime);
            terrainWorld.updateFluids((float)deltaTime, terrainMeshDirty);
            if (playerEntity) {
                bool isNight = timeSystem.getSunElevation() < 0.0f;
                enemySystem.update(&engineWorld, &terrainWorld, playerEntity->localTransform.position, (float)deltaTime, terrainMeshDirty, isNight);
            }


            if (player && player->level > currentLevelTarget) {
                currentLevelTarget = player->level;
                levelTargetPopupTime = 5.0f;
                our::AudioSystem::playSound("assets/sounds/Notification.wav");
            }

            // Update level up notification timer
            if (levelUpNotificationTime > 0.0f)
                levelUpNotificationTime -= (float)deltaTime;

            if (levelTargetPopupTime > 0.0f)
                levelTargetPopupTime -= (float)deltaTime;

            // Level 1: XP increases by 1/3 every day
            if (player && player->level == 1)
            {
                int daysPassed = timeSystem.getDaysPassed();
                if (daysPassed > player->daysSurvived)
                {
                    int daysDiff = daysPassed - player->daysSurvived;
                    player->daysSurvived = daysPassed;
                    player->currentXP += daysDiff * (1.0f / 3.0f);
                    if (player->currentXP >= 1.0f)
                    {
                        player->currentXP = 1.0f;
                        player->level = 2;
                        player->currentXP = 0.0f;
                        levelUpNotificationTime = 3.0f;
                        displayedLevel = 2;
                        our::AudioSystem::playSound("assets/sounds/levelup.wav");
                    }
                }
            }
            blockInteraction.update((float)deltaTime, &engineWorld);
            // Handle water damage
            if (player)
            {
                float targetVolume = player->isUnderwater ? 0.2f : 1.0f;
                our::AudioSystem::setGlobalVolume(targetVolume);

                if (player->isUnderwater)
                {
                    if (player->oxygen > 0.0f) {
                        player->oxygen -= (float)deltaTime;
                    } else {
                        player->waterDamageTimer += (float)deltaTime;
                        while (player->waterDamageTimer >= player->waterDamageInterval)
                        {
                            player->health -= player->waterDamageAmount;
                            our::AudioSystem::playSound("assets/sounds/life_loss.mp3");
                            player->shakeTimer = 0.5f;
                            player->shakeIntensity = 0.2f;
                            player->waterDamageTimer -= player->waterDamageInterval;

                            if (player->health <= 0.0f)
                            {
                                player->health = 0.0f;
                                player->isAlive = false;
                                player->gameState = our::GameState::LOSE;
                                break;
                            }
                        }
                    }
                }
                else
                {
                    player->oxygen = player->maxOxygen;
                    player->waterDamageTimer = 0.0f;
                }

                updateMeatDecay(player, (float)deltaTime);

                // Dynamic ambient sound based on distance to nearest water
                float maxRadius = 10.0f;
                float minDistanceSq = maxRadius * maxRadius;
                bool waterFound = false;

                glm::vec3 pos = playerEntity->localTransform.position;
                int ix = static_cast<int>(std::floor(pos.x));
                int iy = static_cast<int>(std::floor(pos.y));
                int iz = static_cast<int>(std::floor(pos.z));

                for (int dx = -6; dx <= 6; ++dx)
                {
                    for (int dy = -3; dy <= 3; ++dy)
                    {
                        for (int dz = -6; dz <= 6; ++dz)
                        {
                            if (terrainWorld.getBlock(ix + dx, iy + dy, iz + dz) == voxel::WATER)
                            {
                                float distSq = (float)(dx * dx + dy * dy + dz * dz);
                                if (distSq < minDistanceSq)
                                {
                                    minDistanceSq = distSq;
                                    waterFound = true;
                                }
                            }
                        }
                    }
                }

                float volume = 0.0f;
                if (waterFound)
                {
                    float distance = std::sqrt(minDistanceSq);
                    volume = 1.0f - (distance / maxRadius);
                    if (volume < 0.0f)
                        volume = 0.0f;
                }
                our::AudioSystem::setLoopingSoundVolume("water_ambient", volume);
            }

            
            // Update NPC movement using the dedicated system
            npcMovementSystem.update(&engineWorld, (float)deltaTime);
        }

        std::cout << "in play state update" << std::endl;
        auto &keyboard = getApp()->getKeyboard();
        
        if (!isPlaying && player) {
            if (keyboard.justPressed(GLFW_KEY_R)) {
                if (player->gameState == our::GameState::WIN) {
                    std::cout << "in win " << getApp() << std::endl;
                    getApp()->changeState("play");
                    return;
                } else if (player->gameState == our::GameState::LOSE) {
                    std::cout << "in lose " << getApp() << std::endl;
                    player->gameState = our::GameState::PLAYING;
                    player->health = player->maxHealth;
                    player->isAlive = true;
                }
            }
            if (keyboard.justPressed(GLFW_KEY_ESCAPE)) {
                getApp()->changeState("menu");
                return;
            }
        } else {
            // inventory system
            if (keyboard.justPressed(GLFW_KEY_E) && isPlaying)
            {
                isInventoryOpen = !isInventoryOpen;
                if (isInventoryOpen)
                {
                    cameraController.exit();
                    playerController.exit();
                }
                else
                {
                    cameraController.enter(getApp());
                    playerController.enter(getApp());
                }
            }

            if (keyboard.justPressed(GLFW_KEY_ESCAPE)) {
                getApp()->changeState("menu");
                return;
            }
        }

        auto &mouse = getApp()->getMouse();
        if (playerEntity)
        {
            glm::mat4 camMat = playerEntity->localTransform.toMat4();
            glm::vec3 camPos = playerEntity->localTransform.position;
            glm::vec3 camDir = glm::vec3(camMat * glm::vec4(0, 0, -1, 0));

            // Get interaction range from hand component or player component
            float interactRange = 10.0f;  // Default fallback
            for (auto entity : engineWorld.getEntities()) {
                if (!entity) continue;
                auto *handComp = entity->getComponent<our::HandComponent>();
                if (handComp) {
                    interactRange = handComp->interactionRange;
                    break;
                }
            }
            if (player && interactRange == 10.0f) {
                interactRange = player->interactionRange;  // Use player's range if hand not found
            }

            // Hand Animation Logic
            bool triggerHitPulse = false;
            
            if (isPlaying) {
                if (mouse.justPressed(0) || mouse.justPressed(1))
                {
                    triggerHitPulse = true;
                }

                // Highlight Hovered Block
                voxel::RayHit hoverHit = terrainWorld.castRay(camPos, camDir, interactRange);
                if (hoverHit.hit && highlightEntity && highlightEdgesEntity)
                {
                    glm::vec3 pos(hoverHit.x + 0.5f, hoverHit.y + 0.5f, hoverHit.z + 0.5f);
                    highlightEntity->localTransform.position = pos;
                    highlightEdgesEntity->localTransform.position = pos;
                }
                else
                {
                    if (highlightEntity)
                        highlightEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
                    if (highlightEdgesEntity)
                        highlightEdgesEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
                }

                // Break Block / Kill NPC (disabled underwater)
                if (mouse.justPressed(0) && player && !player->isUnderwater)
                {
                    our::Entity *hitEnemy = findHitEnemy(camPos, camDir, interactRange);
                    if (hitEnemy)
                    {
                        damageEnemyAndAwardXP(hitEnemy, player);
                        triggerHitPulse = true;
                    }
                    else if (our::Entity *hitNPC = findHitNPC(camPos, camDir, interactRange))
                    {
                        killNPCAndAwardMeat(hitNPC, player);
                        our::AudioSystem::playSound("assets/sounds/Death.wav");
                    }
                }
                if (mouse.isPressed(0) && player && !player->isUnderwater && hoverHit.hit)
                {
                    int type = terrainWorld.getBlock(hoverHit.x, hoverHit.y, hoverHit.z);
                    auto holdResult = blockInteraction.processHold(hoverHit, type, terrainWorld, &engineWorld, terrainMeshDirty, (float)deltaTime);
                    if (holdResult == BlockInteractionSystem::HoldResult::Broken)
                    {
                        // The block was completely broken
                        our::AudioSystem::playSound("assets/sounds/Hit.wav");
                        if (player)
                            registerCollectedBlock(player, type);
                    }
                    else if (holdResult == BlockInteractionSystem::HoldResult::HitPulse)
                    {
                        triggerHitPulse = true;
                        // The block was hit but not broken
                        if (type == voxel::GRASS)
                            our::AudioSystem::playSound("assets/sounds/Grass.wav");
                        else if (type == voxel::DIRT)
                            our::AudioSystem::playSound("assets/sounds/Dirt.wav");
                        else if (type == voxel::SAND)
                            our::AudioSystem::playSound("assets/sounds/Sand.wav");
                        else if (type == voxel::STONE)
                            our::AudioSystem::playSound("assets/sounds/Stone.wav");
                        else if (type == voxel::Glass)
                            our::AudioSystem::playSound("assets/sounds/Glass.wav");
                        else if (type == voxel::WOOD || type == voxel::LOG)
                            our::AudioSystem::playSound("assets/sounds/Wood.wav");
                        else
                            our::AudioSystem::playSound("assets/sounds/Hit.wav");
                    }
                }
                else if (mouse.justReleased(0) || !mouse.isPressed(0) || !hoverHit.hit || !player || player->isUnderwater)
                {
                    blockInteraction.currentTargetContext = {-1, -1, -1};
                    blockInteraction.accumulatedBreakTime = 0.0f;
                    blockInteraction.particleSpawnTimer = 0.0f;
                }

                if (mouse.justPressed(1) && player && !player->isUnderwater && !isInventoryOpen)
                {
                    int placeType = hotbarBlockType(player);
                    int slotIdx = player->inventoryHotbarSlot;
                    if (placeType > 0 && hoverHit.hit && !player->hotbar[slotIdx].isEmpty() && player->hotbar[slotIdx].count > 0 &&
                        hoverHit.prevY >= 0 && hoverHit.prevY < terrainWorld.height &&
                        terrainWorld.getBlock(hoverHit.prevX, hoverHit.prevY, hoverHit.prevZ) == voxel::AIR)
                    {
                        terrainWorld.placeBlock(hoverHit, placeType);
                        player->hotbar[slotIdx].count--;
                        if (player->hotbar[slotIdx].count <= 0) player->hotbar[slotIdx].clear();
                        terrainMeshDirty = true;
                    }
                }
            } else {
                if (mouse.justReleased(0))
                {
                    blockInteraction.currentTargetContext = {-1, -1, -1};
                    blockInteraction.accumulatedBreakTime = 0.0f;
                }
                if (highlightEntity)
                    highlightEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
                if (highlightEdgesEntity)
                    highlightEdgesEntity->localTransform.position = glm::vec3(0.0f, -1000.0f, 0.0f);
            }

            // Hand Animation & Interaction Update
            our::Entity *handEntity = nullptr;
            our::HandComponent *handComp = nullptr;

            static bool handFoundOnce = false;
            static bool shownEntities = false;

            if (!shownEntities)
            {
                std::cout << "DEBUG: All entities in world:\n";
                for (auto entity : engineWorld.getEntities())
                {
                    if (entity)
                    {
                        std::cout << "  - Name: '" << entity->name << "' Parent: " << (entity->parent ? entity->parent->name : "none") << "\n";
                    }
                }
                shownEntities = true;
            }

            for (auto entity : engineWorld.getEntities())
            {
                if (entity && entity->name == "player_hand")
                {
                    handEntity = entity;
                    handEntity->localTransform.scale = glm::vec3(0.15f, 0.2f, 0.1f);
                    handComp = entity->getComponent<our::HandComponent>();
                    if (!handFoundOnce)
                    {
                        std::cout << "DEBUG: Hand entity found!\n";
                        std::cout << "  Has HandComponent: " << (handComp != nullptr ? "YES" : "NO") << "\n";
                        std::cout << "  Has MeshRenderer: " << (entity->getComponent<our::MeshRendererComponent>() != nullptr ? "YES" : "NO") << "\n";
                        std::cout << "  Local Position: " << entity->localTransform.position.x << ", " << entity->localTransform.position.y << ", " << entity->localTransform.position.z << "\n";
                        handFoundOnce = true;
                    }
                    break;
                }
            }

            if (handEntity && handComp)
            {
                std::cout << "DEBUG: About to update hand. Position before: " << handEntity->localTransform.position.x << ", " << handEntity->localTransform.position.y << ", " << handEntity->localTransform.position.z << "\n";

                // Detect targets within interaction range
                float interactRange = handComp->interactionRange;
                voxel::RayHit localHoverHit = terrainWorld.castRay(camPos, camDir, interactRange);
                our::Entity *localHoverEnemy = findHitEnemy(camPos, camDir, interactRange);
                our::Entity *localHoverNPC = findHitNPC(camPos, camDir, interactRange);

                bool targetInRange = false;
                glm::vec3 targetPos(0.0f);

                if (localHoverEnemy)
                {
                    targetInRange = true;
                    auto *enemy = localHoverEnemy->getComponent<our::EnemyComponent>();
                    targetPos = localHoverEnemy->localTransform.position + (enemy ? enemy->colliderCenter : glm::vec3(0.0f));
                }
                else if (localHoverNPC)
                {
                    targetInRange = true;
                    targetPos = localHoverNPC->localTransform.position;
                }
                else if (localHoverHit.hit)
                {
                    targetInRange = true;
                    targetPos = glm::vec3(localHoverHit.x + 0.5f, localHoverHit.y + 0.5f, localHoverHit.z + 0.5f);
                }

                // Update hand animation using HandSystem
                our::HandSystem::update(
                    handEntity,
                    handComp,
                    camPos,
                    camDir,
                    camMat,
                    deltaTime,
                    triggerHitPulse, // Currently hitting
                    targetInRange,   // Target in range
                    targetPos,       // Target position
                    &terrainWorld    // Terrain for collision avoidance
                );
            }
            else
            {
                std::cout << "DEBUG: handEntity or handComp is null! handEntity=" << (void *)handEntity << " handComp=" << (void *)handComp << "\n";
            }
        } 

        if (terrainMeshDirty)
            rebuildMesh();

        // Water animation: advance timer and periodically rebuild water chunks
        our::mesh_utils::advanceWaterAnim((float)deltaTime);
        waterAnimTimer += (float)deltaTime;
        if (waterAnimTimer >= 0.25f) {
            waterAnimTimer = 0.0f;
            terrainMeshDirty = true;
        }

        // Bee aggression: if bees are angry, damage player when nearby
        if (beesAngry && player && isPlaying && playerEntity) {
            for (auto *e : engineWorld.getEntities()) {
                if (!e) continue;
                auto *killable = e->getComponent<our::KillableNPCComponent>();
                if (!killable || killable->npcType != "bee") continue;
                float dist = glm::length(e->localTransform.position - playerEntity->localTransform.position);
                if (dist < 2.0f) {
                    killable->attackCooldown -= (float)deltaTime;
                    if (killable->attackCooldown <= 0.0f) {
                        player->health -= 2.0f;
                        player->shakeTimer = 0.3f;
                        player->shakeIntensity = 0.15f;
                        killable->attackCooldown = 1.5f;
                        our::AudioSystem::playSound("assets/sounds/Hit.wav");
                        if (player->health <= 0.0f) {
                            player->health = 0.0f;
                            player->isAlive = false;
                            player->gameState = our::GameState::LOSE;
                        }
                    }
                }
            }
        }

        renderer.render(&engineWorld);
        engineWorld.deleteMarkedEntities();
        if (isInventoryOpen)
        {
            renderer.render(&engineWorld);
            return;
        }
    } 

    void onKeyEvent(int key, int scancode, int action, int mods) override
    {
        if (action != GLFW_PRESS)
            return;
        our::Entity *playerEntity = findPlayerEntity();
        our::PlayerComponent *player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        if (!player)
            return;
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
            player->inventoryHotbarSlot = key - GLFW_KEY_1;
    }

    void onScrollEvent(double x, double y) override
    {
        our::Entity *playerEntity = findPlayerEntity();
        our::PlayerComponent *player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        if (!player || y == 0)
            return;
        player->inventoryHotbarSlot = (player->inventoryHotbarSlot + (y > 0 ? -1 : 1) + kHotbarSlots) % kHotbarSlots;
    }

    void onDestroy() override
    {
        our::AudioSystem::stopLoopingSound("water_ambient");
        clearAllChunkRenderGroups();
        engineWorld.deleteMarkedEntities();
        enemySystem.destroy();
        npcMovementSystem.destroy();
        blockInteraction.destroy();
        renderer.destroy();
        cameraController.exit();
        playerController.exit();
        engineWorld.clear();
        
        if (highlightEdgesMesh)
        {
            delete highlightEdgesMesh;
            highlightEdgesMesh = nullptr;
        }

        terrainWorld.activeChunks.clear();
        npcTemplates.clear();
        
        currentCenterChunkX = std::numeric_limits<int>::min();
        currentCenterChunkZ = std::numeric_limits<int>::min();
        terrainMeshDirty = true;
        
        levelUpNotificationTime = 0.0f;
        displayedLevel = 1;
        levelTargetPopupTime = 0.0f;
        currentLevelTarget = 0;
        
        handAnimTime = 0.0f;
        hitAnimTime = 0.0f;
        isHitting = false;
        isInventoryOpen = false;
        beesAngry = false;
        waterAnimTimer = 0.0f;

        our::clearAllAssets();
    }
};

