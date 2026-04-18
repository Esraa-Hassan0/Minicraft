#pragma once
#include <cstddef>
#include <vector>
#include "types.hpp"
namespace voxel {

enum class ChunkType {
    Desert = 0,
    Grass = 1,
    Sea = 2
};


class Chunk {
public:
    static constexpr int CHUNK_SIZE = 16;    
    int chunkX;
    int chunkZ;
    int height;
    ChunkType chunkType;
    bool isDirty = true; // Flag to indicate if the chunk's mesh needs to be rebuilt
    Chunk(int cx, int cz, int h, ChunkType type = ChunkType::Grass);

    void generate(int waterLevel, int stoneLevel);
    int getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int type);
private:
    std::vector<int> blocks;

    bool isInside(int x, int y, int z) const;
    std::size_t flatten(int x, int y, int z) const;
};

} // namespace voxel
