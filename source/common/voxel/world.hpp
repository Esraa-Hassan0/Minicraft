#pragma once
#include "chunk.hpp"
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>
#include <json/json.hpp>


namespace voxel {
    struct RayHit {
        bool hit = false;
        int x, y, z;          // The block that was hit (to break it)
        int prevX, prevY, prevZ; // The empty space right before the hit (to place a block)
    };

class World {
public:
    int height = 100;
    int waterLevel = 6;
    int stoneLevel = 4;
    // fluid system parameters
    std::unordered_map<std::string, Chunk> activeChunks;
    std::vector<glm::ivec3> fluidQueue; 
    float fluidTickTimer = 0.0f;

    World() = default;

    void deserialize(const nlohmann::json& data);
    int getBlock(int worldX, int y, int worldZ) const;
    void setBlock(int worldX, int y, int worldZ, int type);

    void generateChunk(int chunkX, int chunkZ);

    RayHit castRay(glm::vec3 start, glm::vec3 direction, float maxDistance = 8.0f) const;
    void breakBlock(const RayHit& hit);
    void placeBlock(const RayHit& hit, int type);
     
    int getLight(int worldX, int y, int worldZ) const;

    void triggerBlockUpdate(int worldX, int worldY, int worldZ);
    void updateFluids(float deltaTime, bool& meshDirtyFlag);
};

} // namespace voxel