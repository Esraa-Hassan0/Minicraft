#pragma once
#include "../ecs/component.hpp"
#include <string>
#include <glm/glm.hpp>

namespace our {
    // current game session
    enum class GameState {
        PLAYING,
        WIN,
        LOSE
    };

    class PlayerComponent : public Component {
    public:
        // Health system
        float maxHealth = 100.0f;
        float health = 100.0f;
        bool isAlive = true;
        float damageRecoveryTime = 0.5f; // Cooldown after taking damage
        float timeSinceDamage = 0.0f;

        // Resources/Inventory system
        int resourcesCollected = 0;
        int resourcesRequired = 5; // Win condition: collect this many resources

        int inventoryGrass = 0;
        int inventoryDirt = 0;
        int inventoryWood = 0;
        int inventoryStone = 0;
        int inventorySand = 0;
        /// Selected slot: 0 Grass, 1 Dirt, 2 Wood, 3 Stone, 4 Sand
        int inventoryHotbarSlot = 0;
        
        // Meat economy
        int meatCount = 10;
        int meatMax = 10;
        float meatDecayTimer = 0.0f;
        float meatDecayIntervalSec = 20.0f;

        // Movement & Physics
        glm::vec3 velocity = glm::vec3(0.0f);
        float speed = 5.0f; // Movement speed in units/second
        float jumpForce = 8.0f; // Initial vertical velocity when jumping
        float gravityAcceleration = 20.0f; // Gravity acceleration
        bool isGrounded = false;
        
        // Interaction system
        float interactionRange = 5.0f; // How far the player can mine/place blocks
        int blockPlacementCooldown = 150; // Milliseconds between block placements
        int timeSinceLastPlacement = 0;

        // Underwater state
        bool isUnderwater = false;
        float waterDamageInterval = 2.0f;
        float waterDamageTimer = 0.0f;
        float waterDamageAmount = 10.0f;
        float waterSpeedMultiplier = 0.6f;
        float waterGravityMultiplier = 0.3f;
        float swimUpSpeed = 4.0f;
        float swimDownSpeed = 3.0f;
        float waterJumpMultiplier = 1.5f;

        float waterAmbientTimer = 0.0f;

        // Game outcome
        GameState gameState = GameState::PLAYING;

        // XP Leveling System
        int level = 1;
        float currentXP = 0.0f;
        int daysSurvived = 0;

        static std::string getID() { return "PlayerComponent"; }
        
        void deserialize(const nlohmann::json& data) override {
            if (!data.is_object()) return;
            
            maxHealth = data.value("maxHealth", maxHealth);
            health = data.value("health", maxHealth);
            
            resourcesRequired = data.value("resourcesRequired", resourcesRequired);
            
            meatMax = data.value("meatMax", meatMax);
            meatCount = data.value("meatCount", meatCount);
            meatDecayIntervalSec = data.value("meatDecayIntervalSec", meatDecayIntervalSec);
            
            speed = data.value("speed", speed);
            jumpForce = data.value("jumpForce", jumpForce);
            gravityAcceleration = data.value("gravityAcceleration", gravityAcceleration);
            
            interactionRange = data.value("interactionRange", interactionRange);
            blockPlacementCooldown = data.value("blockPlacementCooldown", blockPlacementCooldown);

            waterDamageInterval = data.value("waterDamageInterval", waterDamageInterval);
            waterDamageAmount = data.value("waterDamageAmount", waterDamageAmount);
            waterSpeedMultiplier = data.value("waterSpeedMultiplier", waterSpeedMultiplier);
            waterGravityMultiplier = data.value("waterGravityMultiplier", waterGravityMultiplier);
            swimUpSpeed = data.value("swimUpSpeed", swimUpSpeed);
            swimDownSpeed = data.value("swimDownSpeed", swimDownSpeed);
            waterJumpMultiplier = data.value("waterJumpMultiplier", waterJumpMultiplier);
        }
    };
}