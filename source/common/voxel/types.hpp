#pragma once

namespace voxel {
enum block_types{
    AIR = 0,
    GRASS = 1,
    DIRT = 2,
    STONE = 3,
    SAND = 4,
    WATER = 5,
    LOG = 6,
    WOOD = 7,
    LEAF = 8,
    Diamond = 9,
    Glass = 10
};

// Tool item IDs (start at 100 to avoid collision with block types)
enum ToolItemType {
    TOOL_NONE = 0,
    TOOL_WOODEN_AXE = 100,
    TOOL_STONE_AXE = 101,
    TOOL_WOODEN_PICKAXE = 102,
    TOOL_STONE_PICKAXE = 103
};



// Helper: is this item ID a placeable block? (1-10)
inline bool isBlockItem(int itemId) {
    return itemId >= 1 && itemId <= 10;
}

// Helper: is this item ID a tool? (100+)
inline bool isToolItem(int itemId) {
    return itemId >= 100;
}

// Get display name for an item ID
inline const char* getItemName(int itemId) {
    switch (itemId) {
        case GRASS: return "Grass";
        case DIRT: return "Dirt";
        case STONE: return "Stone";
        case SAND: return "Sand";
        case WATER: return "Water";
        case LOG: return "Log";
        case WOOD: return "Wood";
        case LEAF: return "Leaf";
        case Diamond: return "Diamond";
        case Glass: return "Glass";
        case TOOL_WOODEN_AXE: return "Wood Axe";
        case TOOL_STONE_AXE: return "Stone Axe";
        case TOOL_WOODEN_PICKAXE: return "Wood Pick";
        case TOOL_STONE_PICKAXE: return "Stone Pick";
        default: return "";
    }
}
}