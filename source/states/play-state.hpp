#pragma once

#include <application.hpp>

#include <ecs/world.hpp>
#include <systems/forward-renderer.hpp>
#include <systems/free-camera-controller.hpp>
#include <systems/movement.hpp>
#include <systems/light.hpp>
#include <asset-loader.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <vector>

// This state shows how to use the ECS framework and deserialization.
class Playstate : public our::State
{

    voxel::World terrainWorld;
    our::World engineWorld;
    our::ForwardRenderer renderer;
    our::FreeCameraControllerSystem cameraController;
    our::MovementSystem movementSystem;
    our::LightSystem lightSystem;
    std::vector<our::Entity *> terrainEntities;
    bool isDaytime = true;

    our::Entity *findPlayerEntity()
    {
        for (auto entity : engineWorld.getEntities())
        {
            auto *camera = entity->getComponent<our::CameraComponent>();
            auto *controller = entity->getComponent<our::FreeCameraControllerComponent>();
            if (camera && controller)
            {
                return entity;
            }
        }
        return nullptr;
    }

    void rebuildMesh()
    {
        for (auto *entity : terrainEntities)
        {
            engineWorld.markForRemoval(entity);
        }
        engineWorld.deleteMarkedEntities();
        terrainEntities.clear();

        auto visibleBlocks = terrainWorld.getVisibleBlocks();

        our::Mesh *cubeMesh = our::AssetLoader<our::Mesh>::get("cube");

        our::Material *stoneMat = our::AssetLoader<our::Material>::get("stone");
        our::Material *grassMat = our::AssetLoader<our::Material>::get("grass");
        our::Material *waterMat = our::AssetLoader<our::Material>::get("water");
        our::Material *sandMat = our::AssetLoader<our::Material>::get("sand");
        our::Material *defaultMat = our::AssetLoader<our::Material>::get("default");

        for (const auto &block : visibleBlocks)
        {
            our::Entity *blockEntity = engineWorld.add();

            // Voxel indices represent unit cells [x, x+1], so center the visual cube at +0.5.
            blockEntity->localTransform.position = glm::vec3(block.x + 0.5f, block.y + 0.5f, block.z + 0.5f);
            blockEntity->localTransform.scale = glm::vec3(0.5f, 0.5f, 0.5f);

            auto meshRenderer = blockEntity->addComponent<our::MeshRendererComponent>();
            meshRenderer->mesh = cubeMesh;

            if (block.type == voxel::STONE && stoneMat)
                meshRenderer->material = stoneMat;
            else if (block.type == voxel::GRASS && grassMat)
                meshRenderer->material = grassMat;
            else if (block.type == voxel::WATER && waterMat)
                meshRenderer->material = waterMat;
            else if (block.type == voxel::SAND && sandMat)
                meshRenderer->material = sandMat;
            else
                meshRenderer->material = defaultMat;

            terrainEntities.push_back(blockEntity);
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
            terrainWorld.deserialize(config["terrain"]);
        }
        terrainWorld.generate();
        // We initialize the camera controller system since it needs a pointer to the app
        cameraController.enter(getApp());
        // Then we initialize the renderer
        auto size = getApp()->getFrameBufferSize();
        renderer.initialize(size, config["renderer"]);

        rebuildMesh();
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
        lightSystem.update(&engineWorld, (float)deltaTime);
        cameraController.update(&engineWorld, (float)deltaTime);
        // And finally we use the renderer system to draw the scene
        // Get a reference to the keyboard object
        auto &keyboard = getApp()->getKeyboard();

        auto &mouse = getApp()->getMouse();
        our::Entity *playerEntity = findPlayerEntity();
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
            voxel::RayHit hit = terrainWorld.castRay(cameraPos, cameraDir);
            if (hit.hit)
            {
                terrainWorld.breakBlock(hit);

                rebuildMesh();
            }
        }

        // 4. Handle Block Placing (Right Click)
        if (playerEntity && mouse.justPressed(1))
        { // 1 is usually right click
            voxel::RayHit hit = terrainWorld.castRay(cameraPos, cameraDir);
            if (hit.hit)
            {
                // Place a Stone block for now
                terrainWorld.placeBlock(hit, voxel::STONE);

                rebuildMesh();
            }
        }

        // Handle sky light (day/night)
        if (keyboard.justPressed(GLFW_KEY_T))
        {
            isDaytime = !isDaytime;

            for (auto entity : engineWorld.getEntities())
            {
                /* Check if this entity has a light component */
                if (auto *light = entity->getComponent<our::LightComponent>())
                {
                    /* Toggle based on the name assigned in the JSON config */
                    if (entity->name == "daylight")
                        light->enabled = isDaytime;
                    else if (entity->name == "nightlight")
                        light->enabled = !isDaytime;
                }
            }
        }

        renderer.render(&engineWorld);
        if (keyboard.justPressed(GLFW_KEY_ESCAPE))
        {
            // If the escape  key is pressed in this frame, go to the play state
            getApp()->changeState("menu");
        }
    }

    void onDestroy() override
    {
        // Don't forget to destroy the renderer
        renderer.destroy();
        // On exit, we call exit for the camera controller system to make sure that the mouse is unlocked
        cameraController.exit();
        // Clear the engineWorld
        engineWorld.clear();
        terrainEntities.clear();
        // and we delete all the loaded assets to free memory on the RAM and the VRAM
        our::clearAllAssets();
    }
};