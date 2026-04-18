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
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <audio/audio.hpp>
#include <unordered_map>
#include <vector>
#include <cmath>
#include <limits>
#include <string>
#include <algorithm>

// This state shows how to use the ECS framework and deserialization.
class Playstate : public our::State
{

    struct ChunkRenderGroup {
        std::vector<our::Entity*> entities;
        std::vector<our::Mesh*> meshes;
    };

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
    int chunkLoadRadius = 2;
    int chunkUnloadRadius = 3;
    int currentCenterChunkX = std::numeric_limits<int>::min();
    int currentCenterChunkZ = std::numeric_limits<int>::min();
    bool terrainMeshDirty = true;
    std::unordered_map<std::string, ChunkRenderGroup> chunkRenderGroups;

    static int worldToChunkCoordinate(float worldCoord) {
        return static_cast<int>(std::floor(worldCoord / static_cast<float>(voxel::Chunk::CHUNK_SIZE)));
    }

    our::Entity* findPlayerEntity() {
        for (auto entity : engineWorld.getEntities()) {
            auto* camera = entity->getComponent<our::CameraComponent>();
            auto* player = entity->getComponent<our::PlayerComponent>();
            if (camera && player) {
                return entity;
            }
        }
        return nullptr;
    }

    our::Material* getMaterialForBlockType(int blockType) {
        switch (blockType) {
            case voxel::STONE: return our::AssetLoader<our::Material>::get("stone");
            case voxel::GRASS:
            case voxel::DIRT: return our::AssetLoader<our::Material>::get("grass");
            case voxel::SAND: return our::AssetLoader<our::Material>::get("sand");
            case voxel::WATER: return our::AssetLoader<our::Material>::get("water");
            default: return our::AssetLoader<our::Material>::get("default");
        }
    }

    void clearChunkRenderGroup(const std::string& chunkKey) {
        auto it = chunkRenderGroups.find(chunkKey);
        if (it == chunkRenderGroups.end()) return;

        for (auto* entity : it->second.entities) {
            engineWorld.markForRemoval(entity);
        }

        for (auto* mesh : it->second.meshes) {
            delete mesh;
        }

        chunkRenderGroups.erase(it);
    }

    void clearAllChunkRenderGroups() {
        for (auto& entry : chunkRenderGroups) {
            for (auto* entity : entry.second.entities) {
                engineWorld.markForRemoval(entity);
            }
            for (auto* mesh : entry.second.meshes) {
                delete mesh;
            }
        }
        chunkRenderGroups.clear();
    }

    void buildChunkRenderGroup(const std::string& chunkKey, const voxel::Chunk& chunk) {
        ChunkRenderGroup renderGroup;

        const int meshBlockTypes[] = {
            voxel::STONE,
            voxel::GRASS,
            voxel::DIRT,
            voxel::SAND,
            voxel::WATER,
            voxel::WOOD,
            voxel::LEAF,
            voxel::Diamond,
            voxel::Glass
        };

        glm::vec3 chunkOrigin(
            static_cast<float>(chunk.chunkX * voxel::Chunk::CHUNK_SIZE),
            0.0f,
            static_cast<float>(chunk.chunkZ * voxel::Chunk::CHUNK_SIZE)
        );

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

    void rebuildMesh()
    {
        int chunksUpdatedThisFrame = 0;
        bool stillHasDirtyChunks = false;

        for (auto& entry : terrainWorld.activeChunks) {
            const std::string& chunkKey = entry.first;
            voxel::Chunk& chunk = entry.second;

            bool hasRenderGroup = chunkRenderGroups.find(chunkKey) != chunkRenderGroups.end();
            bool needsUpdate = chunk.isDirty || !hasRenderGroup;
            if (!needsUpdate) continue;

            if (chunksUpdatedThisFrame == 0) {
                clearChunkRenderGroup(chunkKey);
                buildChunkRenderGroup(chunkKey, chunk);
                chunk.isDirty = false;
                ++chunksUpdatedThisFrame;
            } else {
                stillHasDirtyChunks = true;
                break;
            }
        }

        engineWorld.deleteMarkedEntities();
        terrainMeshDirty = stillHasDirtyChunks;
    }

    void updateLoadedChunks(int centerChunkX, int centerChunkZ) {
        bool chunkSetChanged = false;

        for (int dz = -chunkLoadRadius; dz <= chunkLoadRadius; ++dz) {
            for (int dx = -chunkLoadRadius; dx <= chunkLoadRadius; ++dx) {
                int chunkX = centerChunkX + dx;
                int chunkZ = centerChunkZ + dz;
                std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);

                if (terrainWorld.activeChunks.find(key) == terrainWorld.activeChunks.end()) {
                    terrainWorld.generateChunk(chunkX, chunkZ);
                    chunkSetChanged = true;
                }
            }
        }

        for (auto it = terrainWorld.activeChunks.begin(); it != terrainWorld.activeChunks.end();) {
            const auto& chunk = it->second;
            if (std::abs(chunk.chunkX - centerChunkX) > chunkUnloadRadius ||
                std::abs(chunk.chunkZ - centerChunkZ) > chunkUnloadRadius) {
                clearChunkRenderGroup(it->first);
                it = terrainWorld.activeChunks.erase(it);
                chunkSetChanged = true;
            } else {
                ++it;
            }
        }

        if (chunkSetChanged) {
            terrainMeshDirty = true;
        }
    }

    void streamChunksAroundPlayer(const glm::vec3& position) {
        int playerChunkX = worldToChunkCoordinate(position.x);
        int playerChunkZ = worldToChunkCoordinate(position.z);

        if (playerChunkX != currentCenterChunkX || playerChunkZ != currentCenterChunkZ) {
            currentCenterChunkX = playerChunkX;
            currentCenterChunkZ = playerChunkZ;
            updateLoadedChunks(playerChunkX, playerChunkZ);
        }
    }

    void onInitialize() override
    {
        // First of all, we get the scene configuration from the app config
        auto &config = getApp()->getConfig()["scene"];
        // If we have assets in the scene config, we deserialize them
        if (config.contains("assets"))
        {
            our::deserializeAllAssets(config["assets"]);
        }
        // If we have a engineWorld in the scene config, we use it to populate our engineWorld
        if (config.contains("world"))
        {
            engineWorld.deserialize(config["world"]);
        }
        if (config.contains("terrain"))
        {
            auto& terrainConfig = config["terrain"];
            terrainWorld.deserialize(terrainConfig);
            chunkLoadRadius = terrainConfig.value("chunk-load-radius", chunkLoadRadius);
            chunkUnloadRadius = terrainConfig.value("chunk-unload-radius", chunkLoadRadius + 1);
            chunkUnloadRadius = std::max(chunkUnloadRadius, chunkLoadRadius);
        }
        // We initialize the camera controller system since it needs a pointer to the app
        cameraController.enter(getApp());
        // We initialize the player controller system
        playerController.enter(getApp());
        // We initialize the block interaction system
        blockInteraction.enter(getApp());
        timeSystem.initialize(&engineWorld);
        // Then we initialize the renderer
        auto size = getApp()->getFrameBufferSize();
        renderer.initialize(size, config["renderer"]);

        our::Entity* playerEntity = findPlayerEntity();
        if (playerEntity) {
            streamChunksAroundPlayer(playerEntity->localTransform.position);

            auto& pos = playerEntity->localTransform.position;
            int px = static_cast<int>(std::floor(pos.x));
            int pz = static_cast<int>(std::floor(pos.z));
            for (int y = terrainWorld.height - 1; y >= 0; --y) {
                if (terrainWorld.getBlock(px, y, pz) != 0) { // Air is 0
                    pos.y = y + 2.5f; // Place character safely above block (1 unit above ground + offset for center)
                    break;
                }
            }
        }

        if (terrainMeshDirty) {
            rebuildMesh();
        }
    }

    void onImmediateGui() override
    {
        if (!findPlayerEntity())
        {
            return;
        }

        ImDrawList *drawList = ImGui::GetForegroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImVec2 center = ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f);

        constexpr float armLength = 8.0f;
        constexpr float thickness = 2.0f;
        const ImU32 color = IM_COL32(255, 255, 255, 220);

        drawList->AddLine(ImVec2(center.x - armLength, center.y), ImVec2(center.x + armLength, center.y), color, thickness);
        drawList->AddLine(ImVec2(center.x, center.y - armLength), ImVec2(center.x, center.y + armLength), color, thickness);
    }

    void onDraw(double deltaTime) override
    {
        // Here, we just run a bunch of systems to control the world logic
        movementSystem.update(&engineWorld, (float)deltaTime);
        playerController.update(&engineWorld, (float)deltaTime);

        our::Entity *playerEntity = findPlayerEntity();
        if (playerEntity) {
            streamChunksAroundPlayer(playerEntity->localTransform.position);
        }

        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        lightSystem.update(&engineWorld, (float)deltaTime);
        // cameraController.update(&engineWorld, (float)deltaTime);

        auto &mouse = getApp()->getMouse();
        glm::vec3 cameraPos = glm::vec3(0.0f);
        glm::vec3 cameraDir = glm::vec3(0, 0, -1);
        if (playerEntity)
        {
            cameraPos = playerEntity->localTransform.position;
            glm::mat4 cameraMatrix = playerEntity->localTransform.toMat4();
            cameraDir = glm::vec3(cameraMatrix * glm::vec4(0, 0, -1, 0));
        }

        if (playerEntity && mouse.justPressed(0))
        { // 0 is usually left click
            RayHit hit = terrainWorld.castRay(cameraPos, cameraDir);
             if (hit.hit) {
                int blockType = terrainWorld.getBlock(hit.x, hit.y, hit.z);
                if (blockType == voxel::GRASS || blockType == voxel::DIRT) {
                    our::AudioSystem::playSound("assets/sounds/Grass.wav");
                } else if (blockType == voxel::STONE || blockType == voxel::Diamond || blockType == voxel::Glass) {
                    our::AudioSystem::playSound("assets/sounds/Hit.wav");
                } else if (blockType == voxel::WOOD || blockType == voxel::LEAF) {
                    our::AudioSystem::playSound("assets/sounds/Wood.wav");
                } else if (blockType == voxel::SAND) {
                    our::AudioSystem::playSound("assets/sounds/Clay.wav");
                } else {
                    our::AudioSystem::playSound("assets/sounds/Hit.wav");
                }


                terrainWorld.breakBlock(hit);
                terrainMeshDirty = true;
            }
        }

        // 4. Handle Block Placing (Right Click)
        if (playerEntity && mouse.justPressed(1))
        { // 1 is usually right click
            RayHit hit = terrainWorld.castRay(cameraPos, cameraDir);
            if (hit.hit)
            {
                // Place a Stone block for now
                terrainWorld.placeBlock(hit, voxel::STONE);
                terrainMeshDirty = true;
            }
        }

        timeSystem.update(&engineWorld, (float)deltaTime);

        if (terrainMeshDirty) {
            rebuildMesh();
        }

        auto &keyboard = getApp()->getKeyboard();
        renderer.render(&engineWorld);
        if (keyboard.justPressed(GLFW_KEY_ESCAPE))
        {
            // If the escape  key is pressed in this frame, go to the play state
            getApp()->changeState("menu");
        }
    }

    void onDestroy() override
    {
        clearAllChunkRenderGroups();
        engineWorld.deleteMarkedEntities();
        // Don't forget to destroy the renderer
        renderer.destroy();
        // On exit, we call exit for the camera controller system to make sure that the mouse is unlocked
        cameraController.exit();
        // On exit, we call exit for the player controller system
        playerController.exit();
        // On exit, we call exit for the block interaction system
        blockInteraction.exit();
        // Clear the engineWorld
        engineWorld.clear();
        // and we delete all the loaded assets to free memory on the RAM and the VRAM
        our::clearAllAssets();
    }
};