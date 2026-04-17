#pragma once

#include <application.hpp>

#include <ecs/world.hpp>
#include <systems/forward-renderer.hpp>
#include <systems/free-camera-controller.hpp>
#include <systems/movement.hpp>
#include <systems/player-controller.hpp>
#include <systems/collision-system.hpp>
#include <systems/block-interaction.hpp>
//#include <systems/ui-system.hpp>
#include <asset-loader.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <audio/audio.hpp>
#include <vector>

// This state shows how to use the ECS framework and deserialization.
class Playstate: public our::State {

    voxel::World terrainWorld;
    our::World engineWorld;
    our::ForwardRenderer renderer;
    our::FreeCameraControllerSystem cameraController;
    our::MovementSystem movementSystem;
    our::PlayerControllerSystem playerController;
    our::CollisionSystem collisionSystem;
    our::BlockInteractionSystem blockInteraction;
    //our::UISystem uiSystem;
    std::vector<our::Entity*> terrainEntities;

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

    void rebuildMesh() {
        for (auto* entity : terrainEntities) {
            engineWorld.markForRemoval(entity);
        }
        engineWorld.deleteMarkedEntities();
        terrainEntities.clear();

        auto visibleBlocks = terrainWorld.getVisibleBlocks();

        our::Mesh* cubeMesh = our::AssetLoader<our::Mesh>::get("cube");

        our::Material* stoneMat = our::AssetLoader<our::Material>::get("stone");
        our::Material* grassMat = our::AssetLoader<our::Material>::get("grass");
        our::Material* waterMat = our::AssetLoader<our::Material>::get("water");
        our::Material* sandMat  = our::AssetLoader<our::Material>::get("sand");
        our::Material* defaultMat = our::AssetLoader<our::Material>::get("default");

        for(const auto& block : visibleBlocks) {
            our::Entity* blockEntity = engineWorld.add();

            // Voxel indices represent unit cells [x, x+1], so center the visual cube at +0.5.
            blockEntity->localTransform.position = glm::vec3(block.x + 0.5f, block.y + 0.5f, block.z + 0.5f);
            blockEntity->localTransform.scale = glm::vec3(0.5f, 0.5f, 0.5f);

            auto meshRenderer = blockEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = cubeMesh;

            if (block.type == voxel::STONE && stoneMat) meshRenderer->material = stoneMat;
            else if (block.type == voxel::GRASS && grassMat) meshRenderer->material = grassMat;
            else if (block.type == voxel::WATER && waterMat) meshRenderer->material = waterMat;
            else if (block.type == voxel::SAND && sandMat) meshRenderer->material = sandMat;
            else meshRenderer->material = defaultMat;

            terrainEntities.push_back(blockEntity);
        }
    }

    void onInitialize() override {
        // First of all, we get the scene configuration from the app config
        auto& config = getApp()->getConfig()["scene"];
        // If we have assets in the scene config, we deserialize them
        if(config.contains("assets")){
            our::deserializeAllAssets(config["assets"]);
        }
        // If we have a engineWorld in the scene config, we use it to populate our engineWorld
        if(config.contains("world")){
            engineWorld.deserialize(config["world"]);
        }
        if(config.contains("terrain")){
            terrainWorld.deserialize(config["terrain"]);
        }
        terrainWorld.generate();
        // We initialize the camera controller system since it needs a pointer to the app
        cameraController.enter(getApp());
        // We initialize the player controller system
        playerController.enter(getApp());
        // We initialize the block interaction system
        blockInteraction.enter(getApp());
        // Then we initialize the renderer
        auto size = getApp()->getFrameBufferSize();
        renderer.initialize(size, config["renderer"]);

        rebuildMesh();

        our::Entity* playerEntity = findPlayerEntity();
        if (playerEntity) {
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
    }

    void onImmediateGui() override {
        if(!findPlayerEntity()) {
            return;
        }

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        ImVec2 center = ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f);

        constexpr float armLength = 8.0f;
        constexpr float thickness = 2.0f;
        const ImU32 color = IM_COL32(255, 255, 255, 220);

        drawList->AddLine(ImVec2(center.x - armLength, center.y), ImVec2(center.x + armLength, center.y), color, thickness);
        drawList->AddLine(ImVec2(center.x, center.y - armLength), ImVec2(center.x, center.y + armLength), color, thickness);
    }

    void onDraw(double deltaTime) override {
        // Here, we just run a bunch of systems to control the engineWorld logic
        movementSystem.update(&engineWorld, (float)deltaTime);
        playerController.update(&engineWorld, (float)deltaTime);
        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        // cameraController.update(&engineWorld, (float)deltaTime);
        // And finally we use the renderer system to draw the scene
        // Get a reference to the keyboard object
        auto& keyboard = getApp()->getKeyboard();


        auto& mouse = getApp()->getMouse();
        our::Entity* playerEntity = findPlayerEntity();
        glm::vec3 cameraPos = glm::vec3(0.0f);
        glm::vec3 cameraDir = glm::vec3(0, 0, -1);
        if(playerEntity) {
            cameraPos = playerEntity->localTransform.position;
            glm::mat4 cameraMatrix = playerEntity->localTransform.toMat4();
            cameraDir = glm::vec3(cameraMatrix * glm::vec4(0, 0, -1, 0));
        }

        if (playerEntity && mouse.justPressed(0)) { // 0 is usually left click
            voxel::RayHit hit = terrainWorld.castRay(cameraPos, cameraDir);
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

                rebuildMesh(); 
            }
        }

        // 4. Handle Block Placing (Right Click)
        if (playerEntity && mouse.justPressed(1)) { // 1 is usually right click
            voxel::RayHit hit = terrainWorld.castRay(cameraPos, cameraDir);
            if (hit.hit) {
                // Place a Stone block for now
                terrainWorld.placeBlock(hit, voxel::STONE); 

                rebuildMesh(); 
            }
        }

        renderer.render(&engineWorld);
                if(keyboard.justPressed(GLFW_KEY_ESCAPE)){
            // If the escape  key is pressed in this frame, go to the play state
            getApp()->changeState("menu");
        }
    }

    void onDestroy() override {
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
        terrainEntities.clear();
        // and we delete all the loaded assets to free memory on the RAM and the VRAM
        our::clearAllAssets();
    }
};