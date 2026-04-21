#pragma once

#include <vector>
#include <cstdint>

namespace voxel
{
     enum class ChunkType
     {
         Default,
         Sea,
         Desert,
         Grass
     };

    class Chunk
    {
    public:
        static const int CHUNK_SIZE = 16;
        int chunkX, chunkZ, height;
        ChunkType chunkType;

        std::vector<uint8_t> blocks;
        std::vector<uint8_t> light;

        bool isDirty = true;

        Chunk(int cx, int cz, int h, ChunkType type);

        void generate(int waterLevel, int stoneLevel);
        void calculateLighting();

        bool isInside(int x, int y, int z) const;
        std::size_t flatten(int x, int y, int z) const;

        int getBlock(int x, int y, int z) const;
        void setBlock(int x, int y, int z, int type);

        int getLight(int x, int y, int z) const;
        void setLight(int x, int y, int z, int level);
    };
}