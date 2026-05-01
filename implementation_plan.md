# Minicraft Feature Enhancements

Six feature additions for the Minicraft voxel game: water animation, block highlight, bee AI, inventory overhaul, enemy spawn balancing, and basic crafting.

## Proposed Changes

### 1. Water Stream Animation

Currently water uses a flat `tinted` material (solid blue RGBA). To create a stream/ripple effect like Minecraft, we'll animate the water surface **on the CPU by offsetting the water mesh top-face vertices** each frame using a sine wave, so the water level gently undulates. This avoids needing a custom shader.

#### [MODIFY] [mesh-utils.cpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/common/mesh/mesh-utils.cpp)
- Add a `static float waterAnimTime` global that increments each build.
- When building water faces (blockType == WATER, face == 2 / TOP), apply a sinusoidal Y-offset to each vertex position: `vertex.position.y += sin(worldX * 0.8 + worldZ * 0.6 + waterAnimTime) * 0.06`. This makes water surfaces gently ripple.
- Lower the top face slightly (e.g., Y -= 0.15) so water sits below adjacent blocks, Minecraft-style.

#### [MODIFY] [play-state.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/states/play-state.hpp)
- In `onDraw`, periodically mark water chunks dirty (every ~0.15s) so the water mesh rebuilds with the evolving animation phase. We can increment a water animation timer and dirty chunks periodically.

---

### 2. Block Highlight on Raycast Target

The highlight system already exists (lines 870-881 in play-state), with `highlightEntity` (semi-transparent fill) and `highlightEdgesEntity` (wireframe edges). But the tint is very faint (alpha 0.08). We'll make it more visible.

#### [MODIFY] [app.jsonc](file:///d:/CMP/3.2/graphics/project/Minicraft/config/app.jsonc)
- Change `highlight-fill` tint alpha from `0.08` to `0.15` and use a slight color `[0.9, 0.9, 1.0, 0.15]`
- Change `wireframe` tint from `[0,0,0,1]` to `[0.1, 0.1, 0.1, 1.0]` — keep thin black lines but slightly softened.

No code change needed here — the highlight positioning logic already works. We're just making it more visible.

---

### 3. Bees Only Attack When Hit

Currently bees are excluded from `findHitNPC` (line 496: `if (killable->npcType == "bee") continue;`), meaning the player can't interact with them. Bees also follow the player via `follow_player` movement, but they don't have attack logic. The user wants:
- Bees are neutral (don't attack unless provoked)
- If the player hits ANY bee, ALL bees become hostile

#### [MODIFY] [play-state.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/states/play-state.hpp)
- Add a `bool beesAngry = false;` member variable.
- Remove `"bee"` from the skip list in `findHitNPC` so the player CAN target bees.
- When a bee is killed (`killNPCAndAwardMeat` with npcType == "bee"), set `beesAngry = true`.
- In `onDraw`, when `beesAngry` is true, for all bee NPCs, check distance to player. If within 3 blocks, deal damage (similar to enemy attack but lighter, e.g., 5 damage per 2 seconds).

#### [MODIFY] [killable-npc.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/common/components/killable-npc.hpp)
- Add `float attackCooldown = 0.0f;` to track per-bee attack timing.

---

### 4. Generic Minecraft-style Inventory with Bottom 8 Hotbar

Currently the hotbar is 5 hardcoded slots (grass, dirt, wood, stone, sand). The inventory UI exists but slots are empty buttons with no item data.

#### [MODIFY] [player.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/common/components/player.hpp)
- Replace the 5 individual `inventoryGrass/Dirt/Wood/Stone/Sand` integers with a proper slot-based inventory:
  ```cpp
  struct InventorySlot { int itemId = 0; int count = 0; };
  static constexpr int INVENTORY_ROWS = 3;
  static constexpr int INVENTORY_COLS = 9;
  static constexpr int HOTBAR_SLOTS = 9;
  InventorySlot mainInventory[INVENTORY_ROWS * INVENTORY_COLS]; // 27 slots
  InventorySlot hotbar[HOTBAR_SLOTS]; // 9 slots (bottom bar)
  ```
- Pre-fill hotbar with: Grass(slot 0), Dirt(1), Wood(2), Stone(3), Sand(4), others empty.
- Keep `inventoryHotbarSlot` as the selected hotbar index (0-8).

#### [MODIFY] [play-state.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/states/play-state.hpp)
- Update `kHotbarSlots` from 5 to 9.
- Update `hotbarBlockType()` to read from `player->hotbar[slot].itemId`.
- Update `inventoryCountForType()` to find the slot in hotbar/inventory matching the given blockType.
- Update `registerCollectedBlock()` to add items to the first available slot (hotbar first, then main inventory).
- Update `drawHotbarResourceIcon()` to use generic item texture lookup based on `hotbar[slot].itemId`.
- Update the inventory UI to show items in slots with icons and counts. Support drag-drop (or click-to-swap) between inventory and hotbar.
- Update `onKeyEvent` to support keys 1-9 instead of 1-5.
- Update `onScrollEvent` to wrap around 9 slots instead of 5.

---

### 5. Enemy Spawn Rate Balancing + Better Movement

Currently the spawn logic in `trySpawnWave` (enemy-system.hpp:416-468) heavily favors creepers after wave 2 (30% each for zombie/skeleton, 40% for creeper). The user sees too many creepers.

#### [MODIFY] [enemy-system.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/common/systems/enemy-system.hpp)
- **Spawn balance**: Change distribution to favor zombies/skeletons:
  - Wave < 2: 50% zombie, 50% skeleton (no creepers early)
  - Wave >= 2: 35% zombie, 35% skeleton, 30% creeper
- **Increase spawn rate**: Reduce `spawnInterval` from 12.0 to 8.0, reduce the 45% chance to skip spawning to 25%.
- **Increase max enemies** from 7 to 10.
- **Spawn group size**: Allow up to 3 per wave (currently 2 max).
- **Better jump**: Increase jump velocity from 5.9f to 7.5f for more reliable obstacle clearance.
- **Smoother movement**: Increase zombie speed from 2.8 to 3.2, skeleton speed from 2.2 to 2.8. Reduce stuck timer threshold from 1.15s to 0.8s so enemies re-path faster.
- **Better step-up**: Adjust `tryStepUpObstacle` to handle 2-block steps for enemies chasing over uneven terrain.

---

### 6. Basic Crafting System

Add a simple crafting system within the inventory UI. The 2×2 crafting grid (already visually present) will now be functional.

#### [MODIFY] [player.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/common/components/player.hpp)
- Add crafting grid slots:
  ```cpp
  InventorySlot craftingGrid[4]; // 2x2 crafting grid
  InventorySlot craftingResult;  // Output slot
  ```

#### [MODIFY] [play-state.hpp](file:///d:/CMP/3.2/graphics/project/Minicraft/source/states/play-state.hpp)
- Add crafting recipes as a static lookup table:
no 
1 Log -> 4 Wood
1 Sand -> 1 Glass
2 wood vertical -> wooden axe
2 wood horizontal -> wooden pickaxe
2 stone vertical -> stone axe
2 stone horizontal -> stone pickaxe
- Add `checkCraftingRecipe()` function that checks the 2×2 grid against recipes.
- When a recipe matches, show the result in the output slot. Clicking the output slot consumes ingredients and gives the product.
- Make inventory slots interactive: clicking a slot picks up the item, clicking another slot places it (swap logic). Items in crafting grid trigger recipe checking.

---

## Open Questions

> [!IMPORTANT]
> **Inventory item IDs**: The current block types in `types.hpp` use enum values (AIR=0, GRASS=1, DIRT=2, STONE=3, SAND=4, WATER=5, LOG=6, WOOD=7, LEAF=8, Diamond=9, Glass=10). Should inventory items only include placeable blocks, or should we add tool items (WoodenAxe, etc.) from the `ToolType` enum as well?

> [!NOTE]
> **Crafting recipes**: The recipes I proposed are simplified for MVP (4 Wood → Glass, 2 Stone + 2 Wood → Diamond, etc.). Do you want different recipes, or are these fine as placeholder recipes?

> [!NOTE]
> **Bee damage**: I plan for angry bees to deal 5 damage every 2 seconds when within 3 blocks. Want me to adjust those numbers?

---

## Verification Plan

### Manual Verification
1. **Water animation**: Observe water surfaces rippling/undulating when near a lake or ocean biome
2. **Block highlight**: Look at blocks and confirm a visible highlight outline appears on the targeted block
3. **Bee behavior**: Approach bees — they should be neutral. Hit a bee — all bees should become hostile and attack
4. **Inventory**: Press E to open inventory. Verify 9-slot hotbar at bottom, 3×9 main inventory, items with counts. Move items between slots
5. **Enemy spawning**: Play for several minutes — should see more zombies and skeletons than creepers. Enemies should navigate terrain better (jumping over 1-block obstacles, not getting stuck as often)
6. **Crafting**: Place items in the 2×2 grid. When a valid recipe is matched, output appears. Click output to craft
