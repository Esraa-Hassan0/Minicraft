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
#include "../components/light.hpp"
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
namespace our
{

    struct ProjectileComponent : public Component
    {
        glm::vec3 velocity{0.0f};
        float damage = 8.0f;
        float lifetime = 4.0f; // seconds before auto-remove
        float elapsed = 0.0f;

        static std::string getID() { return "ProjectileComponent"; }
        void deserialize(const nlohmann::json &) override {}
    };

    struct ExplosionParticleComponent : public Component
    {
        glm::vec3 velocity{0.0f};
        float lifetime = 1.2f; // total seconds before removal
        float elapsed = 0.0f;
        float startScale = 0.3f;
        glm::vec3 baseColor{1.0f, 0.6f, 0.1f}; // orange fire color

        static std::string getID() { return "ExplosionParticleComponent"; }
        void deserialize(const nlohmann::json &) override {}
    };

    // ──────────────────────────────────────────────────────────────
    class EnemySystem
    {
    public:
        // ── Tuning ────────────────────────────────────────────────
        float spawnRadius = 28.0f;    // Spawn enemies this far from player
        float spawnMinRadius = 12.0f; // But not closer than this
        float spawnInterval = 8.0f;   // Seconds between spawn checks (was 12)
        int maxEnemies = 10;          // Soft cap on living enemies (was 7)

        // ── Internal state ────────────────────────────────────────
        float spawnTimer = 0.0f;
        int waveNumber = 0;

        // Cached meshes (created once, shared across all enemy entities of that type)
        Mesh *zombieMesh = nullptr;
        Mesh *skeletonMesh = nullptr;
        Mesh *creeperMesh = nullptr;
        Mesh *projectileMesh = nullptr;

        // ── Lifecycle ─────────────────────────────────────────────
        void initialize()
        {
            zombieMesh = enemy_mesh::buildZombieMesh();
            skeletonMesh = enemy_mesh::buildSkeletonMesh();
            creeperMesh = enemy_mesh::buildCreeperMesh();
            projectileMesh = enemy_mesh::buildProjectileMesh();
            spawnTimer = spawnInterval * 0.5f; // First wave comes a bit earlier
        }

        void destroy()
        {
            delete zombieMesh;
            zombieMesh = nullptr;
            delete skeletonMesh;
            skeletonMesh = nullptr;
            delete creeperMesh;
            creeperMesh = nullptr;
            delete projectileMesh;
            projectileMesh = nullptr;
        }

        // ── Main update (call every frame from play-state onDraw) ─
        void update(World *world, voxel::World *terrain,
                    const glm::vec3 &playerPos, float dt,
                    bool &terrainMeshDirty, bool isNight = true)
        {
            Entity *playerEntity = findPlayer(world);
            if (!playerEntity)
                return;

            auto *player = playerEntity->getComponent<PlayerComponent>();
            if (!player)
                return;
            player->timeSinceDamage += dt;

            // If game is already over, skip AI
            if (player->gameState != GameState::PLAYING)
                return;

            // Spawn wave logic
            spawnTimer += dt;
            if (spawnTimer >= spawnInterval)
            {
                spawnTimer = 0.0f;
                trySpawnWave(world, terrain, playerPos, isNight);
            }

            // Collect entities to remove after iterating
            std::vector<Entity *> toRemove;

            for (Entity *entity : world->getEntities())
            {
                auto *enemy = entity->getComponent<EnemyComponent>();
                if (!enemy)
                    continue;

                tickEnemy(world, terrain, entity, enemy, player, playerEntity,
                          playerPos, dt, toRemove, terrainMeshDirty);
            }

            // Tick projectiles
            for (Entity *entity : world->getEntities())
            {
                auto *proj = entity->getComponent<ProjectileComponent>();
                if (!proj)
                    continue;

                tickProjectile(world, terrain, entity, proj, player, playerPos, dt, toRemove);
            }

            // Tick explosion particles
            for (Entity *entity : world->getEntities())
            {
                auto *particle = entity->getComponent<ExplosionParticleComponent>();
                if (!particle)
                    continue;

                tickExplosionParticle(entity, particle, dt, toRemove);
            }

            // Remove dead entities
            for (Entity *e : toRemove)
                world->markForRemoval(e);

            world->deleteMarkedEntities();
        }

    private:
        // ── Entity helpers ────────────────────────────────────────
        Entity *findPlayer(World *world)
        {
            for (auto *e : world->getEntities())
            {
                if (e->getComponent<PlayerComponent>())
                    return e;
            }
            return nullptr;
        }

        int countLivingEnemies(World *world)
        {
            int count = 0;
            for (auto *e : world->getEntities())
            {
                auto *en = e->getComponent<EnemyComponent>();
                if (en && en->state != EnemyState::DEAD)
                    count++;
            }
            return count;
        }

        int countLivingEnemiesOfType(World *world, EnemyType type)
        {
            int count = 0;
            for (auto *e : world->getEntities())
            {
                auto *en = e->getComponent<EnemyComponent>();
                if (en && en->state != EnemyState::DEAD && en->type == type)
                    count++;
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
            glm::vec3 &pos, EnemyComponent *enemy, voxel::World *terrain, float dt)
        {
            if (!terrain)
                return;

            // Apply gravity (ourCraft uses symplectic Euler; we keep simple Euler)
            enemy->velocity.y -= enemy->gravityAccel * dt;
            if (enemy->velocity.y < -30.0f)
                enemy->velocity.y = -30.0f;

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
                if (!collidesWithTerrain(testPos, cc, hs, terrain))
                {
                    pos.x = testPos.x;
                }
                else
                {
                    enemy->velocity.x = 0;
                    // Snap to block edge (ourCraft approach: no residual penetration)
                    pos.x = snapToBlockEdge(pos.x, dx, cc.x, hs.x);
                }
            }

            // ── Z axis: test (pos.x, lastPos.y, pos.z+dz) vs lastPos ──
            {
                glm::vec3 testPos = {pos.x, lastPos.y, pos.z + dz};
                if (!collidesWithTerrain(testPos, cc, hs, terrain))
                {
                    pos.z = testPos.z;
                }
                else
                {
                    enemy->velocity.z = 0;
                    pos.z = snapToBlockEdge(pos.z, dz, cc.z, hs.z);
                }
            }

            // ── Y axis: test (pos.x, pos.y+dy, pos.z) ──
            {
                glm::vec3 testPos = {pos.x, pos.y + dy, pos.z};
                if (!collidesWithTerrain(testPos, cc, hs, terrain))
                {
                    pos.y = testPos.y;
                }
                else
                {
                    if (dy < 0)
                    {
                        // Hit floor → snap up, mark grounded
                        enemy->isGrounded = true;
                        pos.y = snapToBlockEdgeY(pos.y, dy, pos.x, pos.z, cc, hs, terrain);
                    }
                    else
                    {
                        // Hit ceiling → stop
                        pos.y = snapToBlockEdgeY(pos.y, dy, pos.x, pos.z, cc, hs, terrain);
                    }
                    enemy->velocity.y = 0;
                }
            }
        }

        // Returns true if the collider at 'pos' overlaps any solid block
        bool collidesWithTerrain(const glm::vec3 &pos, const glm::vec3 &cc,
                                 const glm::vec3 &hs, voxel::World *terrain)
        {
            glm::vec3 mn = pos + cc - hs;
            glm::vec3 mx = pos + cc + hs;
            int x0 = (int)std::floor(mn.x), x1 = (int)std::floor(mx.x);
            int y0 = (int)std::floor(mn.y), y1 = (int)std::floor(mx.y);
            int z0 = (int)std::floor(mn.z), z1 = (int)std::floor(mx.z);
            for (int bx = x0; bx <= x1; bx++)
                for (int by = y0; by <= y1; by++)
                    for (int bz = z0; bz <= z1; bz++)
                    {
                        int bt = terrain->getBlock(bx, by, bz);
                        if (bt != 0 && bt != voxel::WATER)
                            return true;
                    }
            return false;
        }

        // Snap horizontal axis to the nearest block face after collision
        float snapToBlockEdge(float cur, float delta, float ccAxis, float hsAxis)
        {
            if (delta > 0)
                return std::floor(cur + ccAxis + hsAxis) - ccAxis - hsAxis - 0.001f;
            else
                return std::ceil(cur + ccAxis - hsAxis) - ccAxis + hsAxis + 0.001f;
        }

        // Snap Y to the nearest block face (floor or ceiling)
        float snapToBlockEdgeY(float curY, float delta,
                               float posX, float posZ,
                               const glm::vec3 &cc,
                               const glm::vec3 &hs,
                               voxel::World *terrain)
        {
            if (!terrain)
                return curY;

            int x0 = (int)std::floor(posX + cc.x - hs.x);
            int x1 = (int)std::floor(posX + cc.x + hs.x);
            int z0 = (int)std::floor(posZ + cc.z - hs.z);
            int z1 = (int)std::floor(posZ + cc.z + hs.z);

            if (delta < 0)
            {
                float bottom = curY + delta + cc.y - hs.y;
                int yStart = (int)std::floor(bottom);
                int yEnd = yStart + 3;

                float bestTop = -1e9f;
                for (int by = yStart; by <= yEnd; ++by)
                {
                    for (int bx = x0; bx <= x1; ++bx)
                    {
                        for (int bz = z0; bz <= z1; ++bz)
                        {
                            int bt = terrain->getBlock(bx, by, bz);
                            if (bt == voxel::AIR || bt == voxel::WATER)
                                continue;
                            bestTop = std::max(bestTop, (float)(by + 1));
                        }
                    }
                }

                if (bestTop > -1e8f)
                {
                    return bestTop - cc.y + hs.y + 0.001f;
                }

                return curY;
            }
            else
            {
                // Ceiling collision: keep current height and stop vertical velocity.
                return curY;
            }
        }

        // Check if a world position is inside a solid block
        bool isSolidAt(voxel::World *terrain, int x, int y, int z)
        {
            if (!terrain)
                return false;
            int bt = terrain->getBlock(x, y, z);
            return bt != 0 && bt != voxel::WATER;
        }

        bool tryStepUpObstacle(voxel::World *terrain, glm::vec3 &pos, EnemyComponent *enemy, const glm::vec3 &moveDir)
        {
            if (!terrain)
                return false;

            glm::vec3 ahead = pos + moveDir * 0.65f;
            int bx = (int)std::floor(ahead.x);
            int bz = (int)std::floor(ahead.z);
            int footY = (int)std::floor(pos.y + 0.05f);

            bool wallAhead = isSolidAt(terrain, bx, footY, bz);
            bool clearAbove = !isSolidAt(terrain, bx, footY + 1, bz) && !isSolidAt(terrain, bx, footY + 2, bz);
            if (!wallAhead || !clearAbove)
                return false;

            // Try stepping up by one block instead of hard jumping to avoid jitter on level transitions.
            glm::vec3 candidate = pos + glm::vec3(moveDir.x * 0.25f, 1.02f, moveDir.z * 0.25f);
            if (collidesWithTerrain(candidate, enemy->colliderCenter, enemy->colliderHalfSize, terrain))
                return false;

            pos = candidate;
            enemy->velocity.y = 0.0f;
            enemy->isGrounded = true;
            enemy->jumpCooldown = 0.28f;
            enemy->stuckTimer = 0.0f;
            return true;
        }

        // ── Spawning ──────────────────────────────────────────────
        void trySpawnWave(World *world, voxel::World *terrain, glm::vec3 playerPos, bool isNight)
        {
            waveNumber++;
            int living = countLivingEnemies(world);
            if (living >= maxEnemies)
                return;

            int livingCreepers = countLivingEnemiesOfType(world, EnemyType::CREEPER);
            bool forceCreeper = (waveNumber >= 2 && livingCreepers == 0);

            // Minecraft-like pacing: many checks fail, and successful checks spawn small groups.
            // Enemies appear much more frequently at night.
            int skipChance = isNight ? 10 : 75; 
            if ((std::rand() % 100) < skipChance)
                return;

            int toSpawn = 1;
            if (waveNumber > 2 && (std::rand() % 100) < 45)
            {
                toSpawn = 2;
            }
            if (waveNumber > 5 && (std::rand() % 100) < 25)
            {
                toSpawn = 3;
            }
            toSpawn = std::min(toSpawn, maxEnemies - living);

            for (int i = 0; i < toSpawn; i++)
            {
                EnemyType type;
                if (forceCreeper && i == 0)
                {
                    type = EnemyType::CREEPER;
                }
                else
                {
                    int roll = std::rand() % 100;
                    if (waveNumber < 2)
                    {
                        // No creepers early - 50/50 zombie/skeleton
                        type = (roll < 50) ? EnemyType::ZOMBIE : EnemyType::SKELETON;
                    }
                    else
                    {
                        // 35% zombie, 35% skeleton, 30% creeper (was 40/30/30)
                        if (roll < 35)
                            type = EnemyType::ZOMBIE;
                        else if (roll < 70)
                            type = EnemyType::SKELETON;
                        else
                            type = EnemyType::CREEPER;
                    }
                }

                bool spawned = spawnEnemy(world, terrain, playerPos, type);
                if (spawned && type == EnemyType::CREEPER)
                {
                    forceCreeper = false;
                }
            }
        }

        bool spawnEnemy(World *world, voxel::World *terrain, glm::vec3 playerPos, EnemyType type)
        {
            glm::vec3 spawnPos(0.0f);
            bool foundSpawn = false;

            for (int attempt = 0; attempt < 14 && !foundSpawn; ++attempt)
            {
                float angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
                float distNorm = (float)(std::rand() % 1000) / 999.0f;
                float dist = spawnMinRadius + distNorm * (spawnRadius - spawnMinRadius);

                glm::vec3 candidateXZ = playerPos + glm::vec3(
                                                        std::cos(angle) * dist,
                                                        0.0f,
                                                        std::sin(angle) * dist);

                int cx = (int)std::floor(candidateXZ.x);
                int cz = (int)std::floor(candidateXZ.z);

                if (terrain)
                {
                    for (int y = terrain->height - 2; y >= 1; --y)
                    {
                        int below = terrain->getBlock(cx, y, cz);
                        int feet = terrain->getBlock(cx, y + 1, cz);
                        int head = terrain->getBlock(cx, y + 2, cz);

                        bool solidGround = (below != voxel::AIR && below != voxel::WATER);
                        bool clearBody = (feet == voxel::AIR || feet == voxel::WATER) &&
                                         (head == voxel::AIR || head == voxel::WATER);

                        if (solidGround && clearBody)
                        {
                            spawnPos = glm::vec3((float)cx + 0.5f, (float)(y + 1) + 0.01f, (float)cz + 0.5f);

                            // Avoid clusters that look like over-spawning.
                            bool crowded = false;
                            for (auto *e : world->getEntities())
                            {
                                auto *existing = e->getComponent<EnemyComponent>();
                                if (!existing || existing->state == EnemyState::DEAD)
                                    continue;
                                if (glm::distance(e->localTransform.position, spawnPos) < 5.5f)
                                {
                                    crowded = true;
                                    break;
                                }
                            }

                            if (!crowded)
                            {
                                foundSpawn = true;
                            }
                            break;
                        }
                    }
                }
                else
                {
                    spawnPos = glm::vec3(candidateXZ.x, std::max(playerPos.y - 5.0f, 1.0f), candidateXZ.z);
                    foundSpawn = true;
                }
            }

            if (!foundSpawn)
                return false;

            Entity *entity = world->add();
            entity->name = (type == EnemyType::ZOMBIE) ? "zombie" : (type == EnemyType::SKELETON) ? "skeleton"
                                                                                                  : "creeper";

            entity->localTransform.position = spawnPos;
            entity->localTransform.scale = glm::vec3(1.0f);

            auto *enemy = entity->addComponent<EnemyComponent>();
            enemy->type = type;
            enemy->state = EnemyState::IDLE;
            enemy->previousPosition = spawnPos; // prevent false stuck-detection on first frame
            enemy->wanderTarget = spawnPos;     // prevent all enemies rushing to (0,0,0) on first wander

            switch (type)
            {
            case EnemyType::ZOMBIE:
                enemy->maxHealth = 20.0f;
                enemy->health = 20.0f;
                enemy->speed = 3.2f;  // was 2.8
                enemy->attackDamage = 8.0f;
                enemy->attackRange = 1.6f;
                break;
            case EnemyType::SKELETON:
                enemy->maxHealth = 15.0f;
                enemy->health = 15.0f;
                enemy->speed = 2.8f;  // was 2.2
                enemy->attackDamage = 6.0f;
                enemy->attackRange = 1.2f;
                enemy->preferredRange = 10.0f;
                break;
            case EnemyType::CREEPER:
                enemy->maxHealth = 25.0f;
                enemy->health = 25.0f;
                enemy->speed = 3.0f;
                enemy->explodeDamage = 45.0f;
                enemy->explodeRange = 3.0f;
                enemy->attackRange = 1.8f;
                break;
            }

            auto *mr = entity->addComponent<MeshRendererComponent>();
            mr->mesh = getMeshForType(type);

            switch (type)
            {
            case EnemyType::ZOMBIE:
                mr->material = AssetLoader<Material>::get("enemy-zombie");
                if (!mr->material)
                    mr->material = AssetLoader<Material>::get("default");
                break;
            case EnemyType::SKELETON:
                mr->material = AssetLoader<Material>::get("enemy-skeleton");
                if (!mr->material)
                    mr->material = AssetLoader<Material>::get("default");
                break;
            case EnemyType::CREEPER:
                mr->material = AssetLoader<Material>::get("enemy-creeper");
                if (!mr->material)
                    mr->material = AssetLoader<Material>::get("default");
                break;
            }

            return true;
        }

        Mesh *getMeshForType(EnemyType type)
        {
            switch (type)
            {
            case EnemyType::ZOMBIE:
                return zombieMesh;
            case EnemyType::SKELETON:
                return skeletonMesh;
            case EnemyType::CREEPER:
                return creeperMesh;
            }
            return zombieMesh;
        }

        // ── Per-enemy AI tick ─────────────────────────────────────
        void tickEnemy(
            World *world, voxel::World *terrain,
            Entity *entity, EnemyComponent *enemy,
            PlayerComponent *player, Entity *playerEntity,
            glm::vec3 playerPos,
            float dt,
            std::vector<Entity *> &toRemove,
            bool &terrainMeshDirty)
        {
            if (enemy->state == EnemyState::DEAD)
            {
                enemy->deadTimer += dt;
                entity->localTransform.position.y -= dt * 0.8f;
                if (enemy->deadTimer >= enemy->deadDuration)
                    toRemove.push_back(entity);
                return;
            }

            if (enemy->hurtFlashTimer > 0.0f)
            {
                enemy->hurtFlashTimer = std::max(0.0f, enemy->hurtFlashTimer - dt);
            }

            glm::vec3 &pos = entity->localTransform.position;
            float distToPlayer = glm::length(glm::vec2(playerPos.x - pos.x, playerPos.z - pos.z));

            // Attack cooldown tick
            enemy->timeSinceAttack += dt;
            enemy->timeSinceShot += dt;

            // ── State machine ────────────────────────────────────
            switch (enemy->type)
            {
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

            if (enemy->jumpCooldown > 0.0f)
            {
                enemy->jumpCooldown -= dt;
            }

            // Controlled jump-over-obstacle behavior (only when moving and blocked).
            if (terrain && enemy->isGrounded && enemy->jumpCooldown <= 0.0f &&
                (enemy->state == EnemyState::CHASE || enemy->state == EnemyState::ATTACK))
            {
                glm::vec3 moveDir{enemy->velocity.x, 0.0f, enemy->velocity.z};
                float movLen = glm::length(moveDir);
                if (movLen > 0.65f)
                {
                    moveDir /= movLen;
                    if (!tryStepUpObstacle(terrain, pos, enemy, moveDir))
                    {
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

                        if (wallAhead && clearAbove && clearAboveAbove)
                        {
                            enemy->velocity.y = std::max(enemy->velocity.y, 7.5f); // was 5.9
                            enemy->jumpCooldown = 0.65f; // was 0.82
                            enemy->stuckTimer = 0.0f;
                        }
                    }
                }
            }

            // If blocked for too long, stop briefly and repick direction through idle wander.
            glm::vec3 moveDir{enemy->velocity.x, 0.0f, enemy->velocity.z};
            float movLen = glm::length(moveDir);
            float hDist = glm::length(glm::vec2(pos.x - enemy->previousPosition.x, pos.z - enemy->previousPosition.z));
            if (movLen > 0.5f && hDist < 0.001f)
            {
                enemy->stuckTimer += dt;
                if (enemy->stuckTimer > 0.8f) // was 1.15
                {
                    enemy->velocity.x = 0.0f;
                    enemy->velocity.z = 0.0f;
                    enemy->idleTimer = enemy->idleDuration;
                    enemy->stuckTimer = 0.0f;
                }
            }
            else
            {
                enemy->stuckTimer = 0.0f;
            }

            // Face the player when chasing/attacking
            if (enemy->state == EnemyState::CHASE || enemy->state == EnemyState::ATTACK)
            {
                glm::vec3 dir = playerPos - pos;
                dir.y = 0.0f;
                if (glm::length(dir) > 0.001f)
                {
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
            Entity *entity, EnemyComponent *enemy,
            PlayerComponent *player, Entity *playerEntity,
            glm::vec3 playerPos, float dist, float dt)
        {
            glm::vec3 &pos = entity->localTransform.position;

            if (enemy->health <= 0.0f)
            {
                enemy->state = EnemyState::DEAD;
                entity->localTransform.scale = glm::vec3(0.85f);
                return;
            }

            if (dist > enemy->detectionRange)
            {
                enemy->state = EnemyState::IDLE;
                doIdleWander(entity, enemy, dt);
            }
            else if (dist <= enemy->attackRange)
            {
                // AABB melee check
                enemy->state = EnemyState::ATTACK;
                if (enemy->timeSinceAttack >= enemy->attackCooldown)
                {
                    // Check AABB overlap with player
                    auto *playerCollider = playerEntity->getComponent<AABBColliderComponent>();
                    if (playerCollider)
                    {
                        glm::vec3 enemyMin = pos + enemy->colliderCenter - enemy->colliderHalfSize;
                        glm::vec3 enemyMax = pos + enemy->colliderCenter + enemy->colliderHalfSize;
                        glm::vec3 playerMin = playerCollider->getMinCorner(playerEntity->localTransform.position);
                        glm::vec3 playerMax = playerCollider->getMaxCorner(playerEntity->localTransform.position);

                        bool overlap = (enemyMin.x < playerMax.x && enemyMax.x > playerMin.x) &&
                                       (enemyMin.y < playerMax.y && enemyMax.y > playerMin.y) &&
                                       (enemyMin.z < playerMax.z && enemyMax.z > playerMin.z);

                        if (overlap || dist <= enemy->attackRange)
                        {
                            enemy->timeSinceAttack = 0.0f;
                            dealDamageToPlayer(player, enemy->attackDamage);
                            our::AudioSystem::playSound("assets/sounds/Hit.wav");
                        }
                    }
                    else
                    {
                        // Fallback: distance-based
                        enemy->timeSinceAttack = 0.0f;
                        dealDamageToPlayer(player, enemy->attackDamage);
                    }
                }
            }
            else
            {
                // Chase via velocity (collision resolved later)
                enemy->state = EnemyState::CHASE;
                setHorizontalVelocity(enemy, pos, playerPos);
            }
        }

        // ─ Skeleton ──────────────────────────────────────────────
        void tickSkeleton(
            World *world,
            Entity *entity, EnemyComponent *enemy,
            PlayerComponent *player, Entity *playerEntity,
            glm::vec3 playerPos, float dist, float dt)
        {
            glm::vec3 &pos = entity->localTransform.position;

            if (enemy->health <= 0.0f)
            {
                enemy->state = EnemyState::DEAD;
                entity->localTransform.scale = glm::vec3(0.85f);
                return;
            }

            if (dist > enemy->detectionRange)
            {
                enemy->state = EnemyState::IDLE;
                doIdleWander(entity, enemy, dt);
            }
            else
            {
                // Keep skeleton generally advancing toward player, only slight retreat when too close.
                enemy->state = EnemyState::CHASE;
                setHorizontalVelocity(enemy, pos, playerPos);

                if (dist < enemy->attackRange + 0.45f)
                {
                    glm::vec3 awayDir = pos - playerPos;
                    awayDir.y = 0.0f;
                    float len = glm::length(awayDir);
                    if (len > 0.1f)
                    {
                        awayDir /= len;
                        enemy->velocity.x = awayDir.x * enemy->speed * 0.55f;
                        enemy->velocity.z = awayDir.z * enemy->speed * 0.55f;
                    }
                }

                // Shoot projectile
                if (enemy->timeSinceShot >= enemy->shootCooldown && dist <= enemy->detectionRange)
                {
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
            World *world, voxel::World *terrain,
            Entity *entity, EnemyComponent *enemy,
            PlayerComponent *player, Entity *playerEntity,
            glm::vec3 playerPos, float dist, float dt,
            bool &terrainMeshDirty)
        {
            glm::vec3 &pos = entity->localTransform.position;

            if (enemy->health <= 0.0f)
            {
                if (!enemy->isLit)
                {
                    enemy->isLit = true;
                    explodeCreeper(entity, enemy, player, playerEntity, playerPos, terrain, terrainMeshDirty);
                }
                enemy->state = EnemyState::DEAD;
                entity->localTransform.scale = glm::vec3(0.5f);
                return;
            }

            if (enemy->isLit)
            {
                enemy->state = EnemyState::ATTACK;
                enemy->velocity.x *= 0.4f;
                enemy->velocity.z *= 0.4f;
            }
            else if (dist > enemy->detectionRange)
            {
                enemy->state = EnemyState::IDLE;
                doIdleWander(entity, enemy, dt);
            }
            else if (dist <= enemy->attackRange + 0.7f)
            {
                // Close enough → light the fuse
                enemy->state = EnemyState::ATTACK;
                enemy->velocity.x = 0;
                enemy->velocity.z = 0;
                enemy->isLit = true;
                enemy->fuseTimer = 0.0f;
            }
            else
            {
                // Chase player
                enemy->state = EnemyState::CHASE;
                setHorizontalVelocity(enemy, pos, playerPos);
            }

            // Fuse ticking
            if (enemy->isLit)
            {
                enemy->fuseTimer += dt;

                auto *light = entity->getComponent<LightComponent>();
                if (!light) {
                    light = entity->addComponent<LightComponent>();
                    light->type = LightType::SPOT;
                    light->ambient = glm::vec3(0.1f, 0.0f, 0.0f);
                    light->attenConstant = 1.0f;
                    light->attenLinear = 0.09f;
                    light->attenQuadratic = 0.032f;
                    light->enabled = true;
                }

                // Flash the creeper (scale pulse)
                float pulse = std::sin(enemy->fuseTimer * 10.0f) * 0.1f + 1.0f;
                entity->localTransform.scale = glm::vec3(pulse);

                // Pulse the creeper alarm spotlight (red, sweeping slightly)
                if (light) {
                    light->color = glm::vec3(pulse * 3.0f, 0.1f, 0.1f);
                    light->innerCutoffDeg = 20.0f + pulse * 10.0f;
                    light->outerCutoffDeg = 35.0f + pulse * 15.0f;
                }

                if (enemy->fuseTimer >= enemy->fuseTime)
                {
                    // BOOM
                    explodeCreeper(entity, enemy, player, playerEntity, playerPos, terrain, terrainMeshDirty);
                    enemy->state = EnemyState::DEAD;
                }
            }
        }

        // ── Utilities ─────────────────────────────────────────────

        void animateEnemyMotion(Entity *entity, EnemyComponent *enemy, float dt)
        {
            // Keep creeper fuse pulse authoritative while lit.
            if (enemy->type == EnemyType::CREEPER && enemy->isLit)
            {
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

            if (!enemy->isGrounded)
            {
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
        void setHorizontalVelocity(EnemyComponent *enemy, glm::vec3 pos, glm::vec3 target)
        {
            glm::vec3 dir = target - pos;
            dir.y = 0.0f;
            float len = glm::length(dir);
            if (len > 0.1f)
            {
                dir /= len;
                enemy->velocity.x = dir.x * enemy->speed;
                enemy->velocity.z = dir.z * enemy->speed;
            }
            else
            {
                enemy->velocity.x = 0;
                enemy->velocity.z = 0;
            }
        }

        void doIdleWander(Entity *entity, EnemyComponent *enemy, float dt)
        {
            enemy->idleTimer += dt;
            if (enemy->idleTimer >= enemy->idleDuration)
            {
                enemy->idleTimer = 0.0f;
                float angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
                float dist = 1.0f + (float)(std::rand() % 3);
                enemy->wanderTarget = entity->localTransform.position +
                                      glm::vec3(std::cos(angle) * dist, 0, std::sin(angle) * dist);
            }
            glm::vec3 dir = enemy->wanderTarget - entity->localTransform.position;
            dir.y = 0.0f;
            float len = glm::length(dir);
            if (len > 1.0f)
            {
                dir /= len;
                enemy->velocity.x = dir.x * enemy->speed * 0.3f;
                enemy->velocity.z = dir.z * enemy->speed * 0.3f;
            }
            else
            {
                // Reached wander target — stop and wait for next idle tick
                enemy->velocity.x = 0;
                enemy->velocity.z = 0;
                enemy->idleTimer = enemy->idleDuration; // force new target next frame
            }
        }

        void dealDamageToPlayer(PlayerComponent *player, float damage, bool bypassRecovery = false)
        {
            if (!bypassRecovery && player->timeSinceDamage < player->damageRecoveryTime)
                return;
            player->timeSinceDamage = 0.0f;
            player->shakeTimer = 0.3f;
            player->shakeIntensity = 0.2f;
            player->health -= damage;
            our::AudioSystem::playSound("assets/sounds/life_loss.mp3");
            if (player->health <= 0.0f)
            {
                player->health = 0.0f;
                bool wasAlive = player->isAlive;
                player->isAlive = false;
                player->gameState = GameState::LOSE;
                if (wasAlive)
                {
                    our::AudioSystem::playSound("assets/sounds/Death.wav");
                }
            }
        }

        // ── Creeper explosion with block destruction ──────────────
        void explodeCreeper(Entity *entity, EnemyComponent *enemy,
                            PlayerComponent *player, Entity *playerEntity, glm::vec3 playerPos,
                            voxel::World *terrain, bool &terrainMeshDirty)
        {
            glm::vec3 center = entity->localTransform.position + enemy->colliderCenter;
            glm::vec3 playerCenter = playerPos;
            if (playerEntity)
            {
                if (auto *collider = playerEntity->getComponent<AABBColliderComponent>())
                {
                    playerCenter = playerEntity->localTransform.position + collider->center;
                }
            }

            float dist = glm::distance(center, playerCenter);

            // Damage player
            if (dist <= enemy->explodeRange)
            {
                float falloff = 1.0f - glm::clamp(dist / enemy->explodeRange, 0.0f, 1.0f);
                float damageScale = falloff * falloff;
                float damage = enemy->explodeDamage * damageScale;
                dealDamageToPlayer(player, damage, true);
            }

            // Destroy blocks in a sphere
            if (terrain)
            {
                int radius = (int)std::ceil(enemy->explodeRange);
                int cx = (int)std::floor(center.x);
                int cy = (int)std::floor(center.y);
                int cz = (int)std::floor(center.z);

                for (int dx = -radius; dx <= radius; dx++)
                    for (int dy = -radius; dy <= radius; dy++)
                        for (int dz = -radius; dz <= radius; dz++)
                        {
                            float distSq = (float)(dx * dx + dy * dy + dz * dz);
                            if (distSq > enemy->explodeRange * enemy->explodeRange)
                                continue;

                            int bx = cx + dx, by = cy + dy, bz = cz + dz;
                            if (by < 1)
                                continue; // Don't destroy bedrock layer

                            int bt = terrain->getBlock(bx, by, bz);
                            if (bt != 0 && bt != voxel::WATER)
                            {
                                terrain->setBlock(bx, by, bz, voxel::AIR);
                            }
                        }
                terrainMeshDirty = true;
            }

            // Self-destruct
            enemy->health = 0.0f;
            our::AudioSystem::playSound("assets/sounds/Glass.wav"); // explosion sound

            // Spawn visual explosion particles
            spawnExplosionParticles(entity->getWorld(), center, enemy->explodeRange);

            // Strong camera shake for explosion impact
            player->shakeTimer = 0.8f;
            player->shakeIntensity = 0.6f;
        }

        // ── Explosion particles ────────────────────────────────────
        void spawnExplosionParticles(World *world, glm::vec3 center, float radius)
        {
            if (!world)
                return;

            // Color palette: fiery orange, yellow, red, dark smoke
            struct ParticleColor
            {
                float r, g, b;
                float scale;
                float life;
            };
            ParticleColor colors[] = {
                {1.0f, 0.85f, 0.1f, 0.35f, 1.0f}, // bright yellow
                {1.0f, 0.5f, 0.0f, 0.30f, 1.2f},  // orange
                {1.0f, 0.3f, 0.0f, 0.25f, 1.0f},  // dark orange
                {0.9f, 0.15f, 0.0f, 0.28f, 0.9f}, // red-orange
                {1.0f, 1.0f, 0.3f, 0.22f, 0.8f},  // light yellow
                {0.3f, 0.3f, 0.3f, 0.20f, 1.5f},  // smoke/dark
            };
            int numColors = 6;

            int numParticles = 25;
            for (int i = 0; i < numParticles; i++)
            {
                Entity *p = world->add();
                p->name = "explosion_particle";

                // Random direction outward
                float theta = static_cast<float>(std::rand() % 360) * 3.14159f / 180.0f;
                float phi = static_cast<float>(std::rand() % 180) * 3.14159f / 180.0f;
                float speed = 2.0f + static_cast<float>(std::rand() % 100) / 100.0f * 6.0f;
                glm::vec3 dir(
                    std::sin(phi) * std::cos(theta),
                    std::cos(phi) * 0.5f + 0.3f, // bias upward
                    std::sin(phi) * std::sin(theta));
                glm::vec3 vel = glm::normalize(dir) * speed;

                // Small random offset from center
                glm::vec3 offset(
                    (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * radius * 0.5f,
                    (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * radius * 0.5f,
                    (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * radius * 0.5f);

                const auto &col = colors[std::rand() % numColors];

                p->localTransform.position = center + offset;
                p->localTransform.scale = glm::vec3(col.scale);

                auto *particle = p->addComponent<ExplosionParticleComponent>();
                particle->velocity = vel;
                particle->lifetime = col.life + static_cast<float>(std::rand() % 50) / 100.0f;
                particle->startScale = col.scale;
                particle->baseColor = glm::vec3(col.r, col.g, col.b);

                auto *mr = p->addComponent<MeshRendererComponent>();
                mr->mesh = AssetLoader<Mesh>::get("cube");
                mr->material = AssetLoader<Material>::get("explosion-fire");
                if (!mr->material)
                    mr->material = AssetLoader<Material>::get("metal");

                // Attach spot light to select particles for intense sweeping flame beams
                if ((i & 2) == 0)
                {
                    auto *light = p->addComponent<LightComponent>();
                    light->type = LightType::SPOT; // Changed to SPOT light
                    // HDR-like bright fire color (multiplied for intensity)
                    light->color = glm::vec3(col.r * 3.0f, col.g * 2.0f, col.b * 0.8f);
                    light->ambient = glm::vec3(0.4f, 0.2f, 0.05f);
                    // Low attenuation = light reaches further
                    light->attenConstant = 1.0f;
                    light->attenLinear = 0.09f;
                    light->attenQuadratic = 0.032f;
                    
                    // Spot light cone angles
                    light->innerCutoffDeg = 15.0f;
                    light->outerCutoffDeg = 45.0f;

                    light->enabled = true;
                }
            }
        }

        void tickExplosionParticle(Entity *entity, ExplosionParticleComponent *particle,
                                   float dt, std::vector<Entity *> &toRemove)
        {
            particle->elapsed += dt;
            if (particle->elapsed >= particle->lifetime)
            {
                toRemove.push_back(entity);
                return;
            }

            // Move with velocity
            entity->localTransform.position += particle->velocity * dt;

            // Apply gravity (particles arc downward)
            particle->velocity.y -= 8.0f * dt;

            // Slow down horizontal movement (air resistance)
            particle->velocity.x *= (1.0f - 1.5f * dt);
            particle->velocity.z *= (1.0f - 1.5f * dt);

            // Shrink over lifetime
            float t = particle->elapsed / particle->lifetime;
            float scale = particle->startScale * (1.0f - t * t); // quadratic fade
            entity->localTransform.scale = glm::vec3(std::max(0.02f, scale));

            // Add slight spin for visual interest
            entity->localTransform.rotation.y += dt * 5.0f;
            entity->localTransform.rotation.x += dt * 3.0f;

            // Fade point light intensity over lifetime with flicker
            auto *light = entity->getComponent<LightComponent>();
            if (light)
            {
                float brightness = (1.0f - t) * (1.0f - t); // quadratic fade
                // Flicker effect: random-ish sine pulse simulating fire
                float flicker = 0.85f + 0.15f * std::sin(particle->elapsed * 25.0f + particle->startScale * 100.0f);
                brightness *= flicker;
                light->color = particle->baseColor * 3.0f * brightness;
                light->ambient = particle->baseColor * 0.4f * brightness;
                if (t > 0.85f)
                    light->enabled = false;
            }
        }

        // ── Projectile with terrain collision ──────────────────────
        void fireProjectile(World *world, glm::vec3 origin, glm::vec3 target, float damage)
        {
            glm::vec3 dir = glm::normalize(target - origin);

            Entity *arrow = world->add();
            arrow->name = "arrow";
            arrow->localTransform.position = origin;

            float yaw = std::atan2(dir.x, dir.z);
            float pitch = std::asin(-dir.y);
            arrow->localTransform.rotation = glm::vec3(pitch, yaw, 0.0f);

            auto *proj = arrow->addComponent<ProjectileComponent>();
            proj->velocity = dir * 15.0f;
            proj->damage = damage;

            auto *mr = arrow->addComponent<MeshRendererComponent>();
            mr->mesh = projectileMesh;
            mr->material = AssetLoader<Material>::get("metal");
            if (!mr->material)
                mr->material = AssetLoader<Material>::get("default");
        }

        void tickProjectile(
            World *world, voxel::World *terrain,
            Entity *entity, ProjectileComponent *proj,
            PlayerComponent *player, glm::vec3 playerPos,
            float dt,
            std::vector<Entity *> &toRemove)
        {
            proj->elapsed += dt;
            if (proj->elapsed >= proj->lifetime)
            {
                toRemove.push_back(entity);
                return;
            }

            // Move projectile
            entity->localTransform.position += proj->velocity * dt;
            // Gravity arc
            proj->velocity.y -= 9.8f * dt;

            // Update rotation to face velocity direction
            if (glm::length(proj->velocity) > 0.1f)
            {
                glm::vec3 vdir = glm::normalize(proj->velocity);
                entity->localTransform.rotation.y = std::atan2(vdir.x, vdir.z);
                entity->localTransform.rotation.x = std::asin(-vdir.y);
            }

            glm::vec3 apos = entity->localTransform.position;

            // Check terrain collision
            if (terrain)
            {
                int bx = (int)std::floor(apos.x);
                int by = (int)std::floor(apos.y);
                int bz = (int)std::floor(apos.z);
                int bt = terrain->getBlock(bx, by, bz);
                if (bt != 0 && bt != voxel::WATER)
                {
                    toRemove.push_back(entity);
                    return;
                }
            }

            // Check hit player (AABB-based)
            float hitDist = glm::distance(apos, playerPos);
            if (hitDist < 1.0f)
            {
                dealDamageToPlayer(player, proj->damage);
                toRemove.push_back(entity);
            }
        }
    };

} // namespace our
