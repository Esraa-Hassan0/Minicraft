#pragma once
#include "../ecs/component.hpp"
#include <glm/glm.hpp>
#include <string>

namespace our {

    enum class EnemyType {
        ZOMBIE,    // Chases player, melee damage
        SKELETON,  // Keeps distance, shoots projectiles
        CREEPER    // Chases player, explodes when close
    };

    enum class EnemyState {
        IDLE,      // Standing still, looking around
        CHASE,     // Moving toward player
        ATTACK,    // Performing attack animation
        DEAD       // Playing death, about to be removed
    };

    class EnemyComponent : public Component {
    public:
        EnemyType type        = EnemyType::ZOMBIE;
        EnemyState state      = EnemyState::IDLE;

        // Stats
        float maxHealth       = 20.0f;
        float health          = 20.0f;
        float speed           = 3.0f;
        float detectionRange  = 18.0f;  // How far enemy sees the player
        float attackRange     = 1.5f;   // Melee range
        float attackDamage    = 10.0f;
        float attackCooldown  = 1.5f;   // Seconds between attacks
        float timeSinceAttack = 0.0f;

        // Skeleton-specific: ranged attack
        float shootCooldown   = 2.5f;
        float timeSinceShot   = 0.0f;
        float preferredRange  = 10.0f;  // Skeleton tries to keep this distance

        // Creeper-specific: explosion
        float fuseTime        = 2.0f;   // Seconds before explosion
        float fuseTimer       = 0.0f;
        float explodeRange    = 2.5f;   // Radius of explosion
        float explodeDamage   = 50.0f;
        bool  isLit           = false;  // Has fuse started?

        // Death
        float deadTimer       = 0.0f;
        float deadDuration    = 0.6f;   // Seconds before entity is removed

        // Idle wander
        float idleTimer       = 0.0f;
        float idleDuration    = 2.0f;
        glm::vec3 wanderTarget = glm::vec3(0.0f);

        // Physics (gravity + terrain collision)
        glm::vec3 velocity{0.0f};
        bool      isGrounded       = false;
        float     gravityAccel     = 20.0f;
        glm::vec3 colliderHalfSize{0.3f, 0.9f, 0.3f};
        glm::vec3 colliderCenter  {0.0f, 0.9f, 0.0f};

        // Bump-and-jump: detect when stuck against obstacles
        glm::vec3 previousPosition{0.0f};
        float     jumpCooldown     = 0.0f;   // seconds until next jump allowed
        float     stuckTimer       = 0.0f;   // accumulates while blocked

        // Visual animation state
        float walkAnimTime = 0.0f;

        // Visual hit feedback (Minecraft-like red flash)
        float hurtFlashTimer = 0.0f;
        float hurtFlashDuration = 0.18f;

        static std::string getID() { return "EnemyComponent"; }
        void deserialize(const nlohmann::json& data) override {
            if (!data.is_object()) return;
            maxHealth      = data.value("maxHealth",      maxHealth);
            health         = maxHealth;
            speed          = data.value("speed",          speed);
            detectionRange = data.value("detectionRange", detectionRange);
            attackDamage   = data.value("attackDamage",   attackDamage);
            attackCooldown = data.value("attackCooldown", attackCooldown);
            explodeDamage  = data.value("explodeDamage",  explodeDamage);
            explodeRange   = data.value("explodeRange",   explodeRange);
        }
    };

}
