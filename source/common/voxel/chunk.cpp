#include "chunk.hpp"
#include "types.hpp"
#include <random>
#include <cmath>
#include <algorithm>

namespace voxel
{

    // 1. UPDATE CONSTRUCTOR: Initialize the 'light' vector to 15 (max sunlight)
    Chunk::Chunk(int cx, int cz, int h, ChunkType type)
        : chunkX(cx), chunkZ(cz), height(h), chunkType(type),
          blocks(static_cast<std::size_t>(CHUNK_SIZE) * static_cast<std::size_t>(height) * static_cast<std::size_t>(CHUNK_SIZE), AIR),
          light(static_cast<std::size_t>(CHUNK_SIZE) * static_cast<std::size_t>(height) * static_cast<std::size_t>(CHUNK_SIZE), 15) {}

    void Chunk::generate(int waterLevel, int stoneLevel)
    {
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
                int worldX = chunkX * CHUNK_SIZE + x;
                int worldZ = chunkZ * CHUNK_SIZE + z;

                float hills = std::sin(worldX * 0.2f) + std::cos(worldZ * 0.2f);
                int grassSurface = waterLevel + 1 + static_cast<int>(hills * 2.0f);
                int desertSurface = waterLevel + 2 + static_cast<int>(hills * 1.5f);

                int seaFloor = waterLevel - 3 + static_cast<int>(hills * 1.0f);
                int maxSeaFloor = waterLevel - 2;
                if (maxSeaFloor < stoneLevel)
                    maxSeaFloor = stoneLevel;
                seaFloor = std::max(stoneLevel, std::min(maxSeaFloor, seaFloor));

                 for (int y = 0; y < height; ++y)
                 {
                     // --- 3D Cave Carver ---
                     // We don't want caves breaking the surface or the very bottom floor
                     bool isCave = false;
                     if (y > 2 && y < grassSurface - 2) 
                     {
                         // Multiply 3D sine waves to create hollow pockets
                         float caveNoise = std::sin(worldX * 0.12f) * std::sin(y * 0.18f) * std::cos(worldZ * 0.12f);
                         
                         // If the noise value is high enough, we carve a hole
                         if (caveNoise > 0.35f) 
                         {
                             isCave = true;
                         }
                     }

                     // If it's a cave, skip setting the block (leaving it as AIR)
                     if (isCave) 
                     {
                         continue; 
                     }

                     // --- Standard Terrain Placement ---
                     if (y < stoneLevel)
                     {
                         setBlock(x, y, z, STONE);
                         continue;
                     }

                     if (chunkType == ChunkType::Sea)
                     {
                         if (y < seaFloor)
                         {
                             setBlock(x, y, z, STONE);
                         }
                         else if (y == seaFloor)
                         {
                             setBlock(x, y, z, SAND);
                         }
                         else if (y <= waterLevel)
                         {
                             setBlock(x, y, z, WATER);
                         }
                         continue;
                     }

                     if (chunkType == ChunkType::Desert)
                     {
                         if (y <= desertSurface)
                         {
                             setBlock(x, y, z, SAND);
                         }
                         continue;
                     }

                     if (y < grassSurface)
                     {
                         if (y >= grassSurface - 2)
                         {
                             setBlock(x, y, z, DIRT);
                         }
                         else
                         {
                             setBlock(x, y, z, STONE);
                         }
                     }
                     else if (y == grassSurface)
                     {
                         if (grassSurface <= waterLevel)
                         {
                             setBlock(x, y, z, SAND);
                         }
                         else
                         {
                             setBlock(x, y, z, GRASS);
                         }
                     }
                     else if (y <= waterLevel)
                     {
                         setBlock(x, y, z, WATER);
                     }
                 }
            }
        }
        // ==========================================
        // --- 2. Tree Generation Pass ---
        // ==========================================
        if (chunkType == ChunkType::Grass)
        {
            std::mt19937 treeRng(chunkX * 73856 + chunkZ * 19349); 
            std::uniform_int_distribution<int> chanceDist(0, 100);

            for (int z = 2; z < CHUNK_SIZE - 2; z += 3)
            {
                for (int x = 2; x < CHUNK_SIZE - 2; x += 3)
                {
                    if (chanceDist(treeRng) < 5) 
                    {
                        int surfaceY = -1;
                        for (int y = height - 1; y >= 0; --y)
                        {
                            if (getBlock(x, y, z) == GRASS) {
                                surfaceY = y;
                                break;
                            }
                        }

                        if (surfaceY != -1 && surfaceY < height - 10) 
                        {
                            int treeHeight = 4 + (chanceDist(treeRng) % 3); 

                            for (int y = 1; y <= treeHeight; ++y) {
                                setBlock(x, surfaceY + y, z, LOG);
                            }

                            int leafCenterY = surfaceY + treeHeight;
                            for (int ly = leafCenterY - 2; ly <= leafCenterY + 1; ++ly)
                            {
                                int radius = (ly == leafCenterY + 1) ? 1 : 2;
                                
                                for (int lx = x - radius; lx <= x + radius; ++lx)
                                {
                                    for (int lz = z - radius; lz <= z + radius; ++lz)
                                    {
                                        if (std::abs(lx - x) == radius && std::abs(lz - z) == radius && chanceDist(treeRng) < 50) {
                                            continue; 
                                        }

                                        if (getBlock(lx, ly, lz) == AIR) {
                                            setBlock(lx, ly, lz, LEAF);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 2. ADD VERTICAL LIGHT PROPAGATION
    void Chunk::calculateLighting()
    {
        for (int z = 0; z < CHUNK_SIZE; ++z)
        {
            for (int x = 0; x < CHUNK_SIZE; ++x)
            {
                int currentLight = 15; // Start with full sky light

                for (int y = height - 1; y >= 0; --y)
                {
                    int blockType = getBlock(x, y, z);

                    // If it's a solid block, it blocks all light
                    if (blockType != AIR && blockType != WATER && blockType != Glass)
                    {
                        currentLight = 0;
                    }
                    // Water absorbs light gradually
                    else if (blockType == WATER)
                    {
                        currentLight = std::max(0, currentLight - 3);
                    }

                    setLight(x, y, z, currentLight);
                }
            }
        }
    }

    bool Chunk::isInside(int x, int y, int z) const
    {
        return x >= 0 && x < CHUNK_SIZE && y >= 0 && y < height && z >= 0 && z < CHUNK_SIZE;
    }

    std::size_t Chunk::flatten(int x, int y, int z) const
    {
        return static_cast<std::size_t>(x) +
               static_cast<std::size_t>(CHUNK_SIZE) *
                   (static_cast<std::size_t>(y) + static_cast<std::size_t>(height) * static_cast<std::size_t>(z));
    }

    int Chunk::getBlock(int x, int y, int z) const
    {
        if (!isInside(x, y, z))
            return 0;
        return blocks[flatten(x, y, z)];
    }

    void Chunk::setBlock(int x, int y, int z, int type)
    {
        if (!isInside(x, y, z))
            return;
        blocks[flatten(x, y, z)] = type;
        isDirty = true;
    }

    // 3. GETTERS/SETTERS FOR LIGHT
    int Chunk::getLight(int x, int y, int z) const
    {
        if (!isInside(x, y, z))
            return 15; // Assume outside is bright
        return light[flatten(x, y, z)];
    }

    void Chunk::setLight(int x, int y, int z, int level)
    {
        if (!isInside(x, y, z))
            return;
        light[flatten(x, y, z)] = level;
        isDirty = true;
    }

} // namespace voxel