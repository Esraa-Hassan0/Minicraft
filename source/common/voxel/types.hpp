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

enum ToolType {
        Hand = 0,        
        WoodenAxe,       
        StoneAxe,         
        WoodenPickaxe,   
        StonePickaxe      
};
}