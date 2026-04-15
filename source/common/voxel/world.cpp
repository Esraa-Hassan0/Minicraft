#include "world.hpp"
#include <random>
#include <glm/glm.hpp>
namespace voxel {

World::World() : blocks(static_cast<std::size_t>(WIDTH) * HEIGHT * DEPTH, 0) {
    const int WATER_LEVEL = 6;
    const int Stone_LEVEL = 4;
    for(int z = 0; z < DEPTH; ++z) {
        for(int x = 0; x < WIDTH; ++x) {
            float hills = std::sin(x * 0.2f) + std::cos(z * 0.2f); 
            int surfaceHeight = 8 + static_cast<int>(hills * 2.0f);
            for (int y=0 ; y< HEIGHT; y++){
                if(y<=Stone_LEVEL){
                    setBlock(x, y, z, STONE);
                } 
                else if (y <= surfaceHeight) {
                    if(y== surfaceHeight){
                        if (y <= WATER_LEVEL + 1) {
                            setBlock(x, y, z, SAND);
                        } 
                        else {
                            if (std::rand() % 100 < 5) {
                                setBlock(x, y, z, SAND);
                            } else {
                                setBlock(x, y, z, GRASS);
                            }
                        }
                    }
                    else{
                        setBlock(x, y, z, STONE);
                    }
                }
                else if (y <= WATER_LEVEL) {
                    setBlock(x, y, z, WATER);
                }
            }
        }
    }
}

bool World::isInside(int x, int y, int z) const {
    return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH;
}

std::size_t World::flatten(int x, int y, int z) const {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(WIDTH) *
               (static_cast<std::size_t>(y) + static_cast<std::size_t>(HEIGHT) * static_cast<std::size_t>(z));
}

int World::getBlock(int x, int y, int z) const {
    if (!isInside(x, y, z)) {
        return 0;
    }

    return blocks[flatten(x, y, z)];
}

void World::setBlock(int x, int y, int z, int type) {
    if (!isInside(x, y, z)) {
        return;
    }

    blocks[flatten(x, y, z)] = type;
}

bool World::isBlockVisible(int x, int y, int z) const {
    if (getBlock(x, y, z) == 0) {
        return false;
    }

    static constexpr int NEIGHBOR_OFFSETS[6][3] = {
        {1, 0, 0},
        {-1, 0, 0},
        {0, 1, 0},
        {0, -1, 0},
        {0, 0, 1},
        {0, 0, -1},
    };

    for (const auto &offset : NEIGHBOR_OFFSETS) {
        const int neighborX = x + offset[0];
        const int neighborY = y + offset[1];
        const int neighborZ = z + offset[2];

        if (!isInside(neighborX, neighborY, neighborZ) || getBlock(neighborX, neighborY, neighborZ) == 0) {
            return true;
        }
    }

    return false;
}

std::vector<BlockData> World::getVisibleBlocks() const {
    std::vector<BlockData> visibleBlocks;
    visibleBlocks.reserve(blocks.size() / 4);

    for (int z = 0; z < DEPTH; ++z) {
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const int type = getBlock(x, y, z);
                if (type == 0 || !isBlockVisible(x, y, z)) {
                    continue;
                }

                visibleBlocks.push_back(BlockData{x, y, z, type});
            }
        }
    }

    return visibleBlocks;
}

// Raycasting and block manipulation methods would go here (castRay, breakBlock, placeBlock)
RayHit World::castRay(glm::vec3 start, glm::vec3 direction, float maxDistance) const {
    RayHit result;
    
    // Normalize direction just in case
    glm::vec3 dir = glm::normalize(direction);
    
    // We step forward by a small fraction (e.g., 0.1 units) to ensure we don't skip blocks
    float stepSize = 0.1f; 
    glm::vec3 currentPos = start;
    
    int lastX = -1, lastY = -1, lastZ = -1;

    for (float t = 0; t < maxDistance; t += stepSize) {
        int gridX = static_cast<int>(std::floor(currentPos.x));
        int gridY = static_cast<int>(std::floor(currentPos.y));
        int gridZ = static_cast<int>(std::floor(currentPos.z));

        // If we hit a solid block
        if (getBlock(gridX, gridY, gridZ) != AIR) {
            result.hit = true;
            result.x = gridX;
            result.y = gridY;
            result.z = gridZ;
            
            // The previous empty space is where we would place a new block
            result.prevX = lastX;
            result.prevY = lastY;
            result.prevZ = lastZ;
            return result;
        }

        // Keep track of the last empty block we were in
        lastX = gridX;
        lastY = gridY;
        lastZ = gridZ;

        // Move the ray forward
        currentPos += dir * stepSize;
    }

    return result; // hit is false if we reached maxDistance hitting nothing
}

void World::breakBlock(const RayHit& hit) {
    if (hit.hit && getBlock(hit.x, hit.y, hit.z) != AIR) {
        // add some block breaking effects here (particles, sound, etc.), delay based on block type, etc.
        setBlock(hit.x, hit.y, hit.z, AIR);
    }
}

void World::placeBlock(const RayHit& hit, int type) {
    if (hit.hit && getBlock(hit.prevX, hit.prevY, hit.prevZ) == AIR) {
        setBlock(hit.prevX, hit.prevY, hit.prevZ, type);
    }
}


} // namespace voxel
