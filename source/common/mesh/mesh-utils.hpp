#pragma once

#include "mesh.hpp"
#include <string>

namespace voxel {
    class Chunk;
    class World;
}

namespace our::mesh_utils {

    struct MeshBuildData {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> elements;
    };

    enum class FaceCategory {
        ALL = 0,
        TOP = 1,
        BOTTOM = 2,
        SIDES = 3
    };

    // Load an ".obj" file into the mesh
    Mesh* loadOBJ(const std::string& filename);
    // Create a sphere (the vertex order in the triangles are CCW from the outside)
    // Segments define the number of divisions on the both the latitude and the longitude
    Mesh* sphere(const glm::ivec2& segments);

    // Build indexed mesh data for one chunk using the world to cull hidden faces across chunk boundaries.
    // If blockTypeFilter is AIR (< 0 by default), all non-air blocks are included.
    MeshBuildData buildChunkMeshData(const voxel::Chunk& chunk, const voxel::World& world, int blockTypeFilter = -1, FaceCategory faceCategory = FaceCategory::ALL);

    // Convenience wrappers that allocate/update Mesh objects from chunk data.
    Mesh* buildChunkMesh(const voxel::Chunk& chunk, const voxel::World& world, int blockTypeFilter = -1, FaceCategory faceCategory = FaceCategory::ALL);
    void updateChunkMesh(Mesh* mesh, const voxel::Chunk& chunk, const voxel::World& world, int blockTypeFilter = -1, FaceCategory faceCategory = FaceCategory::ALL);
}