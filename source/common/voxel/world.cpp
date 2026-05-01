#include "world.hpp"
#include "types.hpp"
#include <random>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
namespace voxel {

namespace {
    constexpr int BIOME_REGION_SIZE = 3;

    int floor_div(int value, int divisor) {
        int quotient = value / divisor;
        int remainder = value % divisor;
        if (remainder != 0 && value < 0) {
            --quotient;
        }
        return quotient;
    }

    int positive_mod(int value, int mod) {
        int result = value % mod;
        return result < 0 ? result + mod : result;
    }

    ChunkType get_chunk_type_for_coordinates(int chunkX, int chunkZ) {
        int regionX = floor_div(chunkX, BIOME_REGION_SIZE);
        int regionZ = floor_div(chunkZ, BIOME_REGION_SIZE);

        // 3x3 regions guarantee at least three neighboring chunks share a biome.
        // This deterministic pattern also ensures all three biome types appear near spawn.
        int biomeIndex = positive_mod(regionX + 2 * regionZ, 3);

        switch (biomeIndex) {
            case 0: return ChunkType::Desert;
            case 1: return ChunkType::Grass;
            default: return ChunkType::Sea;
        }
    }
}

void World::deserialize(const nlohmann::json& data) {
    // Read values from JSON, using the second argument as a fallback default
    height = data.value("height", 100);
    waterLevel = data.value("waterLevel", 6);
    stoneLevel = data.value("stoneLevel", 4);
}

void World::generateChunk(int chunkX, int chunkZ) {
    std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);

    if (activeChunks.find(key) == activeChunks.end()) {
        ChunkType type = get_chunk_type_for_coordinates(chunkX, chunkZ);
        activeChunks.emplace(key, Chunk(chunkX, chunkZ, height, type));
        activeChunks.at(key).generate(waterLevel, stoneLevel);

        static const glm::ivec2 neighborOffsets[4] = {
            {1, 0}, {-1, 0}, {0, 1}, {0, -1}
        };

        for (const auto& offset : neighborOffsets) {
            std::string neighborKey =
                std::to_string(chunkX + offset.x) + "_" + std::to_string(chunkZ + offset.y);
            auto neighborIt = activeChunks.find(neighborKey);
            if (neighborIt != activeChunks.end()) {
                neighborIt->second.isDirty = true;
            }
        }
    }
}

int World::getBlock(int worldX, int y, int worldZ) const {
    if (y < 0 || y >= height) return 0;

    int chunkX = static_cast<int>(std::floor(worldX / static_cast<float>(Chunk::CHUNK_SIZE)));
    int chunkZ = static_cast<int>(std::floor(worldZ / static_cast<float>(Chunk::CHUNK_SIZE)));

    std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);

    auto it = activeChunks.find(key);
    if (it == activeChunks.end()) {
        return 0;
    }

    int localX = (worldX % Chunk::CHUNK_SIZE + Chunk::CHUNK_SIZE) % Chunk::CHUNK_SIZE;
    int localZ = (worldZ % Chunk::CHUNK_SIZE + Chunk::CHUNK_SIZE) % Chunk::CHUNK_SIZE;

    return it->second.getBlock(localX, y, localZ);
}

void World::setBlock(int worldX, int y, int worldZ, int type) {
    if (y < 0 || y >= height) return;

    int chunkX = static_cast<int>(std::floor(worldX / static_cast<float>(Chunk::CHUNK_SIZE)));
    int chunkZ = static_cast<int>(std::floor(worldZ / static_cast<float>(Chunk::CHUNK_SIZE)));

    std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);

    auto it = activeChunks.find(key);
    if (it != activeChunks.end()) {
        int localX = (worldX % Chunk::CHUNK_SIZE + Chunk::CHUNK_SIZE) % Chunk::CHUNK_SIZE;
        int localZ = (worldZ % Chunk::CHUNK_SIZE + Chunk::CHUNK_SIZE) % Chunk::CHUNK_SIZE;
        it->second.setBlock(localX, y, localZ, type);


        if (localX == 0) {
            std::string neighborKey = std::to_string(chunkX - 1) + "_" + std::to_string(chunkZ);
            auto neighborIt = activeChunks.find(neighborKey);
            if (neighborIt != activeChunks.end()) neighborIt->second.isDirty = true;
        } 
        else if (localX == Chunk::CHUNK_SIZE - 1) {
            std::string neighborKey = std::to_string(chunkX + 1) + "_" + std::to_string(chunkZ);
            auto neighborIt = activeChunks.find(neighborKey);
            if (neighborIt != activeChunks.end()) neighborIt->second.isDirty = true;
        }

        if (localZ == 0) {
            std::string neighborKey = std::to_string(chunkX) + "_" + std::to_string(chunkZ - 1);
            auto neighborIt = activeChunks.find(neighborKey);
            if (neighborIt != activeChunks.end()) neighborIt->second.isDirty = true;
        } 
        else if (localZ == Chunk::CHUNK_SIZE - 1) {
            std::string neighborKey = std::to_string(chunkX) + "_" + std::to_string(chunkZ + 1);
            auto neighborIt = activeChunks.find(neighborKey);
            if (neighborIt != activeChunks.end()) neighborIt->second.isDirty = true;
        }
        triggerBlockUpdate(worldX, y, worldZ);
    }
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
        int hitBlockType = getBlock(gridX, gridY, gridZ);
        if (hitBlockType != AIR && hitBlockType != WATER) {
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

int World::getLight(int worldX, int y, int worldZ) const {
    if (y < 0 || y >= height) return 0;

    int chunkX = static_cast<int>(std::floor(worldX / static_cast<float>(Chunk::CHUNK_SIZE)));
    int chunkZ = static_cast<int>(std::floor(worldZ / static_cast<float>(Chunk::CHUNK_SIZE)));

    std::string key = std::to_string(chunkX) + "_" + std::to_string(chunkZ);

    auto it = activeChunks.find(key);
    if (it == activeChunks.end()) {
        return 15;
    }

    int localX = (worldX % Chunk::CHUNK_SIZE + Chunk::CHUNK_SIZE) % Chunk::CHUNK_SIZE;
    int localZ = (worldZ % Chunk::CHUNK_SIZE + Chunk::CHUNK_SIZE) % Chunk::CHUNK_SIZE;

    return it->second.getLight(localX, y, localZ);
}
void World::triggerBlockUpdate(int worldX, int y, int worldZ) {
    if (getBlock(worldX, y, worldZ) == voxel::WATER) {
        fluidQueue.push_back({worldX, y, worldZ});
    }

    static const glm::ivec3 offsets[6] = {
        {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}
    };

    for (const auto& offset : offsets) {
        int nx = worldX + offset.x;
        int ny = y + offset.y;
        int nz = worldZ + offset.z;
        if (getBlock(nx, ny, nz) == voxel::WATER) {
            fluidQueue.push_back({nx, ny, nz});
        }
    }
}

void World::updateFluids(float deltaTime, bool& meshDirtyFlag) {
    fluidTickTimer += deltaTime;
    if (fluidTickTimer < 0.15f) return; 
    fluidTickTimer = 0.0f;

    if (fluidQueue.empty()) return;

    std::vector<glm::ivec3> currentQueue = fluidQueue;
    fluidQueue.clear();

    bool fluidMoved = false;

    for (const auto& pos : currentQueue) {
        int x = pos.x, y = pos.y, z = pos.z;

        if (getBlock(x, y, z) != voxel::WATER) continue;

        if (getBlock(x, y - 1, z) == voxel::AIR) {
            setBlock(x, y - 1, z, voxel::WATER);
            fluidMoved = true;
        }
        else {
            static const glm::ivec3 sides[4] = { {1,0,0}, {-1,0,0}, {0,0,1}, {0,0,-1} };
            for (const auto& dir : sides) {
                if (getBlock(x + dir.x, y, z + dir.z) == voxel::AIR) {
                    setBlock(x + dir.x, y, z + dir.z, voxel::WATER);
                    fluidMoved = true;
                }
            }
        }
    }

    if (fluidMoved) {
        meshDirtyFlag = true;
    }
}

} // namespace voxel
