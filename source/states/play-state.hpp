#pragma once

#include <application.hpp>

#include <ecs/world.hpp>
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
#include <cstdint>
#include <cstdio>
#include <vector>

// This state shows how to use the ECS framework and deserialization.
class Playstate : public our::State
{

    voxel::World terrainWorld;
    our::World engineWorld;
    our::ForwardRenderer renderer;
    our::FreeCameraControllerSystem cameraController;
    our::MovementSystem movementSystem;
    our::PlayerControllerSystem playerController;
    our::CollisionSystem collisionSystem;
    our::BlockInteractionSystem blockInteraction;
    std::vector<our::Entity*> terrainEntities;
    our::LightSystem lightSystem;
    our::TimeSystem timeSystem;
    
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

    static constexpr int kHotbarSlots = 5;

    static int hotbarBlockType(int slot) {
        static const int types[kHotbarSlots] = {
            voxel::GRASS, voxel::DIRT, voxel::WOOD, voxel::STONE, voxel::SAND};
        if (slot < 0) {
            slot = 0;
        }
        if (slot >= kHotbarSlots) {
            slot = kHotbarSlots - 1;
        }
        return types[slot];
    }

    static int* inventoryCountForType(our::PlayerComponent* player, int blockType) {
        switch (blockType) {
            case voxel::GRASS:
                return &player->inventoryGrass;
            case voxel::DIRT:
                return &player->inventoryDirt;
            case voxel::WOOD:
                return &player->inventoryWood;
            case voxel::STONE:
                return &player->inventoryStone;
            case voxel::SAND:
                return &player->inventorySand;
            default:
                return nullptr;
        }
    }

    static void registerCollectedBlock(our::PlayerComponent* player, int blockType) {
        int* slot = inventoryCountForType(player, blockType);
        if (!slot) {
            return;
        }
        (*slot)++;
        player->resourcesCollected++;
        if (player->resourcesCollected >= player->resourcesRequired) {
            player->gameState = our::GameState::WIN;
        }
    }

    /// Small preview matching voxel materials (textures / tints from scene assets).
    static void drawHotbarResourceIcon(ImDrawList* dl, int hotbarSlot, const ImVec2& iconMin, const ImVec2& iconMax) {
        const ImU32 outline = IM_COL32(18, 18, 22, 220);
        switch (hotbarSlot) {
            case 0: {
                our::Texture2D* tex = our::AssetLoader<our::Texture2D>::get("grass");
                if (tex) {
                    dl->AddImage((ImTextureID)(intptr_t)tex->getOpenGLName(), iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1),
                                 IM_COL32_WHITE);
                } else {
                    dl->AddRectFilled(iconMin, iconMax, IM_COL32(72, 130, 58, 255), 4.0f);
                }
                break;
            }
            case 1:
                dl->AddRectFilled(iconMin, iconMax, IM_COL32(115, 77, 51, 255), 4.0f);
                break;
            case 2: {
                our::Texture2D* tex = our::AssetLoader<our::Texture2D>::get("wood");
                if (tex) {
                    dl->AddImage((ImTextureID)(intptr_t)tex->getOpenGLName(), iconMin, iconMax, ImVec2(0, 0), ImVec2(1, 1),
                                 IM_COL32_WHITE);
                } else {
                    dl->AddRectFilled(iconMin, iconMax, IM_COL32(130, 85, 48, 255), 4.0f);
                }
                break;
            }
            case 3:
                dl->AddRectFilled(iconMin, iconMax, IM_COL32(118, 118, 118, 255), 4.0f);
                break;
            case 4:
                dl->AddRectFilled(iconMin, iconMax, IM_COL32(204, 190, 72, 255), 4.0f);
                break;
            default:
                dl->AddRectFilled(iconMin, iconMax, IM_COL32(60, 60, 65, 255), 4.0f);
                break;
        }
        dl->AddRect(iconMin, iconMax, outline, 4.0f, ImDrawCornerFlags_All, 1.25f);
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
        our::Material *dirtMat = our::AssetLoader<our::Material>::get("dirt");
        our::Material *woodMat = our::AssetLoader<our::Material>::get("wood-block");
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
            else if (block.type == voxel::DIRT && dirtMat)
                meshRenderer->material = dirtMat;
            else if (block.type == voxel::WOOD && woodMat)
                meshRenderer->material = woodMat;
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
        // We initialize the player controller system
        playerController.enter(getApp());
        // We initialize the block interaction system
        blockInteraction.enter(getApp());
        timeSystem.initialize(&engineWorld);
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

    void onImmediateGui() override
    {
        our::Entity* playerEntity = findPlayerEntity();
        if (!playerEntity)
        {
            return;
        }

        ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        our::PlayerComponent* player = playerEntity->getComponent<our::PlayerComponent>();
        if (player)
        {
            ImGuiWindowFlags invFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoBackground;
            constexpr float box = 60.0f;
            constexpr float gap = 8.0f;
            constexpr float iconSize = 34.0f;
            const float invW = kHotbarSlots * box + (kHotbarSlots - 1) * gap + 24.0f;
            const float invH = box + 26.0f;
            ImVec2 invSize(invW, invH);
            ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f - invSize.x * 0.5f, displaySize.y - invSize.y - 16.0f),
                                     ImGuiCond_Always);
            ImGui::SetNextWindowSize(invSize, ImGuiCond_Always);
            if (ImGui::Begin("InventoryHotbar", nullptr, invFlags))
            {
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, 0.0f));
                ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
                int counts[kHotbarSlots] = {player->inventoryGrass, player->inventoryDirt, player->inventoryWood,
                                            player->inventoryStone, player->inventorySand};
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for (int i = 0; i < kHotbarSlots; i++)
                {
                    if (i > 0) {
                        ImGui::SameLine(0.0f, gap);
                    }
                    ImGui::PushID(i);
                    const bool selected = (player->inventoryHotbarSlot == i);
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const ImVec2 br(p.x + box, p.y + box);
                    const ImU32 fill = selected ? IM_COL32(55, 85, 130, 230) : IM_COL32(28, 28, 32, 220);
                    const ImU32 border = selected ? IM_COL32(240, 200, 90, 255) : IM_COL32(90, 90, 98, 255);
                    dl->AddRectFilled(p, br, fill, 6.0f);
                    const float ix0 = p.x + (box - iconSize) * 0.5f;
                    const float iy0 = p.y + 5.0f;
                    const ImVec2 iconMin(ix0, iy0);
                    const ImVec2 iconMax(ix0 + iconSize, iy0 + iconSize);
                    drawHotbarResourceIcon(dl, i, iconMin, iconMax);
                    dl->AddRect(p, br, border, 6.0f, ImDrawCornerFlags_All, selected ? 2.5f : 1.0f);
                    if (ImGui::InvisibleButton("slot", ImVec2(box, box))) {
                        player->inventoryHotbarSlot = i;
                    }
                    char cnt[24];
                    std::snprintf(cnt, sizeof(cnt), "x%d", counts[i]);
                    const ImVec2 ts = ImGui::CalcTextSize(cnt);
                    dl->AddText(ImVec2(p.x + (box - ts.x) * 0.5f, p.y + box - ts.y - 4.0f),
                                 IM_COL32(255, 255, 255, 255), cnt);
                    ImGui::PopID();
                }
                ImGui::PopStyleVar();
            }
            ImGui::End();
        }

        ImDrawList *drawList = ImGui::GetForegroundDrawList();
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
        collisionSystem.update(&engineWorld, &terrainWorld, (float)deltaTime);
        lightSystem.update(&engineWorld, (float)deltaTime);
        // cameraController.update(&engineWorld, (float)deltaTime);

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

        our::PlayerComponent* player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;

        if (playerEntity && mouse.justPressed(0))
        { // 0 is usually left click
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

                if (player && inventoryCountForType(player, blockType)) {
                    registerCollectedBlock(player, blockType);
                }

                terrainWorld.breakBlock(hit);

                rebuildMesh();
            }
        }

        // 4. Handle Block Placing (Right Click)
        if (playerEntity && mouse.justPressed(1))
        { // 1 is usually right click
            voxel::RayHit hit = terrainWorld.castRay(cameraPos, cameraDir);
            if (hit.hit && player)
            {
                int placeType = hotbarBlockType(player->inventoryHotbarSlot);
                int* stack = inventoryCountForType(player, placeType);
                if (stack && *stack > 0) {
                    terrainWorld.placeBlock(hit, placeType);
                    (*stack)--;
                    rebuildMesh();
                }
            }
        }

        timeSystem.update(&engineWorld, (float)deltaTime);

        auto &keyboard = getApp()->getKeyboard();
        renderer.render(&engineWorld);
        if (keyboard.justPressed(GLFW_KEY_ESCAPE))
        {
            // If the escape  key is pressed in this frame, go to the play state
            getApp()->changeState("menu");
        }
    }

    void onKeyEvent(int key, int scancode, int action, int mods) override
    {
        (void)scancode;
        (void)mods;
        if (action != GLFW_PRESS && action != GLFW_REPEAT) {
            return;
        }
        our::Entity* playerEntity = findPlayerEntity();
        our::PlayerComponent* player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        if (!player) {
            return;
        }
        if (key == GLFW_KEY_1) {
            player->inventoryHotbarSlot = 0;
        } else if (key == GLFW_KEY_2) {
            player->inventoryHotbarSlot = 1;
        } else if (key == GLFW_KEY_3) {
            player->inventoryHotbarSlot = 2;
        } else if (key == GLFW_KEY_4) {
            player->inventoryHotbarSlot = 3;
        } else if (key == GLFW_KEY_5) {
            player->inventoryHotbarSlot = 4;
        }
    }

    void onScrollEvent(double x_offset, double y_offset) override
    {
        (void)x_offset;
        our::Entity* playerEntity = findPlayerEntity();
        our::PlayerComponent* player = playerEntity ? playerEntity->getComponent<our::PlayerComponent>() : nullptr;
        if (!player || y_offset == 0.0) {
            return;
        }
        int delta = y_offset > 0.0 ? -1 : 1;
        int s = player->inventoryHotbarSlot + delta;
        if (s < 0) {
            s = kHotbarSlots - 1;
        }
        if (s >= kHotbarSlots) {
            s = 0;
        }
        player->inventoryHotbarSlot = s;
    }

    void onDestroy() override
    {
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