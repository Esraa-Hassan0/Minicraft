#pragma once
#include "../ecs/component.hpp"
#include "../voxel/types.hpp"
#include <string>
#include <glm/glm.hpp>

namespace our {
    // current game session
    enum class GameState {
        PLAYING,
        WIN,
        LOSE
    };

    // Generic inventory slot: holds any item (block or tool) with a count
    struct InventorySlot {
        int itemId = 0;  // 0 = empty, 1-10 = block types, 100+ = tools
        int count = 0;

        bool isEmpty() const { return itemId == 0 || count <= 0; }
        void clear() { itemId = 0; count = 0; }
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
        int resourcesRequired = 100; // Win condition: collect this many resources

        // Generic inventory system (Minecraft-style)
        static constexpr int HOTBAR_SLOTS = 9;
        static constexpr int INV_ROWS = 3;
        static constexpr int INV_COLS = 9;
        static constexpr int MAIN_INV_SIZE = INV_ROWS * INV_COLS; // 27

        InventorySlot hotbar[HOTBAR_SLOTS];         // Bottom 9 slots (always visible)
        InventorySlot mainInventory[MAIN_INV_SIZE];  // 3x9 = 27 main slots

        // Crafting grid (2x2) + output
        InventorySlot craftingGrid[4];   // [0]=top-left, [1]=top-right, [2]=bottom-left, [3]=bottom-right
        InventorySlot craftingResult;    // Output slot

        // Cursor item (held by mouse when moving items)
        InventorySlot cursorItem;

        /// Selected hotbar slot: 0-8
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
        float interactionRange = 10.0f; // How far the player can mine/place blocks
        int blockPlacementCooldown = 150; // Milliseconds between block placements
        int timeSinceLastPlacement = 0;

        // Underwater state
        bool isUnderwater = false;
        float maxOxygen = 20.0f; // 20 seconds of air (4 bubbles = 5s each)
        float oxygen = 20.0f;
        float waterDamageInterval = 10.0f; // Damage every 10s when drowning
        float waterDamageTimer = 0.0f;
        float waterDamageAmount = 10.0f; // 1 full heart
        float waterSpeedMultiplier = 0.6f;
        float waterGravityMultiplier = 0.3f;
        float swimUpSpeed = 4.0f;
        float swimDownSpeed = 3.0f;
        float waterJumpMultiplier = 1.5f;

        float waterAmbientTimer = 0.0f;

        // Game outcome
        GameState gameState = GameState::PLAYING;

        // Screen Shake
        float shakeTimer = 0.0f;
        float shakeIntensity = 0.0f;

        // XP Leveling System
        int level = 1;
        float currentXP = 0.0f;
        int daysSurvived = 0;

        // Initialize default hotbar with basic blocks
        void initDefaultInventory() {
            for (int i = 0; i < HOTBAR_SLOTS; i++) hotbar[i].clear();
            for (int i = 0; i < MAIN_INV_SIZE; i++) mainInventory[i].clear();
            for (int i = 0; i < 4; i++) craftingGrid[i].clear();
            craftingResult.clear();
            cursorItem.clear();

            // Pre-fill hotbar with basic blocks
            hotbar[0] = {voxel::GRASS, 0};
            hotbar[1] = {voxel::DIRT, 0};
            hotbar[2] = {voxel::WOOD, 0};
            hotbar[3] = {voxel::STONE, 0};
            hotbar[4] = {voxel::SAND, 0};
            hotbar[5] = {voxel::LOG, 0};
            hotbar[6] = {voxel::Glass, 0};
        }

        // Add item to inventory: tries hotbar first (existing stacks), then main inventory
        bool addItem(int itemId, int amount = 1) {
            // First try to stack in hotbar
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (hotbar[i].itemId == itemId) {
                    hotbar[i].count += amount;
                    return true;
                }
            }
            // Then try to stack in main inventory
            for (int i = 0; i < MAIN_INV_SIZE; i++) {
                if (mainInventory[i].itemId == itemId) {
                    mainInventory[i].count += amount;
                    return true;
                }
            }
            // Then try empty hotbar slot
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (hotbar[i].isEmpty()) {
                    hotbar[i] = {itemId, amount};
                    return true;
                }
            }
            // Then try empty main inventory slot
            for (int i = 0; i < MAIN_INV_SIZE; i++) {
                if (mainInventory[i].isEmpty()) {
                    mainInventory[i] = {itemId, amount};
                    return true;
                }
            }
            return false; // Inventory full
        }

        // Get total count of an item across all inventory
        int getItemCount(int itemId) const {
            int total = 0;
            for (int i = 0; i < HOTBAR_SLOTS; i++)
                if (hotbar[i].itemId == itemId) total += hotbar[i].count;
            for (int i = 0; i < MAIN_INV_SIZE; i++)
                if (mainInventory[i].itemId == itemId) total += mainInventory[i].count;
            return total;
        }

        // Remove one item from the first slot that has it (hotbar first)
        bool removeItem(int itemId, int amount = 1) {
            for (int i = 0; i < HOTBAR_SLOTS; i++) {
                if (hotbar[i].itemId == itemId && hotbar[i].count >= amount) {
                    hotbar[i].count -= amount;
                    if (hotbar[i].count <= 0) hotbar[i].clear();
                    return true;
                }
            }
            for (int i = 0; i < MAIN_INV_SIZE; i++) {
                if (mainInventory[i].itemId == itemId && mainInventory[i].count >= amount) {
                    mainInventory[i].count -= amount;
                    if (mainInventory[i].count <= 0) mainInventory[i].clear();
                    return true;
                }
            }
            return false;
        }

        // Get the item ID in the currently selected hotbar slot
        int getSelectedItemId() const {
            if (inventoryHotbarSlot < 0 || inventoryHotbarSlot >= HOTBAR_SLOTS) return 0;
            return hotbar[inventoryHotbarSlot].itemId;
        }

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

            initDefaultInventory();
        }
    };
}