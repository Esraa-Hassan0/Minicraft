#include "chunk.hpp"
#include <random>
#include <cmath>
#include <algorithm>

namespace voxel {

Chunk::Chunk(int cx, int cz, int h, ChunkType type)
    : chunkX(cx), chunkZ(cz), height(h), chunkType(type),
      blocks(static_cast<std::size_t>(CHUNK_SIZE) * static_cast<std::size_t>(height) * static_cast<std::size_t>(CHUNK_SIZE), AIR) {}

void Chunk::generate(int waterLevel, int stoneLevel) {
    for(int z = 0; z < CHUNK_SIZE; ++z) {
        for(int x = 0; x < CHUNK_SIZE; ++x) {
            int worldX = chunkX * CHUNK_SIZE + x;
            int worldZ = chunkZ * CHUNK_SIZE + z;

            float hills = std::sin(worldX * 0.2f) + std::cos(worldZ * 0.2f);
            int grassSurface = waterLevel + 1 + static_cast<int>(hills * 2.0f);
            int desertSurface = waterLevel + 2 + static_cast<int>(hills * 1.5f);
            // Keep sea biomes deep enough for actual diving gameplay.
            // With low water levels, clamping to waterLevel-1 makes water only 1 block deep.
            int seaFloor = waterLevel - 3 + static_cast<int>(hills * 1.0f);
            int maxSeaFloor = waterLevel - 2; // guarantees at least 2 blocks of water column
            if (maxSeaFloor < stoneLevel) maxSeaFloor = stoneLevel;
            seaFloor = std::max(stoneLevel, std::min(maxSeaFloor, seaFloor));
            
            for (int y = 0 ; y < height; ++y) {
                if (y < stoneLevel) {
                    setBlock(x, y, z, STONE);
                    continue;
                }

                if (chunkType == ChunkType::Sea) {
                    if (y < seaFloor) {
                        setBlock(x, y, z, STONE);
                    } else if (y == seaFloor) {
                        setBlock(x, y, z, SAND);
                    } else if (y <= waterLevel) {
                        setBlock(x, y, z, WATER);
                    }
                    continue;
                }

                if (chunkType == ChunkType::Desert) {
                    if (y <= desertSurface) {
                        setBlock(x, y, z, SAND);
                    }
                    continue;
                }

                // Grass biome with occasional lakes when terrain falls below water level.
                if (y < grassSurface) {
                    if (y >= grassSurface - 2) {
                        setBlock(x, y, z, DIRT);
                    } else {
                        setBlock(x, y, z, STONE);
                    }
                } else if (y == grassSurface) {
                    if (grassSurface <= waterLevel) {
                        setBlock(x, y, z, SAND);
                    } else {
                        setBlock(x, y, z, GRASS);
                    }
                } else if (y <= waterLevel) {
                    setBlock(x, y, z, WATER);
                }
            }
        }
    }
}



bool Chunk::isInside(int x, int y, int z) const {
    return x >= 0 && x < CHUNK_SIZE && y >= 0 && y < height && z >= 0 && z < CHUNK_SIZE;
}

std::size_t Chunk::flatten(int x, int y, int z) const {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(CHUNK_SIZE) *
               (static_cast<std::size_t>(y) + static_cast<std::size_t>(height) * static_cast<std::size_t>(z));
}

int Chunk::getBlock(int x, int y, int z) const {
    if (!isInside(x, y, z)) {
        return 0;
    }

    return blocks[flatten(x, y, z)];
}

void Chunk::setBlock(int x, int y, int z, int type) {
    if (!isInside(x, y, z)) {
        return;
    }

    blocks[flatten(x, y, z)] = type;
    isDirty = true;
}

} // namespace voxel
