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
#include <asset-loader.hpp>
#include <texture/texture2d.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <components/player.hpp>
#include <components/aabb-collider.hpp>
#include <components/killable-npc.hpp>
#include <components/camera.hpp>
#include <audio/audio.hpp>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <limits>
#include <string>
#include <algorithm>
#include <chrono>
#include <optional>

class Playstate : public our::State
{
    struct ChunkRenderGroup
    {
        std::vector<our::Entity*> entities;
        std::vector<our::Mesh*> meshes;
    };

    /* --- Core Engine Systems --- */
    voxel::World terrainWorld;
    our::World engineWorld;
    our::ForwardRenderer renderer;
    our::FreeCameraControllerSystem cameraController;
    our::MovementSystem movementSystem;
    our::PlayerControllerSystem playerController;
    our::CollisionSystem collisionSystem;
    our::LightSystem lightSystem;
    our::TimeSystem timeSystem;
    our::BlockInteractionSystem blockInteraction;

    /* --- Selection & Highlight Entities --- */
    our::Entity* highlightEntity = nullptr;      
    our::Entity* highlightEdgesEntity = nullptr; 
    our::Mesh* highlightEdgesMesh = nullptr;     

    /* --- Lighting State --- */
    bool isDaytime = true;

    /* --- Chunk Streaming State --- */
    int chunkLoadRadius = 2;
    int chunkUnloadRadius = 3;
    int currentCenterChunkX = std::numeric_limits<int>::min();
    int currentCenterChunkZ = std::numeric_limits<int>::min();
    bool terrainMeshDirty = true;
    std::unordered_map<std::string, ChunkRenderGroup> chunkRenderGroups;

    /* --- NPC Spawner State --- */
    struct NpcSpawnTypeConfig
    {
        std::string namePrefix, meshName, materialName;
        glm::vec3 scale = glm::vec3(0.55f);
        glm::vec3 colliderHalfSize = glm::vec3(0.4f, 0.5f, 0.4f);
        float moveSpeed = 1.0f;
        float wanderRetargetMinSec = 1.2f;
        float wanderRetargetMaxSec = 3.0f;
    };
    std::vector<NpcSpawnTypeConfig> npcSpawnTypes;
    float npcWorldMargin = 1.0f, npcSpawnMargin = 2.0f, npcMinPlayerDistance = 4.0f;
    int npcSpawnAttempts = 50, npcCountMin = 5, npcCountMax = 20;

    /* --- Inventory Helpers --- */
    static constexpr int kHotbarSlots = 5;

    static int hotbarBlockType(int slot)
    {
        static const int types[kHotbarSlots] = {voxel::GRASS, voxel::DIRT, voxel::WOOD, voxel::STONE, voxel::SAND};
        return types[std::clamp(slot, 0, kHotbarSlots - 1)];
    }

    static int* inventoryCountForType(our::PlayerComponent* player, int blockType)
    {
        switch (blockType)
        {
            case voxel::GRASS: return &player->inventoryGrass;
            case voxel::DIRT:  return &player->inventoryDirt;
            case voxel::WOOD:  return &player->inventoryWood;
            case voxel::STONE: return &player->inventoryStone;
            case voxel::SAND:  return &player->inventorySand;
            default:           return nullptr;
        }
    }

    void registerCollectedBlock(our::PlayerComponent* player, int blockType)
    {
        if (int* slot = inventoryCountForType(player, blockType))
        {
            (*slot)++;
            player->resourcesCollected++;
            if (player->resourcesCollected >= player->resourcesRequired)
                player->gameState = our::GameState::WIN;
        }
    }

    /* --- Utility Methods --- */
    our::Entity* findPlayerEntity()
    {
        for (auto entity : engineWorld.getEntities())
        {
            if (entity->getComponent<our::CameraComponent>() && entity->getComponent<our::PlayerComponent>())
                return entity;
        }
        return nullptr;
    }

    static int worldToChunkCoordinate(float coord) { return static_cast<int>(std::floor(coord / 16.0f)); }

    our::Material* getMaterialForBlockType(int blockType)
    {
        switch (blockType)
        {
            case voxel::STONE: return our::AssetLoader<our::Material>::get("stone");
            case voxel::DIRT:  return our::AssetLoader<our::Material>::get("dirt");
            case voxel::SAND:  return our::AssetLoader<our::Material>::get("sand");
            case voxel::WATER: return our::AssetLoader<our::Material>::get("water");
            case voxel::WOOD:  return our::AssetLoader<our::Material>::get("wood-block");
            default:           return our::AssetLoader<our::Material>::get("default");
        }
    }

    /* --- NPC Logic --- */
    void spawnKillableNpcsForSession()
    {
        if (npcSpawnTypes.empty()) return;
        int count = npcCountMin + (rand() % (npcCountMax - npcCountMin + 1));
        for (int i = 0; i < count; ++i)
        {
            const auto& type = npcSpawnTypes[rand() % npcSpawnTypes.size()];
            our::Entity* e = engineWorld.add();
            e->localTransform.position = glm::vec3(npcSpawnMargin + (rand() % (64 - 4)), 20.0f, npcSpawnMargin + (rand() % (64 - 4)));
            e->localTransform.scale = type.scale;
            auto* mr = e->addComponent<our::MeshRendererComponent>();
            mr->mesh = our::AssetLoader<our::Mesh>::get(type.meshName);
            mr->material = our::AssetLoader<our::Material>::get(type.materialName);
            auto* kn = e->addComponent<our::KillableNpcComponent>();
            kn->moveSpeed = type.moveSpeed;
            auto* col = e->addComponent<our::AABBColliderComponent>();
            col->halfSize = type.colliderHalfSize;
            col->isTrigger = true;
        }
    }

    void updateKillableNpcWander(float dt)
    {
        for (auto* entity : engineWorld.getEntities())
        {
            if (auto* kn = entity->getComponent<our::KillableNpcComponent>())
            {
                kn->wanderTimerSec -= dt;
                if (kn->wanderTimerSec <= 0)
                {
                    float a = (rand() % 100) * 0.0628f;
                    kn->wanderDirXZ = glm::vec2(cos(a), sin(a));
                    kn->wanderTimerSec = 2.0f;
                }
                entity->localTransform.position.x += kn->wanderDirXZ.x * kn->moveSpeed * dt;
                entity->localTransform.position.z += kn->wanderDirXZ.y * kn->moveSpeed * dt;
                entity->localTransform.rotation.y = atan2(kn->wanderDirXZ.x, kn->wanderDirXZ.y);
            }
        }
    }

    /* --- Chunk Rendering Management --- */
    void buildChunkRenderGroup(const std::string& chunkKey, const voxel::Chunk& chunk)
    {
        ChunkRenderGroup renderGroup;
        glm::vec3 chunkOrigin(chunk.chunkX * 16, 0.0f, chunk.chunkZ * 16);

        int types[] = {voxel::STONE, voxel::DIRT, voxel::SAND, voxel::WATER, voxel::WOOD};
        for (int t : types)
        {
            if (auto* mesh = our::mesh_utils::buildChunkMesh(chunk, terrainWorld, t))
            {
                our::Entity* e = engineWorld.add();
                e->localTransform.position = chunkOrigin;
                auto* mr = e->addComponent<our::MeshRendererComponent>();
                mr->mesh = mesh; mr->material = getMaterialForBlockType(t);
                renderGroup.entities.push_back(e); renderGroup.meshes.push_back(mesh);
            }
        }
        
        auto addGrass = [&](our::mesh_utils::FaceCategory cat, const char* mat) {
            if (auto* mesh = our::mesh_utils::buildChunkMesh(chunk, terrainWorld, voxel::GRASS, cat)) {
                our::Entity* e = engineWorld.add(); e->localTransform.position = chunkOrigin;
                auto* mr = e->addComponent<our::MeshRendererComponent>();
                mr->mesh = mesh; mr->material = our::AssetLoader<our::Material>::get(mat);
                renderGroup.entities.push_back(e); renderGroup.meshes.push_back(mesh);
            }
        };
        addGrass(our::mesh_utils::FaceCategory::TOP, "grass-top");
        addGrass(our::mesh_utils::FaceCategory::SIDES, "grass-side");
        addGrass(our::mesh_utils::FaceCategory::BOTTOM, "dirt");

        chunkRenderGroups[chunkKey] = std::move(renderGroup);
    }

    void rebuildMesh()
    {
        int updated = 0;
        for (auto& entry : terrainWorld.activeChunks)
        {
            if (entry.second.isDirty || chunkRenderGroups.find(entry.first) == chunkRenderGroups.end())
            {
                if (updated++ > 0) { terrainMeshDirty = true; break; }
                auto it = chunkRenderGroups.find(entry.first);
                if (it != chunkRenderGroups.end())
                {
                    for (auto* e : it->second.entities) engineWorld.markForRemoval(e);
                    for (auto* m : it->second.meshes) delete m;
                    chunkRenderGroups.erase(it);
                }
                buildChunkRenderGroup(entry.first, entry.second);
                entry.second.isDirty = false;
                terrainMeshDirty = false;
            }
        }
        engineWorld.deleteMarkedEntities();
    }

    void updateLoadedChunks(int cx, int cz)
    {
        for (int dz = -chunkLoadRadius; dz <= chunkLoadRadius; ++dz)
        {
            for (int dx = -chunkLoadRadius; dx <= chunkLoadRadius; ++dx)
            {
                std::string key = std::to_string(cx + dx) + "_" + std::to_string(cz + dz);
                if (terrainWorld.activeChunks.find(key) == terrainWorld.activeChunks.end())
                    terrainWorld.generateChunk(cx + dx, cz + dz);
            }
        }
    }

    /* --- State Overrides --- */
    void onInitialize() override
    {
        auto &config = getApp()->getConfig()["scene"];
        our::deserializeAllAssets(config["assets"]);
        engineWorld.deserialize(config["world"]);
        terrainWorld.deserialize(config["terrain"]);
        
        terrainWorld.generate();
        cameraController.enter(getApp());
        playerController.enter(getApp());
        blockInteraction.initialize(&engineWorld);
        timeSystem.initialize(&engineWorld);
        renderer.initialize(getApp()->getFrameBufferSize(), config["renderer"]);

        highlightEntity = engineWorld.add();
        highlightEntity->addComponent<our::MeshRendererComponent>()->mesh = our::AssetLoader<our::Mesh>::get("cube");
        highlightEntity->getComponent<our::MeshRendererComponent>()->material = our::AssetLoader<our::Material>::get("highlight-fill");
        highlightEntity->localTransform.scale = glm::vec3(0.502f);

        highlightEdgesEntity = engineWorld.add();
        highlightEdgesMesh = our::mesh_utils::cubeEdges();
        highlightEdgesEntity->addComponent<our::MeshRendererComponent>()->mesh = highlightEdgesMesh;
        highlightEdgesEntity->getComponent<our::MeshRendererComponent>()->material = our::AssetLoader<our::Material>::get("wireframe");
        highlightEdgesEntity->localTransform.scale = glm::vec3(0.505f);

        spawnKillableNpcsForSession();
        rebuildMesh();
    }

    void onDraw(double deltaTime) override
    {
        movementSystem.update(&engineWorld, (float)deltaTime);
        playerController.update(&engineWorld, (float)deltaTime);
        updateKillableNpcWander((float)deltaTime);
        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        lightSystem.update(&engineWorld, (float)deltaTime);
        timeSystem.update(&engineWorld, (float)deltaTime);
        blockInteraction.update((float)deltaTime, &engineWorld);

        auto& keyboard = getApp()->getKeyboard();
        auto& mouse = getApp()->getMouse();

        /* Day/Night Toggle */
        if (keyboard.justPressed(GLFW_KEY_T))
        {
            isDaytime = !isDaytime;
            for (auto entity : engineWorld.getEntities())
            {
                if (auto* light = entity->getComponent<our::LightComponent>())
                {
                    if (entity->name == "daylight") light->enabled = isDaytime;
                    else if (entity->name == "nightlight") light->enabled = !isDaytime;
                }
            }
        }

        our::Entity *playerEntity = findPlayerEntity();
        if (playerEntity)
        {
            glm::vec3 camPos = playerEntity->localTransform.position;
            glm::vec3 camDir = glm::vec3(playerEntity->localTransform.toMat4() * glm::vec4(0, 0, -1, 0));
            auto* playerComp = playerEntity->getComponent<our::PlayerComponent>();

            int px = worldToChunkCoordinate(camPos.x), pz = worldToChunkCoordinate(camPos.z);
            if (px != currentCenterChunkX || pz != currentCenterChunkZ)
            {
                currentCenterChunkX = px; currentCenterChunkZ = pz;
                updateLoadedChunks(px, pz);
            }

            voxel::RayHit vhit = terrainWorld.castRay(camPos, camDir, 8.0f);
            if (vhit.hit)
            {
                glm::vec3 p(vhit.x + 0.5f, vhit.y + 0.5f, vhit.z + 0.5f);
                highlightEntity->localTransform.position = highlightEdgesEntity->localTransform.position = p;
            }
            else
            {
                highlightEntity->localTransform.position = highlightEdgesEntity->localTransform.position = glm::vec3(0, -1000, 0);
            }

            /* Interaction Logic */
            if (mouse.justPressed(0))
            {
                if (vhit.hit)
                {
                    int type = terrainWorld.getBlock(vhit.x, vhit.y, vhit.z);
                    blockInteraction.processClick(vhit, type, terrainWorld, &engineWorld, terrainMeshDirty);
                    if (blockInteraction.currentHits == 0)
                    {
                        our::AudioSystem::playSound("assets/sounds/Hit.wav");
                        if (playerComp) registerCollectedBlock(playerComp, type);
                    }
                }
            }
            if (mouse.justPressed(1) && playerComp)
            {
                int placeType = hotbarBlockType(playerComp->inventoryHotbarSlot);
                int* stack = inventoryCountForType(playerComp, placeType);
                if (vhit.hit && stack && *stack > 0)
                {
                    terrainWorld.placeBlock(vhit, placeType);
                    (*stack)--; terrainMeshDirty = true;
                }
            }
        }

        if (terrainMeshDirty) rebuildMesh();
        renderer.render(&engineWorld);
        engineWorld.deleteMarkedEntities();
        if (keyboard.justPressed(GLFW_KEY_ESCAPE)) getApp()->changeState("menu");
    }

    void onImmediateGui() override
    {
        our::Entity* playerEntity = findPlayerEntity();
        if (!playerEntity) return;
        
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        our::PlayerComponent* player = playerEntity->getComponent<our::PlayerComponent>();

        /* Hotbar Drawing */
        if (player)
        {
            ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f - 160.0f, displaySize.y - 80.0f));
            ImGui::SetNextWindowSize(ImVec2(320.0f, 70.0f));
            if (ImGui::Begin("Hotbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground))
            {
                for (int i = 0; i < kHotbarSlots; i++)
                {
                    if (i > 0) ImGui::SameLine();
                    bool sel = (player->inventoryHotbarSlot == i);
                    ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.8f, 0.7f, 0.2f, 0.8f) : ImVec4(0.1f, 0.1f, 0.1f, 0.6f));
                    ImGui::Button(std::to_string(i+1).c_str(), ImVec2(50, 50));
                    ImGui::PopStyleColor();
                }
            }
            ImGui::End();
        }

        /* Crosshair */
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImVec2 center(displaySize.x * 0.5f, displaySize.y * 0.5f);
        dl->AddLine(ImVec2(center.x - 10, center.y), ImVec2(center.x + 10, center.y), IM_COL32_WHITE, 2.0f);
        dl->AddLine(ImVec2(center.x, center.y - 10), ImVec2(center.x, center.y + 10), IM_COL32_WHITE, 2.0f);
    }

    void onKeyEvent(int key, int scancode, int action, int mods) override
    {
        if (action != GLFW_PRESS) return;
        our::Entity* pe = findPlayerEntity();
        if (pe) {
            auto* p = pe->getComponent<our::PlayerComponent>();
            if (p && key >= GLFW_KEY_1 && key <= GLFW_KEY_5) p->inventoryHotbarSlot = key - GLFW_KEY_1;
        }
    }

    void onScrollEvent(double x, double y) override
    {
        our::Entity* pe = findPlayerEntity();
        if (pe) {
            auto* p = pe->getComponent<our::PlayerComponent>();
            if (p && y != 0) p->inventoryHotbarSlot = (p->inventoryHotbarSlot + (y > 0 ? -1 : 1) + kHotbarSlots) % kHotbarSlots;
        }
    }

    void onDestroy() override
    {
        clearAllChunkRenderGroups();
        engineWorld.deleteMarkedEntities();
        renderer.destroy();
        cameraController.exit();
        playerController.exit();
        engineWorld.clear();
        if (highlightEdgesMesh) delete highlightEdgesMesh;
        our::clearAllAssets();
    }

    void clearAllChunkRenderGroups()
    {
        for (auto& entry : chunkRenderGroups)
        {
            for (auto* e : entry.second.entities) engineWorld.markForRemoval(e);
            for (auto* m : entry.second.meshes) delete m;
        }
        chunkRenderGroups.clear();
    }
};