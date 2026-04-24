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
    float spawnRadius      = 28.0f;  // Spawn enemies this far from player
    float spawnMinRadius   = 12.0f;  // But not closer than this
    float spawnInterval    = 12.0f;  // Seconds between spawn checks
    int   maxEnemies       = 7;      // Soft cap on living enemies

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

    int countLivingEnemiesOfType(World* world, EnemyType type) {
        int count = 0;
        for (auto* e : world->getEntities()) {
            auto* en = e->getComponent<EnemyComponent>();
            if (en && en->state != EnemyState::DEAD && en->type == type) count++;
        }
        return count;
    }

    // ══════════════════════════════════════════════════════════
    //  AABB terrain collision for enemies (per-axis sweep)
    //  Inspired by ourCraft's performCollision() pattern.
    // ══════════════════════════════════════════════════════════
    // ── Physics: ourCraft-style per-axis sweep ──────────────────
    // Key insight from ourCraft/physics.cpp:
    //   checkCollisionBrute() resolves X, Z, Y independently,
    //   each axis using the LAST SAFE position as reference, NOT the
    //   current (already-moved) position. This prevents the bounce and
    //   duplication glitch caused by penetration-push cascading.
    void resolveEnemyTerrainCollision(
        glm::vec3& pos, EnemyComponent* enemy, voxel::World* terrain, float dt)
    {
        if (!terrain) return;

        // Apply gravity (ourCraft uses symplectic Euler; we keep simple Euler)
        enemy->velocity.y -= enemy->gravityAccel * dt;
        if (enemy->velocity.y < -30.0f) enemy->velocity.y = -30.0f;

        enemy->isGrounded = false;

        glm::vec3 lastPos = enemy->previousPosition; // last confirmed-safe position

        // ── Δ displacements this frame ──
        float dx = enemy->velocity.x * dt;
        float dy = enemy->velocity.y * dt;
        float dz = enemy->velocity.z * dt;

        glm::vec3 hs = enemy->colliderHalfSize;
        glm::vec3 cc = enemy->colliderCenter;

        // ── X axis: test (pos.x+dx, lastPos.y, lastPos.z) vs lastPos ──
        {
            glm::vec3 testPos = {pos.x + dx, lastPos.y, lastPos.z};
            if (!collidesWithTerrain(testPos, cc, hs, terrain)) {
                pos.x = testPos.x;
            } else {
                enemy->velocity.x = 0;
                // Snap to block edge (ourCraft approach: no residual penetration)
                pos.x = snapToBlockEdge(pos.x, dx, cc.x, hs.x);
            }
        }

        // ── Z axis: test (pos.x, lastPos.y, pos.z+dz) vs lastPos ──
        {
            glm::vec3 testPos = {pos.x, lastPos.y, pos.z + dz};
            if (!collidesWithTerrain(testPos, cc, hs, terrain)) {
                pos.z = testPos.z;
            } else {
                enemy->velocity.z = 0;
                pos.z = snapToBlockEdge(pos.z, dz, cc.z, hs.z);
            }
        }

        // ── Y axis: test (pos.x, pos.y+dy, pos.z) ──
        {
            glm::vec3 testPos = {pos.x, pos.y + dy, pos.z};
            if (!collidesWithTerrain(testPos, cc, hs, terrain)) {
                pos.y = testPos.y;
            } else {
                if (dy < 0) {
                    // Hit floor → snap up, mark grounded
                    enemy->isGrounded = true;
                    pos.y = snapToBlockEdgeY(pos.y, dy, pos.x, pos.z, cc, hs, terrain);
                } else {
                    // Hit ceiling → stop
                    pos.y = snapToBlockEdgeY(pos.y, dy, pos.x, pos.z, cc, hs, terrain);
                }
                enemy->velocity.y = 0;
            }
        }
    }

    // Returns true if the collider at 'pos' overlaps any solid block
    bool collidesWithTerrain(const glm::vec3& pos, const glm::vec3& cc,
                              const glm::vec3& hs, voxel::World* terrain)
    {
        glm::vec3 mn = pos + cc - hs;
        glm::vec3 mx = pos + cc + hs;
        int x0=(int)std::floor(mn.x), x1=(int)std::floor(mx.x);
        int y0=(int)std::floor(mn.y), y1=(int)std::floor(mx.y);
        int z0=(int)std::floor(mn.z), z1=(int)std::floor(mx.z);
        for (int bx=x0;bx<=x1;bx++)
        for (int by=y0;by<=y1;by++)
        for (int bz=z0;bz<=z1;bz++) {
            int bt = terrain->getBlock(bx,by,bz);
            if (bt != 0 && bt != voxel::WATER) return true;
        }
        return false;
    }

    // Snap horizontal axis to the nearest block face after collision
    float snapToBlockEdge(float cur, float delta, float ccAxis, float hsAxis)
    {
        if (delta > 0)
            return std::floor(cur + ccAxis + hsAxis) - ccAxis - hsAxis - 0.001f;
        else
            return std::ceil(cur + ccAxis - hsAxis)  - ccAxis + hsAxis + 0.001f;
    }

    // Snap Y to the nearest block face (floor or ceiling)
    float snapToBlockEdgeY(float curY, float delta,
                           float posX, float posZ,
                           const glm::vec3& cc,
                           const glm::vec3& hs,
                           voxel::World* terrain)
    {
        if (!terrain) return curY;

        int x0 = (int)std::floor(posX + cc.x - hs.x);
        int x1 = (int)std::floor(posX + cc.x + hs.x);
        int z0 = (int)std::floor(posZ + cc.z - hs.z);
        int z1 = (int)std::floor(posZ + cc.z + hs.z);

        if (delta < 0) {
            float bottom = curY + delta + cc.y - hs.y;
            int yStart = (int)std::floor(bottom);
            int yEnd = yStart + 3;

            float bestTop = -1e9f;
            for (int by = yStart; by <= yEnd; ++by) {
                for (int bx = x0; bx <= x1; ++bx) {
                    for (int bz = z0; bz <= z1; ++bz) {
                        int bt = terrain->getBlock(bx, by, bz);
                        if (bt == voxel::AIR || bt == voxel::WATER) continue;
                        bestTop = std::max(bestTop, (float)(by + 1));
                    }
                }
            }

            if (bestTop > -1e8f) {
                return bestTop - cc.y + hs.y + 0.001f;
            }

            return curY;
        } else {
            // Ceiling collision: keep current height and stop vertical velocity.
            return curY;
        }
    }

    // Check if a world position is inside a solid block
    bool isSolidAt(voxel::World* terrain, int x, int y, int z) {
        if (!terrain) return false;
        int bt = terrain->getBlock(x, y, z);
        return bt != 0 && bt != voxel::WATER;
    }

    bool tryStepUpObstacle(voxel::World* terrain, glm::vec3& pos, EnemyComponent* enemy, const glm::vec3& moveDir)
    {
        if (!terrain) return false;

        glm::vec3 ahead = pos + moveDir * 0.65f;
        int bx = (int)std::floor(ahead.x);
        int bz = (int)std::floor(ahead.z);
        int footY = (int)std::floor(pos.y + 0.05f);

        bool wallAhead = isSolidAt(terrain, bx, footY, bz);
        bool clearAbove = !isSolidAt(terrain, bx, footY + 1, bz) && !isSolidAt(terrain, bx, footY + 2, bz);
        if (!wallAhead || !clearAbove) return false;

        // Try stepping up by one block instead of hard jumping to avoid jitter on level transitions.
        glm::vec3 candidate = pos + glm::vec3(moveDir.x * 0.25f, 1.02f, moveDir.z * 0.25f);
        if (collidesWithTerrain(candidate, enemy->colliderCenter, enemy->colliderHalfSize, terrain)) return false;

        pos = candidate;
        enemy->velocity.y = 0.0f;
        enemy->isGrounded = true;
        enemy->jumpCooldown = 0.28f;
        enemy->stuckTimer = 0.0f;
        return true;
    }

    // ── Spawning ──────────────────────────────────────────────
    void trySpawnWave(World* world, voxel::World* terrain, glm::vec3 playerPos) {
        waveNumber++;
        int living = countLivingEnemies(world);
        if (living >= maxEnemies) return;

        int livingCreepers = countLivingEnemiesOfType(world, EnemyType::CREEPER);
        bool forceCreeper = (waveNumber >= 2 && livingCreepers == 0);

        // Minecraft-like pacing: many checks fail, and successful checks spawn small groups.
        if ((std::rand() % 100) < 45) return;

        int toSpawn = 1;
        if (waveNumber > 3 && (std::rand() % 100) < 35) {
            toSpawn = 2;
        }
        toSpawn = std::min(toSpawn, maxEnemies - living);

        for (int i = 0; i < toSpawn; i++) {
            EnemyType type;
            if (forceCreeper && i == 0) {
                type = EnemyType::CREEPER;
            } else {
                int roll = std::rand() % 100;
                if (waveNumber < 2) {
                    type = (roll < 65) ? EnemyType::ZOMBIE : EnemyType::SKELETON;
                } else {
                    if      (roll < 40) type = EnemyType::ZOMBIE;
                    else if (roll < 70) type = EnemyType::SKELETON;
                    else                type = EnemyType::CREEPER;
                }
            }

            bool spawned = spawnEnemy(world, terrain, playerPos, type);
            if (spawned && type == EnemyType::CREEPER) {
                forceCreeper = false;
            }
        }
    }

    bool spawnEnemy(World* world, voxel::World* terrain, glm::vec3 playerPos, EnemyType type) {
        glm::vec3 spawnPos(0.0f);
        bool foundSpawn = false;

        for (int attempt = 0; attempt < 14 && !foundSpawn; ++attempt) {
            float angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
            float distNorm = (float)(std::rand() % 1000) / 999.0f;
            float dist = spawnMinRadius + distNorm * (spawnRadius - spawnMinRadius);

            glm::vec3 candidateXZ = playerPos + glm::vec3(
                std::cos(angle) * dist,
                0.0f,
                std::sin(angle) * dist);

            int cx = (int)std::floor(candidateXZ.x);
            int cz = (int)std::floor(candidateXZ.z);

            if (terrain) {
                for (int y = terrain->height - 2; y >= 1; --y) {
                    int below = terrain->getBlock(cx, y, cz);
                    int feet  = terrain->getBlock(cx, y + 1, cz);
                    int head  = terrain->getBlock(cx, y + 2, cz);

                    bool solidGround = (below != voxel::AIR && below != voxel::WATER);
                    bool clearBody   = (feet == voxel::AIR || feet == voxel::WATER) &&
                                       (head == voxel::AIR || head == voxel::WATER);

                    if (solidGround && clearBody) {
                        spawnPos = glm::vec3((float)cx + 0.5f, (float)(y + 1) + 0.01f, (float)cz + 0.5f);

                        // Avoid clusters that look like over-spawning.
                        bool crowded = false;
                        for (auto* e : world->getEntities()) {
                            auto* existing = e->getComponent<EnemyComponent>();
                            if (!existing || existing->state == EnemyState::DEAD) continue;
                            if (glm::distance(e->localTransform.position, spawnPos) < 5.5f) {
                                crowded = true;
                                break;
                            }
                        }

                        if (!crowded) {
                            foundSpawn = true;
                        }
                        break;
                    }
                }
            } else {
                spawnPos = glm::vec3(candidateXZ.x, std::max(playerPos.y - 5.0f, 1.0f), candidateXZ.z);
                foundSpawn = true;
            }
        }

        if (!foundSpawn) return false;

        Entity* entity = world->add();
        entity->name   = (type == EnemyType::ZOMBIE)   ? "zombie"   :
                         (type == EnemyType::SKELETON)  ? "skeleton" : "creeper";

        entity->localTransform.position = spawnPos;
        entity->localTransform.scale    = glm::vec3(1.0f);

        auto* enemy  = entity->addComponent<EnemyComponent>();
        enemy->type  = type;
        enemy->state = EnemyState::IDLE;
        enemy->previousPosition = spawnPos;  // prevent false stuck-detection on first frame
        enemy->wanderTarget     = spawnPos;   // prevent all enemies rushing to (0,0,0) on first wander

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

            return true;
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

        if (enemy->hurtFlashTimer > 0.0f) {
            enemy->hurtFlashTimer = std::max(0.0f, enemy->hurtFlashTimer - dt);
        }

        glm::vec3& pos = entity->localTransform.position;
        float distToPlayer = glm::length(glm::vec2(playerPos.x - pos.x, playerPos.z - pos.z));

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
            tickCreeper(world, terrain, entity, enemy, player, playerEntity, playerPos, distToPlayer, dt, terrainMeshDirty);
            break;
        }

        if (enemy->jumpCooldown > 0.0f) {
            enemy->jumpCooldown -= dt;
        }

        // Controlled jump-over-obstacle behavior (only when moving and blocked).
        if (terrain && enemy->isGrounded && enemy->jumpCooldown <= 0.0f &&
            (enemy->state == EnemyState::CHASE || enemy->state == EnemyState::ATTACK)) {
            glm::vec3 moveDir{enemy->velocity.x, 0.0f, enemy->velocity.z};
            float movLen = glm::length(moveDir);
            if (movLen > 0.65f) {
                moveDir /= movLen;
                if (!tryStepUpObstacle(terrain, pos, enemy, moveDir)) {
                    glm::vec3 ahead = pos + moveDir * 0.7f;
                    int bx = (int)std::floor(ahead.x);
                    int bz = (int)std::floor(ahead.z);
                    int footY = (int)std::floor(pos.y + 0.05f);

                    int blockAhead = terrain->getBlock(bx, footY, bz);
                    int blockAbove = terrain->getBlock(bx, footY + 1, bz);
                    int blockAboveAbove = terrain->getBlock(bx, footY + 2, bz);

                    bool wallAhead = (blockAhead != voxel::AIR && blockAhead != voxel::WATER);
                    bool clearAbove = (blockAbove == voxel::AIR || blockAbove == voxel::WATER);
                    bool clearAboveAbove = (blockAboveAbove == voxel::AIR || blockAboveAbove == voxel::WATER);

                    if (wallAhead && clearAbove && clearAboveAbove) {
                        enemy->velocity.y = std::max(enemy->velocity.y, 5.9f);
                        enemy->jumpCooldown = 0.82f;
                        enemy->stuckTimer = 0.0f;
                    }
                }
            }
        }

        // If blocked for too long, stop briefly and repick direction through idle wander.
        glm::vec3 moveDir{enemy->velocity.x, 0.0f, enemy->velocity.z};
        float movLen = glm::length(moveDir);
        float hDist = glm::length(glm::vec2(pos.x - enemy->previousPosition.x, pos.z - enemy->previousPosition.z));
        if (movLen > 0.5f && hDist < 0.001f) {
            enemy->stuckTimer += dt;
            if (enemy->stuckTimer > 1.15f) {
                enemy->velocity.x = 0.0f;
                enemy->velocity.z = 0.0f;
                enemy->idleTimer = enemy->idleDuration;
                enemy->stuckTimer = 0.0f;
            }
        } else {
            enemy->stuckTimer = 0.0f;
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

        // Visual movement animation (walk sway) without vertical hopping.
        animateEnemyMotion(entity, enemy, dt);
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
            // Keep skeleton generally advancing toward player, only slight retreat when too close.
            enemy->state = EnemyState::CHASE;
            setHorizontalVelocity(enemy, pos, playerPos);

            if (dist < enemy->attackRange + 0.45f) {
                glm::vec3 awayDir = pos - playerPos;
                awayDir.y = 0.0f;
                float len = glm::length(awayDir);
                if (len > 0.1f) {
                    awayDir /= len;
                    enemy->velocity.x = awayDir.x * enemy->speed * 0.55f;
                    enemy->velocity.z = awayDir.z * enemy->speed * 0.55f;
                }
            }

            // Shoot projectile
            if (enemy->timeSinceShot >= enemy->shootCooldown && dist <= enemy->detectionRange) {
                enemy->timeSinceShot = 0.0f;
                enemy->state = EnemyState::ATTACK;
                enemy->velocity.x *= 0.35f;
                enemy->velocity.z *= 0.35f;
                fireProjectile(world, pos + glm::vec3(0, 1.2f, 0), playerPos, enemy->attackDamage);
            }
        }
    }

    // ─ Creeper ───────────────────────────────────────────────
    void tickCreeper(
        World* world, voxel::World* terrain,
        Entity* entity, EnemyComponent* enemy,
        PlayerComponent* player, Entity* playerEntity,
        glm::vec3 playerPos, float dist, float dt,
        bool& terrainMeshDirty)
    {
        glm::vec3& pos = entity->localTransform.position;

        if (enemy->health <= 0.0f) {
            if (!enemy->isLit) {
                enemy->isLit = true;
                explodeCreeper(entity, enemy, player, playerEntity, playerPos, terrain, terrainMeshDirty);
            }
            enemy->state = EnemyState::DEAD;
            entity->localTransform.scale = glm::vec3(0.5f);
            return;
        }

        if (enemy->isLit) {
            enemy->state = EnemyState::ATTACK;
            enemy->velocity.x *= 0.4f;
            enemy->velocity.z *= 0.4f;
        } else if (dist > enemy->detectionRange) {
            enemy->state = EnemyState::IDLE;
            doIdleWander(entity, enemy, dt);
        } else if (dist <= enemy->attackRange + 0.7f) {
            // Close enough → light the fuse
            enemy->state = EnemyState::ATTACK;
            enemy->velocity.x = 0;
            enemy->velocity.z = 0;
            enemy->isLit = true;
            enemy->fuseTimer = 0.0f;
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
                explodeCreeper(entity, enemy, player, playerEntity, playerPos, terrain, terrainMeshDirty);
                enemy->state = EnemyState::DEAD;
            }
        }
    }

    // ── Utilities ─────────────────────────────────────────────

    void animateEnemyMotion(Entity* entity, EnemyComponent* enemy, float dt) {
        // Keep creeper fuse pulse authoritative while lit.
        if (enemy->type == EnemyType::CREEPER && enemy->isLit) {
            return;
        }

        float horizontalSpeed = glm::length(glm::vec2(enemy->velocity.x, enemy->velocity.z));
        bool moving = horizontalSpeed > 0.2f &&
            (enemy->state == EnemyState::CHASE || enemy->state == EnemyState::ATTACK) &&
            enemy->isGrounded;

        enemy->walkAnimTime += dt * (moving ? (3.5f + horizontalSpeed) : 1.2f);

        float targetRoll = moving
            ? std::sin(enemy->walkAnimTime * 8.0f) * 0.18f
            : std::sin(enemy->walkAnimTime * 1.7f) * 0.01f;
        float targetPitch = moving
            ? std::sin(enemy->walkAnimTime * 4.2f) * 0.04f
            : 0.0f;

        if (!enemy->isGrounded) {
            targetRoll = 0.0f;
            targetPitch = glm::clamp(-enemy->velocity.y * 0.012f, -0.08f, 0.08f);
        }

        float blend = glm::clamp(dt * 10.0f, 0.0f, 1.0f);
        entity->localTransform.rotation.z = glm::mix(entity->localTransform.rotation.z, targetRoll, blend);
        entity->localTransform.rotation.x = glm::mix(entity->localTransform.rotation.x, targetPitch, blend);

        // Keep constant scale to avoid bounce/jump-looking silhouette changes.
        entity->localTransform.scale = glm::vec3(1.0f);
    }

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
        if (len > 1.0f) {
            dir /= len;
            enemy->velocity.x = dir.x * enemy->speed * 0.3f;
            enemy->velocity.z = dir.z * enemy->speed * 0.3f;
        } else {
            // Reached wander target — stop and wait for next idle tick
            enemy->velocity.x = 0;
            enemy->velocity.z = 0;
            enemy->idleTimer = enemy->idleDuration; // force new target next frame
        }
    }

    void dealDamageToPlayer(PlayerComponent* player, float damage, bool bypassRecovery = false) {
        if (!bypassRecovery && player->timeSinceDamage < player->damageRecoveryTime) return;
        player->timeSinceDamage = 0.0f;
        player->damageFlashTimer = 0.3f;  // Red flash for 0.3s
        player->health -= damage;
        if (player->health <= 0.0f) {
            player->health    = 0.0f;
            bool wasAlive = player->isAlive;
            player->isAlive   = false;
            player->gameState = GameState::LOSE;
            if (wasAlive) {
                our::AudioSystem::playSound("assets/sounds/Death.wav");
            }
        }
    }

    // ── Creeper explosion with block destruction ──────────────
    void explodeCreeper(Entity* entity, EnemyComponent* enemy,
                        PlayerComponent* player, Entity* playerEntity, glm::vec3 playerPos,
                        voxel::World* terrain, bool& terrainMeshDirty)
    {
        glm::vec3 center = entity->localTransform.position + enemy->colliderCenter;
        glm::vec3 playerCenter = playerPos;
        if (playerEntity) {
            if (auto* collider = playerEntity->getComponent<AABBColliderComponent>()) {
                playerCenter = playerEntity->localTransform.position + collider->center;
            }
        }

        float dist = glm::distance(center, playerCenter);

        // Damage player
        if (dist <= enemy->explodeRange) {
            float falloff  = 1.0f - glm::clamp(dist / enemy->explodeRange, 0.0f, 1.0f);
            float damageScale = falloff * falloff;
            float damage = enemy->explodeDamage * damageScale;
            dealDamageToPlayer(player, damage, true);
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
