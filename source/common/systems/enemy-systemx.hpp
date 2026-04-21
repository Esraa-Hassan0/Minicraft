#pragma once

/*  EnemySystem  —  Phase 2 "Game Soul" upgrade
    ================================================================
    - AABB terrain collision for all enemies (per-axis sweep)
    - Gravity + grounding
    - Skeleton arrows collide with terrain blocks
    - Creeper explosions destroy blocks in a radius
    - Zombie melee via AABB overlap
    - Win/Lose ImGui overlay
    ================================================================
*/

#include <ecs/world.hpp>
#include <ecs/entity.hpp>
#include <components/camera.hpp>
#include <components/player.hpp>
#include <components/mesh-renderer.hpp>
#include <components/aabb-collider.hpp>
#include <asset-loader.hpp>
#include <material/material.hpp>
#include <voxel/world.hpp>
#include <audio/audio.hpp>

#include "../components/enemy-component.hpp"
#include "../mesh/enemy-mesh-builder.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <vector>
#include <cstdlib>
#include <cmath>
#include <string>
#include <algorithm>

// ──────────────────────────────────────────────────────────────
//  Internal projectile tag (lives as a component on an entity)
// ──────────────────────────────────────────────────────────────
namespace our {

struct ProjectileComponent : public Component {
    glm::vec3 velocity{0.0f};
    float     damage    = 8.0f;
    float     lifetime  = 4.0f;   // seconds before auto-remove
    float     elapsed   = 0.0f;

    static std::string getID() { return "ProjectileComponent"; }
    void deserialize(const nlohmann::json&) override {}
};

// ──────────────────────────────────────────────────────────────
class EnemySystem {
public:

    // ── Tuning ────────────────────────────────────────────────
    float spawnRadius      = 20.0f;  // Spawn enemies this far from player
    float spawnMinRadius   =  8.0f;  // But not closer than this
    float spawnInterval    = 10.0f;  // Seconds between spawn waves
    int   maxEnemies       = 12;     // Soft cap on living enemies

    // ── Internal state ────────────────────────────────────────
    float spawnTimer = 0.0f;
    int   waveNumber = 0;

    // Cached meshes (created once, shared across all enemy entities of that type)
    Mesh* zombieMesh     = nullptr;
    Mesh* skeletonMesh   = nullptr;
    Mesh* creeperMesh    = nullptr;
    Mesh* projectileMesh = nullptr;

    // ── Lifecycle ─────────────────────────────────────────────
    void initialize() {
        zombieMesh     = enemy_mesh::buildZombieMesh();
        skeletonMesh   = enemy_mesh::buildSkeletonMesh();
        creeperMesh    = enemy_mesh::buildCreeperMesh();
        projectileMesh = enemy_mesh::buildProjectileMesh();
        spawnTimer     = spawnInterval * 0.5f; // First wave comes a bit earlier
    }

    void destroy() {
        delete zombieMesh;     zombieMesh     = nullptr;
        delete skeletonMesh;   skeletonMesh   = nullptr;
        delete creeperMesh;    creeperMesh    = nullptr;
        delete projectileMesh; projectileMesh = nullptr;
    }

    // ── Main update (call every frame from play-state onDraw) ─
    void update(World* world, voxel::World* terrain,
                const glm::vec3& playerPos, float dt,
                bool& terrainMeshDirty)
    {
        Entity* playerEntity = findPlayer(world);
        if (!playerEntity) return;

        auto* player = playerEntity->getComponent<PlayerComponent>();
        if (!player) return;

        // If game is already over, skip AI
        if (player->gameState != GameState::PLAYING) return;

        // Spawn wave logic
        spawnTimer += dt;
        if (spawnTimer >= spawnInterval) {
            spawnTimer = 0.0f;
            trySpawnWave(world, terrain, playerPos);
        }

        // Collect entities to remove after iterating
        std::vector<Entity*> toRemove;

        for (Entity* entity : world->getEntities()) {
            auto* enemy = entity->getComponent<EnemyComponent>();
            if (!enemy) continue;

            tickEnemy(world, terrain, entity, enemy, player, playerEntity,
                      playerPos, dt, toRemove, terrainMeshDirty);
        }

        // Tick projectiles
        for (Entity* entity : world->getEntities()) {
            auto* proj = entity->getComponent<ProjectileComponent>();
            if (!proj) continue;

            tickProjectile(world, terrain, entity, proj, player, playerPos, dt, toRemove);
        }

        // Remove dead entities
        for (Entity* e : toRemove)
            world->markForRemoval(e);

        world->deleteMarkedEntities();
    }

    // ── ImGui overlay for WIN / LOSE ───────────────────────────
    void drawGameOverlay(World* world) {
        Entity* playerEntity = findPlayer(world);
        if (!playerEntity) return;
        auto* player = playerEntity->getComponent<PlayerComponent>();
        if (!player) return;
        if (player->gameState == GameState::PLAYING) return;

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

        // Full-screen dim overlay
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGuiWindowFlags bgFlags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_NoInputs     |
                                   ImGuiWindowFlags_NoNav        |
                                   ImGuiWindowFlags_NoMove       |
                                   ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("##gameoverdim", nullptr, bgFlags);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled({0,0}, io.DisplaySize, IM_COL32(0,0,0,160));
        ImGui::End();

        // Centered panel
        ImVec2 panelSize(520, 300);
        ImGui::SetNextWindowPos(ImVec2(center.x - panelSize.x*0.5f,
                                      center.y - panelSize.y*0.5f));
        ImGui::SetNextWindowSize(panelSize);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove       |
                                 ImGuiWindowFlags_NoSavedSettings;

        if (player->gameState == GameState::WIN) {
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
        const char* title = (player->gameState == GameState::WIN) ? "YOU WIN!" : "GAME OVER";
        ImVec4 titleColor = (player->gameState == GameState::WIN)
            ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
            : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        ImGui::SetCursorPosX((panelSize.x - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::TextColored(titleColor, "%s", title);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        ImGui::Spacing();

        // Stats
        ImGui::SetWindowFontScale(1.3f);
        ImGui::SetCursorPosX(40.0f);
        if (player->gameState == GameState::WIN) {
            ImGui::TextColored({0.9f,0.9f,0.9f,1.0f},
                "Resources Collected: %d / %d",
                player->resourcesCollected, player->resourcesRequired);
        } else {
            ImGui::TextColored({0.9f,0.9f,0.9f,1.0f},
                "You were defeated!");
        }
        ImGui::Spacing();
        ImGui::SetCursorPosX(40.0f);
        ImGui::TextColored({0.85f,0.85f,0.5f,1.0f},
            "Press  R  to restart");
        ImGui::Spacing();
        ImGui::SetCursorPosX(40.0f);
        ImGui::TextColored({0.7f,0.7f,0.7f,1.0f},
            "Press  ESC  to return to the main menu");
        ImGui::SetWindowFontScale(1.0f);

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }

private:
    // ── Entity helpers ────────────────────────────────────────
    Entity* findPlayer(World* world) {
        for (auto* e : world->getEntities()) {
            if (e->getComponent<PlayerComponent>())
                return e;
        }
        return nullptr;
    }

    int countLivingEnemies(World* world) {
        int count = 0;
        for (auto* e : world->getEntities()) {
            auto* en = e->getComponent<EnemyComponent>();
            if (en && en->state != EnemyState::DEAD) count++;
        }
        return count;
    }

    // ══════════════════════════════════════════════════════════
    //  AABB terrain collision for enemies (per-axis sweep)
    //  Inspired by ourCraft's performCollision() pattern.
    // ══════════════════════════════════════════════════════════
    void resolveEnemyTerrainCollision(
        glm::vec3& pos, EnemyComponent* enemy, voxel::World* terrain, float dt)
    {
        if (!terrain) return;

        glm::vec3 hs = enemy->colliderHalfSize;
        glm::vec3 cc = enemy->colliderCenter;

        // Apply gravity
        enemy->velocity.y -= enemy->gravityAccel * dt;
        if (enemy->velocity.y < -40.0f) enemy->velocity.y = -40.0f;

        enemy->isGrounded = false;

        // Integrate velocity and resolve per-axis
        // --- X axis ---
        pos.x += enemy->velocity.x * dt;
        resolveAxis(pos, cc, hs, terrain, 0, enemy->velocity);

        // --- Z axis ---
        pos.z += enemy->velocity.z * dt;
        resolveAxis(pos, cc, hs, terrain, 2, enemy->velocity);

        // --- Y axis ---
        pos.y += enemy->velocity.y * dt;
        {
            glm::vec3 mn = pos + cc - hs;
            glm::vec3 mx = pos + cc + hs;
            int minX = (int)std::floor(mn.x), maxX = (int)std::floor(mx.x);
            int minY = (int)std::floor(mn.y), maxY = (int)std::floor(mx.y);
            int minZ = (int)std::floor(mn.z), maxZ = (int)std::floor(mx.z);

            for (int bx = minX; bx <= maxX; bx++)
            for (int by = minY; by <= maxY; by++)
            for (int bz = minZ; bz <= maxZ; bz++) {
                int bt = terrain->getBlock(bx, by, bz);
                if (bt == 0 || bt == voxel::WATER) continue;

                // Block AABB: center (bx+0.5, by+0.5, bz+0.5), half (0.5,0.5,0.5)
                float blockMinY = (float)by;
                float blockMaxY = (float)by + 1.0f;
                float entMinY   = pos.y + cc.y - hs.y;
                float entMaxY   = pos.y + cc.y + hs.y;

                float penDown = entMaxY - blockMinY;
                float penUp   = blockMaxY - entMinY;

                if (penDown > 0 && penUp > 0) {
                    if (penUp < penDown) {
                        // Push up (landed on block)
                        pos.y += penUp;
                        if (enemy->velocity.y < 0) {
                            enemy->velocity.y = 0;
                            enemy->isGrounded = true;
                        }
                    } else {
                        // Push down (hit head)
                        pos.y -= penDown;
                        if (enemy->velocity.y > 0)
                            enemy->velocity.y = 0;
                    }
                }
            }
        }
    }

    // Resolve collision on a single horizontal axis (0=X, 2=Z)
    void resolveAxis(glm::vec3& pos, const glm::vec3& cc, const glm::vec3& hs,
                     voxel::World* terrain, int axis, glm::vec3& vel)
    {
        glm::vec3 mn = pos + cc - hs;
        glm::vec3 mx = pos + cc + hs;
        int minX = (int)std::floor(mn.x), maxX = (int)std::floor(mx.x);
        int minY = (int)std::floor(mn.y), maxY = (int)std::floor(mx.y);
        int minZ = (int)std::floor(mn.z), maxZ = (int)std::floor(mx.z);

        for (int bx = minX; bx <= maxX; bx++)
        for (int by = minY; by <= maxY; by++)
        for (int bz = minZ; bz <= maxZ; bz++) {
            int bt = terrain->getBlock(bx, by, bz);
            if (bt == 0 || bt == voxel::WATER) continue;

            float blockMin, blockMax, entMin, entMax;
            if (axis == 0) {
                blockMin = (float)bx; blockMax = (float)bx + 1.0f;
                entMin = pos.x + cc.x - hs.x; entMax = pos.x + cc.x + hs.x;
            } else {
                blockMin = (float)bz; blockMax = (float)bz + 1.0f;
                entMin = pos.z + cc.z - hs.z; entMax = pos.z + cc.z + hs.z;
            }

            float penNeg = entMax - blockMin;
            float penPos = blockMax - entMin;

            if (penNeg > 0 && penPos > 0) {
                if (penPos < penNeg) {
                    if (axis == 0) { pos.x += penPos; vel.x = 0; }
                    else           { pos.z += penPos; vel.z = 0; }
                } else {
                    if (axis == 0) { pos.x -= penNeg; vel.x = 0; }
                    else           { pos.z -= penNeg; vel.z = 0; }
                }
            }
        }
    }

    // Check if a world position is inside a solid block
    bool isSolidAt(voxel::World* terrain, int x, int y, int z) {
        if (!terrain) return false;
        int bt = terrain->getBlock(x, y, z);
        return bt != 0 && bt != voxel::WATER;
    }

    // ── Spawning ──────────────────────────────────────────────
    void trySpawnWave(World* world, voxel::World* terrain, glm::vec3 playerPos) {
        waveNumber++;
        int living = countLivingEnemies(world);
        if (living >= maxEnemies) return;

        int toSpawn = std::min(3 + waveNumber, maxEnemies - living);

        for (int i = 0; i < toSpawn; i++) {
            EnemyType type;
            int roll = std::rand() % 10;
            if      (roll < 5)                    type = EnemyType::ZOMBIE;
            else if (roll < 8 || waveNumber < 2)  type = EnemyType::SKELETON;
            else                                   type = EnemyType::CREEPER;

            spawnEnemy(world, terrain, playerPos, type);
        }
    }

    void spawnEnemy(World* world, voxel::World* terrain, glm::vec3 playerPos, EnemyType type) {
        float angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
        float dist  = spawnMinRadius + (float)(std::rand() % (int)(spawnRadius - spawnMinRadius));
        glm::vec3 spawnPos = playerPos + glm::vec3(
            std::cos(angle) * dist,
            0.0f,
            std::sin(angle) * dist
        );

        // Find ground level at spawn position
        if (terrain) {
            for (int y = terrain->height - 1; y >= 0; --y) {
                if (isSolidAt(terrain, (int)std::floor(spawnPos.x), y, (int)std::floor(spawnPos.z))) {
                    spawnPos.y = (float)(y + 1) + 0.01f;
                    break;
                }
            }
        } else {
            spawnPos.y = std::max(playerPos.y - 5.0f, 1.0f);
        }

        Entity* entity = world->add();
        entity->name   = (type == EnemyType::ZOMBIE)   ? "zombie"   :
                         (type == EnemyType::SKELETON)  ? "skeleton" : "creeper";

        entity->localTransform.position = spawnPos;
        entity->localTransform.scale    = glm::vec3(1.0f);

        auto* enemy  = entity->addComponent<EnemyComponent>();
        enemy->type  = type;
        enemy->state = EnemyState::IDLE;
        enemy->previousPosition = spawnPos;  // prevent false stuck-detection on first frame

        switch (type) {
            case EnemyType::ZOMBIE:
                enemy->maxHealth    = 20.0f; enemy->health = 20.0f;
                enemy->speed        = 2.8f;
                enemy->attackDamage = 8.0f;
                enemy->attackRange  = 1.6f;
                break;
            case EnemyType::SKELETON:
                enemy->maxHealth    = 15.0f; enemy->health = 15.0f;
                enemy->speed        = 2.2f;
                enemy->attackDamage = 6.0f;
                enemy->attackRange  = 1.2f;
                enemy->preferredRange = 10.0f;
                break;
            case EnemyType::CREEPER:
                enemy->maxHealth    = 25.0f; enemy->health = 25.0f;
                enemy->speed        = 3.0f;
                enemy->explodeDamage= 45.0f;
                enemy->explodeRange = 3.0f;
                enemy->attackRange  = 1.8f;
                break;
        }

        auto* mr  = entity->addComponent<MeshRendererComponent>();
        mr->mesh  = getMeshForType(type);

        switch (type) {
            case EnemyType::ZOMBIE:
                mr->material = AssetLoader<Material>::get("enemy-zombie");
                if (!mr->material) mr->material = AssetLoader<Material>::get("default");
                break;
            case EnemyType::SKELETON:
                mr->material = AssetLoader<Material>::get("enemy-skeleton");
                if (!mr->material) mr->material = AssetLoader<Material>::get("default");
                break;
            case EnemyType::CREEPER:
                mr->material = AssetLoader<Material>::get("enemy-creeper");
                if (!mr->material) mr->material = AssetLoader<Material>::get("default");
                break;
        }
    }

    Mesh* getMeshForType(EnemyType type) {
        switch (type) {
            case EnemyType::ZOMBIE:   return zombieMesh;
            case EnemyType::SKELETON: return skeletonMesh;
            case EnemyType::CREEPER:  return creeperMesh;
        }
        return zombieMesh;
    }

    // ── Per-enemy AI tick ─────────────────────────────────────
    void tickEnemy(
        World* world, voxel::World* terrain,
        Entity* entity, EnemyComponent* enemy,
        PlayerComponent* player, Entity* playerEntity,
        glm::vec3 playerPos,
        float dt,
        std::vector<Entity*>& toRemove,
        bool& terrainMeshDirty)
    {
        if (enemy->state == EnemyState::DEAD) {
            enemy->deadTimer += dt;
            entity->localTransform.position.y -= dt * 0.8f;
            if (enemy->deadTimer >= enemy->deadDuration)
                toRemove.push_back(entity);
            return;
        }

        glm::vec3& pos = entity->localTransform.position;
        float distToPlayer = glm::distance(pos, playerPos);

        // ── Stuck detection & hard jump (direct Y teleport) ──────
        //    Compare X/Z displacement since last frame.
        //    If near-zero while chasing → blocked by wall → teleport up 1.1 blocks.
        if (enemy->jumpCooldown > 0.0f)
            enemy->jumpCooldown -= dt;

        {
            glm::vec3 prevPos = enemy->previousPosition;
            float hDeltaX = pos.x - prevPos.x;
            float hDeltaZ = pos.z - prevPos.z;
            float hDistMoved = std::sqrt(hDeltaX * hDeltaX + hDeltaZ * hDeltaZ);

            bool isChasing = (enemy->state == EnemyState::CHASE);
            float intendedHSpeed = std::sqrt(enemy->velocity.x * enemy->velocity.x +
                                             enemy->velocity.z * enemy->velocity.z);

            // Hard stuck: moved < 0.01 units on X/Z while trying to chase
            if (isChasing && enemy->isGrounded && enemy->jumpCooldown <= 0.0f &&
                intendedHSpeed > 0.5f && hDistMoved < 0.01f)
            {
                // Direct position teleport — clears a 1-block obstacle instantly
                pos.y += 1.1f;
                enemy->velocity.y = 0.0f;  // no residual vertical velocity
                enemy->jumpCooldown = 0.6f;
            }
        }

        // Attack cooldown tick
        enemy->timeSinceAttack += dt;
        enemy->timeSinceShot   += dt;

        // ── State machine ────────────────────────────────────
        switch (enemy->type) {
        case EnemyType::ZOMBIE:
            tickZombie(entity, enemy, player, playerEntity, playerPos, distToPlayer, dt);
            break;
        case EnemyType::SKELETON:
            tickSkeleton(world, entity, enemy, player, playerEntity, playerPos, distToPlayer, dt);
            break;
        case EnemyType::CREEPER:
            tickCreeper(world, terrain, entity, enemy, player, playerPos, distToPlayer, dt, terrainMeshDirty);
            break;
        }

        // Face the player when chasing/attacking
        if (enemy->state == EnemyState::CHASE || enemy->state == EnemyState::ATTACK) {
            glm::vec3 dir = playerPos - pos;
            dir.y = 0.0f;
            if (glm::length(dir) > 0.001f) {
                float yaw = std::atan2(dir.x, dir.z);
                entity->localTransform.rotation.y = yaw;
            }
        }

        // Apply terrain collision + gravity (skip dead enemies)
        resolveEnemyTerrainCollision(pos, enemy, terrain, dt);

        // Save position AFTER collision resolution so next frame's
        // stuck-detection compares two post-collision positions (apples-to-apples).
        enemy->previousPosition = pos;
    }

    // ─ Zombie ────────────────────────────────────────────────
    void tickZombie(
        Entity* entity, EnemyComponent* enemy,
        PlayerComponent* player, Entity* playerEntity,
        glm::vec3 playerPos, float dist, float dt)
    {
        glm::vec3& pos = entity->localTransform.position;

        if (enemy->health <= 0.0f) {
            enemy->state = EnemyState::DEAD;
            entity->localTransform.scale = glm::vec3(0.85f);
            return;
        }

        if (dist > enemy->detectionRange) {
            enemy->state = EnemyState::IDLE;
            doIdleWander(entity, enemy, dt);
        } else if (dist <= enemy->attackRange) {
            // AABB melee check
            enemy->state = EnemyState::ATTACK;
            if (enemy->timeSinceAttack >= enemy->attackCooldown) {
                // Check AABB overlap with player
                auto* playerCollider = playerEntity->getComponent<AABBColliderComponent>();
                if (playerCollider) {
                    glm::vec3 enemyMin = pos + enemy->colliderCenter - enemy->colliderHalfSize;
                    glm::vec3 enemyMax = pos + enemy->colliderCenter + enemy->colliderHalfSize;
                    glm::vec3 playerMin = playerCollider->getMinCorner(playerEntity->localTransform.position);
                    glm::vec3 playerMax = playerCollider->getMaxCorner(playerEntity->localTransform.position);

                    bool overlap = (enemyMin.x < playerMax.x && enemyMax.x > playerMin.x) &&
                                   (enemyMin.y < playerMax.y && enemyMax.y > playerMin.y) &&
                                   (enemyMin.z < playerMax.z && enemyMax.z > playerMin.z);

                    if (overlap || dist <= enemy->attackRange) {
                        enemy->timeSinceAttack = 0.0f;
                        dealDamageToPlayer(player, enemy->attackDamage);
                        our::AudioSystem::playSound("assets/sounds/Hit.wav");
                    }
                } else {
                    // Fallback: distance-based
                    enemy->timeSinceAttack = 0.0f;
                    dealDamageToPlayer(player, enemy->attackDamage);
                }
            }
        } else {
            // Chase via velocity (collision resolved later)
            enemy->state = EnemyState::CHASE;
            setHorizontalVelocity(enemy, pos, playerPos);
        }
    }

    // ─ Skeleton ──────────────────────────────────────────────
    void tickSkeleton(
        World* world,
        Entity* entity, EnemyComponent* enemy,
        PlayerComponent* player, Entity* playerEntity,
        glm::vec3 playerPos, float dist, float dt)
    {
        glm::vec3& pos = entity->localTransform.position;

        if (enemy->health <= 0.0f) {
            enemy->state = EnemyState::DEAD;
            entity->localTransform.scale = glm::vec3(0.85f);
            return;
        }

        if (dist > enemy->detectionRange) {
            enemy->state = EnemyState::IDLE;
            doIdleWander(entity, enemy, dt);
        } else {
            if (dist < enemy->preferredRange * 0.7f) {
                // Too close → back away
                enemy->state = EnemyState::CHASE;
                glm::vec3 awayDir = pos - playerPos;
                awayDir.y = 0.0f;
                float len = glm::length(awayDir);
                if (len > 0.1f) {
                    awayDir /= len;
                    enemy->velocity.x = awayDir.x * enemy->speed;
                    enemy->velocity.z = awayDir.z * enemy->speed;
                } else {
                    enemy->velocity.x = 0;
                    enemy->velocity.z = 0;
                }
            } else if (dist > enemy->preferredRange * 1.3f) {
                // Too far → approach
                enemy->state = EnemyState::CHASE;
                setHorizontalVelocity(enemy, pos, playerPos);
            } else {
                // Good range → shoot
                enemy->state = EnemyState::ATTACK;
                enemy->velocity.x = 0;
                enemy->velocity.z = 0;
            }

            // Shoot projectile
            if (enemy->timeSinceShot >= enemy->shootCooldown && dist <= enemy->detectionRange) {
                enemy->timeSinceShot = 0.0f;
                fireProjectile(world, pos + glm::vec3(0, 1.2f, 0), playerPos, enemy->attackDamage);
            }
        }
    }

    // ─ Creeper ───────────────────────────────────────────────
    void tickCreeper(
        World* world, voxel::World* terrain,
        Entity* entity, EnemyComponent* enemy,
        PlayerComponent* player,
        glm::vec3 playerPos, float dist, float dt,
        bool& terrainMeshDirty)
    {
        glm::vec3& pos = entity->localTransform.position;

        if (enemy->health <= 0.0f) {
            if (!enemy->isLit) {
                enemy->isLit = true;
                explodeCreeper(entity, enemy, player, playerPos, terrain, terrainMeshDirty);
            }
            enemy->state = EnemyState::DEAD;
            entity->localTransform.scale = glm::vec3(0.5f);
            return;
        }

        if (dist > enemy->detectionRange) {
            enemy->state = EnemyState::IDLE;
            doIdleWander(entity, enemy, dt);
        } else if (dist <= enemy->attackRange) {
            // Close enough → light the fuse
            enemy->state = EnemyState::ATTACK;
            enemy->velocity.x = 0;
            enemy->velocity.z = 0;
            if (!enemy->isLit) {
                enemy->isLit = true;
                enemy->fuseTimer = 0.0f;
            }
        } else {
            // Chase player
            enemy->state = EnemyState::CHASE;
            setHorizontalVelocity(enemy, pos, playerPos);
        }

        // Fuse ticking
        if (enemy->isLit) {
            enemy->fuseTimer += dt;

            // Flash the creeper (scale pulse)
            float pulse = std::sin(enemy->fuseTimer * 10.0f) * 0.1f + 1.0f;
            entity->localTransform.scale = glm::vec3(pulse);

            if (enemy->fuseTimer >= enemy->fuseTime) {
                // BOOM
                explodeCreeper(entity, enemy, player, playerPos, terrain, terrainMeshDirty);
                enemy->state = EnemyState::DEAD;
            }
        }
    }

    // ── Utilities ─────────────────────────────────────────────

    // Set horizontal velocity toward target (gravity handled separately)
    void setHorizontalVelocity(EnemyComponent* enemy, glm::vec3 pos, glm::vec3 target) {
        glm::vec3 dir = target - pos;
        dir.y = 0.0f;
        float len = glm::length(dir);
        if (len > 0.1f) {
            dir /= len;
            enemy->velocity.x = dir.x * enemy->speed;
            enemy->velocity.z = dir.z * enemy->speed;
        } else {
            enemy->velocity.x = 0;
            enemy->velocity.z = 0;
        }
    }

    void doIdleWander(Entity* entity, EnemyComponent* enemy, float dt) {
        enemy->idleTimer += dt;
        if (enemy->idleTimer >= enemy->idleDuration) {
            enemy->idleTimer = 0.0f;
            float angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
            float dist  = 1.0f + (float)(std::rand() % 3);
            enemy->wanderTarget = entity->localTransform.position +
                glm::vec3(std::cos(angle)*dist, 0, std::sin(angle)*dist);
        }
        glm::vec3 dir = enemy->wanderTarget - entity->localTransform.position;
        dir.y = 0.0f;
        float len = glm::length(dir);
        if (len > 0.3f) {
            dir /= len;
            enemy->velocity.x = dir.x * enemy->speed * 0.3f;
            enemy->velocity.z = dir.z * enemy->speed * 0.3f;
        } else {
            enemy->velocity.x = 0;
            enemy->velocity.z = 0;
        }
    }

    void dealDamageToPlayer(PlayerComponent* player, float damage) {
        if (player->timeSinceDamage < player->damageRecoveryTime) return;
        player->timeSinceDamage = 0.0f;
        player->damageFlashTimer = 0.3f;  // Red flash for 0.3s
        player->health -= damage;
        if (player->health <= 0.0f) {
            player->health    = 0.0f;
            player->isAlive   = false;
            player->gameState = GameState::LOSE;
            our::AudioSystem::playSound("assets/sounds/Death.wav");
        }
    }

    // ── Creeper explosion with block destruction ──────────────
    void explodeCreeper(Entity* entity, EnemyComponent* enemy,
                        PlayerComponent* player, glm::vec3 playerPos,
                        voxel::World* terrain, bool& terrainMeshDirty)
    {
        glm::vec3 center = entity->localTransform.position;
        float dist = glm::distance(center, playerPos);

        // Damage player
        if (dist <= enemy->explodeRange) {
            float ratio  = 1.0f - (dist / enemy->explodeRange);
            float damage = enemy->explodeDamage * ratio;
            dealDamageToPlayer(player, damage);
        }

        // Destroy blocks in a sphere
        if (terrain) {
            int radius = (int)std::ceil(enemy->explodeRange);
            int cx = (int)std::floor(center.x);
            int cy = (int)std::floor(center.y);
            int cz = (int)std::floor(center.z);

            for (int dx = -radius; dx <= radius; dx++)
            for (int dy = -radius; dy <= radius; dy++)
            for (int dz = -radius; dz <= radius; dz++) {
                float distSq = (float)(dx*dx + dy*dy + dz*dz);
                if (distSq > enemy->explodeRange * enemy->explodeRange) continue;

                int bx = cx + dx, by = cy + dy, bz = cz + dz;
                if (by < 1) continue;  // Don't destroy bedrock layer

                int bt = terrain->getBlock(bx, by, bz);
                if (bt != 0 && bt != voxel::WATER) {
                    terrain->setBlock(bx, by, bz, voxel::AIR);
                }
            }
            terrainMeshDirty = true;
        }

        // Self-destruct
        enemy->health = 0.0f;
        our::AudioSystem::playSound("assets/sounds/Glass.wav"); // explosion sound
    }

    // ── Projectile with terrain collision ──────────────────────
    void fireProjectile(World* world, glm::vec3 origin, glm::vec3 target, float damage) {
        glm::vec3 dir = glm::normalize(target - origin);

        Entity* arrow   = world->add();
        arrow->name     = "arrow";
        arrow->localTransform.position = origin;

        float yaw   = std::atan2(dir.x, dir.z);
        float pitch = std::asin(-dir.y);
        arrow->localTransform.rotation = glm::vec3(pitch, yaw, 0.0f);

        auto* proj    = arrow->addComponent<ProjectileComponent>();
        proj->velocity = dir * 15.0f;
        proj->damage   = damage;

        auto* mr   = arrow->addComponent<MeshRendererComponent>();
        mr->mesh   = projectileMesh;
        mr->material = AssetLoader<Material>::get("metal");
        if (!mr->material) mr->material = AssetLoader<Material>::get("default");
    }

    void tickProjectile(
        World* world, voxel::World* terrain,
        Entity* entity, ProjectileComponent* proj,
        PlayerComponent* player, glm::vec3 playerPos,
        float dt,
        std::vector<Entity*>& toRemove)
    {
        proj->elapsed += dt;
        if (proj->elapsed >= proj->lifetime) {
            toRemove.push_back(entity);
            return;
        }

        // Move projectile
        entity->localTransform.position += proj->velocity * dt;
        // Gravity arc
        proj->velocity.y -= 9.8f * dt;

        // Update rotation to face velocity direction
        if (glm::length(proj->velocity) > 0.1f) {
            glm::vec3 vdir = glm::normalize(proj->velocity);
            entity->localTransform.rotation.y = std::atan2(vdir.x, vdir.z);
            entity->localTransform.rotation.x = std::asin(-vdir.y);
        }

        glm::vec3 apos = entity->localTransform.position;

        // Check terrain collision
        if (terrain) {
            int bx = (int)std::floor(apos.x);
            int by = (int)std::floor(apos.y);
            int bz = (int)std::floor(apos.z);
            int bt = terrain->getBlock(bx, by, bz);
            if (bt != 0 && bt != voxel::WATER) {
                toRemove.push_back(entity);
                return;
            }
        }

        // Check hit player (AABB-based)
        float hitDist = glm::distance(apos, playerPos);
        if (hitDist < 1.0f) {
            dealDamageToPlayer(player, proj->damage);
            toRemove.push_back(entity);
        }
    }
};

} // namespace our
