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
#include <components/aabb-collider.hpp>
#include <components/killable-npc.hpp>
#include <components/camera.hpp>
#include <audio/audio.hpp>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <utility>
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
    float npcWorldMargin = 1.0f;
    float npcSpawnMargin = 2.0f;
    float npcMinPlayerDistance = 4.0f;
    int npcSpawnAttempts = 50;
    int npcCountMin = 5;
    int npcCountMax = 20;

    struct NpcSpawnTypeConfig {
        std::string namePrefix;
        std::string meshName;
        std::string materialName;
        glm::vec3 scale = glm::vec3(0.55f);
        glm::vec3 colliderHalfSize = glm::vec3(0.4f, 0.5f, 0.4f);
        glm::vec3 colliderCenter = glm::vec3(0.0f);
        bool colliderPhysical = false;
        bool colliderTrigger = true;
        float moveSpeed = 1.0f;
        float wanderInitialMinSec = 0.25f;
        float wanderInitialMaxSec = 0.45f;
        float wanderRetargetMinSec = 1.2f;
        float wanderRetargetMaxSec = 3.0f;
    };

    std::vector<NpcSpawnTypeConfig> npcSpawnTypes;
    
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

    static int topSolidBlockY(const voxel::World& w, int gx, int gz) {
        for (int iy = w.height - 1; iy >= 0; --iy) {
            if (w.getBlock(gx, iy, gz) != voxel::AIR) {
                return iy;
            }
        }
        return -1;
    }

    static void snapKillableNpcFeetToTerrain(our::Entity* entity, const voxel::World& terrain) {
        auto* kn = entity->getComponent<our::KillableNpcComponent>();
        if (!kn) {
            return;
        }
        glm::vec3& p = entity->localTransform.position;
        const float sy = entity->localTransform.scale.y;
        const float footLift = 0.5f * sy + 0.04f;
        const int gx = static_cast<int>(std::floor(p.x));
        const int gz = static_cast<int>(std::floor(p.z));
        const int ty = topSolidBlockY(terrain, gx, gz);
        if (ty >= 0) {
            p.y = static_cast<float>(ty + 1) + footLift;
        }
    }

    static void snapKillableNpcsToTerrain(our::World* world, const voxel::World& terrain) {
        for (auto* entity : world->getEntities()) {
            if (!entity->getComponent<our::KillableNpcComponent>()) {
                continue;
            }
            snapKillableNpcFeetToTerrain(entity, terrain);
        }
    }

    static glm::vec3 jsonVec3(const nlohmann::json& value) {
        return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    }

    void loadNpcSpawnerConfig(const nlohmann::json& sceneConfig) {
        if (!sceneConfig.contains("npcSpawner")) {
            return;
        }
        const auto& cfg = sceneConfig["npcSpawner"];
        npcWorldMargin = cfg.value("worldMargin", npcWorldMargin);
        npcSpawnMargin = cfg.value("spawnMargin", npcSpawnMargin);
        npcMinPlayerDistance = cfg.value("minPlayerDistance", npcMinPlayerDistance);
        npcSpawnAttempts = cfg.value("spawnAttempts", npcSpawnAttempts);
        npcCountMin = cfg.value("minCount", npcCountMin);
        npcCountMax = cfg.value("maxCount", npcCountMax);
        if (npcCountMax < npcCountMin) {
            npcCountMax = npcCountMin;
        }

        npcSpawnTypes.clear();
        if (!cfg.contains("types") || !cfg["types"].is_array()) {
            return;
        }

        for (const auto& t : cfg["types"]) {
            if (!t.is_object()) {
                continue;
            }
            NpcSpawnTypeConfig type;
            type.namePrefix = t.value("namePrefix", "npc");
            type.meshName = t.value("mesh", "");
            type.materialName = t.value("material", "");
            if (t.contains("scale")) type.scale = jsonVec3(t["scale"]);
            if (t.contains("colliderHalfSize")) type.colliderHalfSize = jsonVec3(t["colliderHalfSize"]);
            if (t.contains("colliderCenter")) type.colliderCenter = jsonVec3(t["colliderCenter"]);
            type.colliderPhysical = t.value("colliderPhysical", type.colliderPhysical);
            type.colliderTrigger = t.value("colliderTrigger", type.colliderTrigger);
            type.moveSpeed = t.value("moveSpeed", type.moveSpeed);
            type.wanderInitialMinSec = t.value("wanderInitialMinSec", type.wanderInitialMinSec);
            type.wanderInitialMaxSec = t.value("wanderInitialMaxSec", type.wanderInitialMaxSec);
            type.wanderRetargetMinSec = t.value("wanderRetargetMinSec", type.wanderRetargetMinSec);
            type.wanderRetargetMaxSec = t.value("wanderRetargetMaxSec", type.wanderRetargetMaxSec);
            if (type.wanderInitialMaxSec < type.wanderInitialMinSec) {
                type.wanderInitialMaxSec = type.wanderInitialMinSec;
            }
            if (type.wanderRetargetMaxSec < type.wanderRetargetMinSec) {
                type.wanderRetargetMaxSec = type.wanderRetargetMinSec;
            }
            npcSpawnTypes.push_back(type);
        }
    }

    /// Spawns animals using config from scene.npcSpawner.
    void spawnKillableNpcsForSession() {
        if (npcSpawnTypes.empty()) {
            return;
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
        const unsigned tick = static_cast<unsigned>(ms.count() & 0xFFFFFFFFu);
        const int count = npcCountMin + static_cast<int>(tick % (npcCountMax - npcCountMin + 1));

        our::Entity* playerEntity = findPlayerEntity();
        const glm::vec2 playerXZ(
            playerEntity ? playerEntity->localTransform.position.x : static_cast<float>(terrainWorld.width) * 0.5f,
            playerEntity ? playerEntity->localTransform.position.z : static_cast<float>(terrainWorld.depth) * 0.5f);

        const int spanX = std::max(1, terrainWorld.width - static_cast<int>(npcSpawnMargin * 2.0f));
        const int spanZ = std::max(1, terrainWorld.depth - static_cast<int>(npcSpawnMargin * 2.0f));

        for (int i = 0; i < count; ++i) {
            const unsigned pickSeed = (((tick ^ 0xA5A5A5A5u) >> (i % 16)) ^ (static_cast<unsigned>(i) * 2246822519u));
            const auto& type = npcSpawnTypes[pickSeed % npcSpawnTypes.size()];

            float x = npcSpawnMargin;
            float z = npcSpawnMargin;
            for (int attempt = 0; attempt < npcSpawnAttempts; ++attempt) {
                x = npcSpawnMargin + static_cast<float>(rand() % spanX);
                z = npcSpawnMargin + static_cast<float>(rand() % spanZ);
                if (glm::length(glm::vec2(x, z) - playerXZ) >= npcMinPlayerDistance) {
                    break;
                }
            }

            our::Entity* e = engineWorld.add();
            e->name = type.namePrefix + "-" + std::to_string(i);
            e->localTransform.position = glm::vec3(x, 0.0f, z);
            e->localTransform.scale = type.scale;
            e->localTransform.rotation = glm::vec3(0.0f);

            auto* mr = e->addComponent<our::MeshRendererComponent>();
            mr->mesh = our::AssetLoader<our::Mesh>::get(type.meshName);
            mr->material = our::AssetLoader<our::Material>::get(type.materialName);

            auto* kn = e->addComponent<our::KillableNpcComponent>();
            kn->moveSpeed = type.moveSpeed;
            kn->wanderRetargetMinSec = type.wanderRetargetMinSec;
            kn->wanderRetargetMaxSec = type.wanderRetargetMaxSec;
            const float initRange = type.wanderInitialMaxSec - type.wanderInitialMinSec;
            const float r = static_cast<float>(rand() % 10000) / 10000.0f;
            kn->wanderTimerSec = type.wanderInitialMinSec + initRange * r;

            auto* col = e->addComponent<our::AABBColliderComponent>();
            col->halfSize = type.colliderHalfSize;
            col->center = type.colliderCenter;
            col->isPhysical = type.colliderPhysical;
            col->isTrigger = type.colliderTrigger;
        }
    }

    void updateKillableNpcWander(float dt) {
        const float margin = npcWorldMargin;
        const float wmin = margin;
        const float wmaxX = static_cast<float>(terrainWorld.width) - margin;
        const float wmaxZ = static_cast<float>(terrainWorld.depth) - margin;

        for (auto* entity : engineWorld.getEntities()) {
            auto* kn = entity->getComponent<our::KillableNpcComponent>();
            if (!kn) {
                continue;
            }

            kn->wanderTimerSec -= dt;
            if (kn->wanderTimerSec <= 0.0f) {
                const float a =
                    static_cast<float>(rand() % 10000) / 10000.0f * 6.2831853f;
                kn->wanderDirXZ = glm::vec2(std::cos(a), std::sin(a));
                const float range = kn->wanderRetargetMaxSec - kn->wanderRetargetMinSec;
                const float r = static_cast<float>(rand() % 10000) / 10000.0f;
                kn->wanderTimerSec = kn->wanderRetargetMinSec + range * r;
            }

            glm::vec2 dir = kn->wanderDirXZ;
            const float len = glm::length(dir);
            if (len > 1e-4f) {
                dir /= len;
            } else {
                dir = glm::vec2(1.0f, 0.0f);
            }

            glm::vec3& p = entity->localTransform.position;
            const float step = kn->moveSpeed * dt;
            float nx = p.x + dir.x * step;
            float nz = p.z + dir.y * step;
            nx = std::clamp(nx, wmin, wmaxX);
            nz = std::clamp(nz, wmin, wmaxZ);

            const int gx = static_cast<int>(std::floor(nx));
            const int gz = static_cast<int>(std::floor(nz));
            if (topSolidBlockY(terrainWorld, gx, gz) < 0) {
                kn->wanderTimerSec = 0.0f;
                continue;
            }

            p.x = nx;
            p.z = nz;
            entity->localTransform.rotation.x = 0.0f;
            entity->localTransform.rotation.z = 0.0f;
            entity->localTransform.rotation.y = std::atan2(dir.x, dir.y);

            snapKillableNpcFeetToTerrain(entity, terrainWorld);
        }
    }

    static float distanceToVoxelHit(const glm::vec3& rayOrigin, const voxel::RayHit& hit) {
        if (!hit.hit) {
            return 1.0e9f;
        }
        const glm::vec3 c(static_cast<float>(hit.x) + 0.5f, static_cast<float>(hit.y) + 0.5f,
                          static_cast<float>(hit.z) + 0.5f);
        return glm::length(c - rayOrigin);
    }

    static std::optional<std::pair<our::Entity*, float>> raycastKillableNpcs(our::World* world,
                                                                             const glm::vec3& rayOrigin,
                                                                             const glm::vec3& rayDirection,
                                                                             float maxDistance,
                                                                             our::Entity* playerEntity) {
        const glm::vec3 dir = glm::normalize(rayDirection);
        float bestT = maxDistance;
        our::Entity* bestEntity = nullptr;

        for (auto* entity : world->getEntities()) {
            if (entity == playerEntity) {
                continue;
            }
            if (!entity->getComponent<our::KillableNpcComponent>()) {
                continue;
            }
            auto* col = entity->getComponent<our::AABBColliderComponent>();
            if (!col) {
                continue;
            }
            const glm::vec3 pos = entity->localTransform.position;
            const glm::vec3 boxMin = col->getMinCorner(pos);
            const glm::vec3 boxMax = col->getMaxCorner(pos);

            glm::vec3 invDir(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);
            glm::vec3 t0 = (boxMin - rayOrigin) * invDir;
            glm::vec3 t1 = (boxMax - rayOrigin) * invDir;
            glm::vec3 tmin = glm::min(t0, t1);
            glm::vec3 tmax = glm::max(t0, t1);
            const float tEnter = glm::max(glm::max(tmin.x, tmin.y), tmin.z);
            const float tExit = glm::min(glm::min(tmax.x, tmax.y), tmax.z);
            if (tEnter < tExit && tExit > 0.0f && tEnter > 0.0f && tEnter < bestT) {
                bestT = tEnter;
                bestEntity = entity;
            }
        }
        if (bestEntity) {
            return std::make_pair(bestEntity, bestT);
        }
        return std::nullopt;
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
        loadNpcSpawnerConfig(config);
        terrainWorld.generate();
        std::srand(static_cast<unsigned>(std::time(nullptr)));
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
        spawnKillableNpcsForSession();
        snapKillableNpcsToTerrain(&engineWorld, terrainWorld);
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
        updateKillableNpcWander(static_cast<float>(deltaTime));
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
            constexpr float reach = 8.0f;
            std::optional<std::pair<our::Entity*, float>> npcHit =
                raycastKillableNpcs(&engineWorld, cameraPos, cameraDir, reach, playerEntity);
            voxel::RayHit vhit = terrainWorld.castRay(cameraPos, cameraDir, reach);
            const float voxelDist = distanceToVoxelHit(cameraPos, vhit);
            const bool npcCloser = npcHit.has_value() && (!vhit.hit || npcHit->second < voxelDist);

            if (npcCloser) {
                engineWorld.markForRemoval(npcHit->first);
                engineWorld.deleteMarkedEntities();
                our::AudioSystem::playSound("assets/sounds/animal_hit.wav");
            } else if (vhit.hit) {
                int blockType = terrainWorld.getBlock(vhit.x, vhit.y, vhit.z);
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

                terrainWorld.breakBlock(vhit);

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