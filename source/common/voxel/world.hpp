#pragma once

#include <cstddef>
#include <vector>
#include "types.hpp"
#include <glm/glm.hpp>
#include <json/json.hpp>
namespace voxel {

struct BlockData {
    int x = 0;
    int y = 0;
    int z = 0;
    int type = 0;
};

struct RayHit {
    bool hit = false;
    int x, y, z;          // The block that was hit (to break it)
    int prevX, prevY, prevZ; // The empty space right before the hit (to place a block)
};
class World {
public:
    int width = 64;
    int height = 32;
    int depth = 64;
    int stoneLevel = 4;
    int waterLevel = 6;

    World() = default;

    void deserialize(const nlohmann::json& data);
    void generate();

    int getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, int type);
    RayHit castRay(glm::vec3 start, glm::vec3 direction, float maxDistance = 8.0f) const;
    void breakBlock(const RayHit& hit);
    void placeBlock(const RayHit& hit, int type);
    // The renderer can consume this each frame to draw only exposed blocks.
    std::vector<BlockData> getVisibleBlocks() const;

private:
    std::vector<int> blocks;

    bool isInside(int x, int y, int z) const;
    std::size_t flatten(int x, int y, int z) const;
    bool isBlockVisible(int x, int y, int z) const;
};

} // namespace voxel
