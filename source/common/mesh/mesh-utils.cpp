#include "mesh-utils.hpp"

// We will use "Tiny OBJ Loader" to read and process '.obj" files
#define TINYOBJLOADER_IMPLEMENTATION
#include <tinyobj/tiny_obj_loader.h>

#include <voxel/chunk.hpp>
#include <voxel/world.hpp>
#include <voxel/types.hpp>

#include <iostream>
#include <vector>
#include <unordered_map>

namespace {
    const glm::ivec3 NEIGHBOR_OFFSETS[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };

    const glm::vec3 FACE_NORMALS[6] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };

    const glm::vec3 FACE_VERTICES[6][4] = {
        // Right (+X)
        {{1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        // Left (-X)
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        // Top (+Y)
        {{0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        // Bottom (-Y)
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        // Front (+Z)
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}},
        // Back (-Z)
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}}
    };

    const glm::vec2 FACE_UVS[4] = {
        {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}
    };

     our::Color getFaceColor(int face, int blockType) {
         if (blockType == voxel::block_types::WATER) {
             return our::Color(50, 100, 255, 200);
         }

        switch (face) {
            case 2: return our::Color(255, 255, 255, 255); // Top
            case 3: return our::Color(100, 100, 100, 255); // Bottom
            case 4:
            case 5: return our::Color(170, 170, 170, 255); // Front / Back
            default: return our::Color(200, 200, 200, 255); // Left / Right
        }
    }
}

our::Mesh* our::mesh_utils::loadOBJ(const std::string& filename) {

    // The data that we will use to initialize our mesh
    std::vector<our::Vertex> vertices;
    std::vector<GLuint> elements;

    // Since the OBJ can have duplicated vertices, we make them unique using this map
    // The key is the vertex, the value is its index in the vector "vertices".
    // That index will be used to populate the "elements" vector.
    std::unordered_map<our::Vertex, GLuint> vertex_map;

    // The data loaded by Tiny OBJ Loader
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str())) {
        std::cerr << "Failed to load obj file \"" << filename << "\" due to error: " << err << std::endl;
        return nullptr;
    }
    if (!warn.empty()) {
        std::cout << "WARN while loading obj file \"" << filename << "\": " << warn << std::endl;
    }

    // An obj file can have multiple shapes where each shape can have its own material
    // Ideally, we would load each shape into a separate mesh or store the start and end of it in the element buffer to be able to draw each shape separately
    // But we ignored this fact since we don't plan to use multiple materials in the examples
    for (const auto &shape : shapes) {
        for (const auto &index : shape.mesh.indices) {
            Vertex vertex = {};

            // Read the data for a vertex from the "attrib" object
            vertex.position = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
            };

            vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
            };

            vertex.tex_coord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    attrib.texcoords[2 * index.texcoord_index + 1]
            };


            vertex.color = {
                    attrib.colors[3 * index.vertex_index + 0] * 255,
                    attrib.colors[3 * index.vertex_index + 1] * 255,
                    attrib.colors[3 * index.vertex_index + 2] * 255,
                    255
            };

            // See if we already stored a similar vertex
            auto it = vertex_map.find(vertex);
            if (it == vertex_map.end()) {
                // if no, add it to the vertices and record its index
                auto new_vertex_index = static_cast<GLuint>(vertices.size());
                vertex_map[vertex] = new_vertex_index;
                elements.push_back(new_vertex_index);
                vertices.push_back(vertex);
            } else {
                // if yes, just add its index in the elements vector
                elements.push_back(it->second);
            }
        }
    }

    return new our::Mesh(vertices, elements);
}

// Create a sphere (the vertex order in the triangles are CCW from the outside)
// Segments define the number of divisions on the both the latitude and the longitude
our::Mesh* our::mesh_utils::sphere(const glm::ivec2& segments){
    std::vector<our::Vertex> vertices;
    std::vector<GLuint> elements;

    // We populate the sphere vertices by looping over its longitude and latitude
    for(int lat = 0; lat <= segments.y; lat++){
        float v = (float)lat / segments.y;
        float pitch = v * glm::pi<float>() - glm::half_pi<float>();
        float cos = glm::cos(pitch), sin = glm::sin(pitch);
        for(int lng = 0; lng <= segments.x; lng++){
            float u = (float)lng/segments.x;
            float yaw = u * glm::two_pi<float>();
            glm::vec3 normal = {cos * glm::cos(yaw), sin, cos * glm::sin(yaw)};
            glm::vec3 position = normal;
            glm::vec2 tex_coords = glm::vec2(u, v);
            our::Color color = our::Color(255, 255, 255, 255);
            vertices.push_back({position, color, tex_coords, normal});
        }
    }

    for(int lat = 1; lat <= segments.y; lat++){
        int start = lat*(segments.x+1);
        for(int lng = 1; lng <= segments.x; lng++){
            int prev_lng = lng-1;
            elements.push_back(lng + start);
            elements.push_back(lng + start - segments.x - 1);
            elements.push_back(prev_lng + start - segments.x - 1);
            elements.push_back(prev_lng + start - segments.x - 1);
            elements.push_back(prev_lng + start);
            elements.push_back(lng + start);
        }
    }

    return new our::Mesh(vertices, elements);
}

our::mesh_utils::MeshBuildData our::mesh_utils::buildChunkMeshData(const voxel::Chunk& chunk, const voxel::World& world, int blockTypeFilter, FaceCategory faceCategory) {
    MeshBuildData meshData;
    meshData.vertices.reserve(4000);
    meshData.elements.reserve(6000);

    for (int z = 0; z < voxel::Chunk::CHUNK_SIZE; ++z) {
        for (int y = 0; y < chunk.height; ++y) {
             for (int x = 0; x < voxel::Chunk::CHUNK_SIZE; ++x) {
                 int blockType = chunk.getBlock(x, y, z);
                 if (blockType == voxel::block_types::AIR) continue;
                if (blockTypeFilter >= 0 && blockType != blockTypeFilter) continue;

                glm::vec3 blockPos(x, y, z);
                int worldX = chunk.chunkX * voxel::Chunk::CHUNK_SIZE + x;
                int worldZ = chunk.chunkZ * voxel::Chunk::CHUNK_SIZE + z;

                for (int face = 0; face < 6; ++face) {
                    if (faceCategory == FaceCategory::TOP && face != 2) continue;
                    if (faceCategory == FaceCategory::BOTTOM && face != 3) continue;
                    if (faceCategory == FaceCategory::SIDES && (face == 2 || face == 3)) continue;

                    int nx = worldX + NEIGHBOR_OFFSETS[face].x;
                    int ny = y + NEIGHBOR_OFFSETS[face].y;
                    int nz = worldZ + NEIGHBOR_OFFSETS[face].z;

                      int neighborBlock = world.getBlock(nx, ny, nz);

                      // 1. If the neighbor is a solid, opaque block, DO NOT draw this face.
                      if (neighborBlock != voxel::block_types::AIR && 
                          neighborBlock != voxel::block_types::WATER && 
                          neighborBlock != voxel::block_types::Glass) 
                      {
                          continue;
                      }

                      // 2. If WE are a water block, and the NEIGHBOR is also water, DO NOT draw the face.
                      // (This prevents ugly grid lines from rendering inside the middle of a lake).
                      if (blockType == voxel::block_types::WATER && neighborBlock == voxel::block_types::WATER) 
                      {
                          continue;
                      }

                     unsigned int startIndex = static_cast<unsigned int>(meshData.vertices.size());
                     our::Color faceColor = getFaceColor(face, blockType);
                     
                      // Calculate lighting based on adjacent block's light level
                      int lightValue = world.getLight(
                          worldX + NEIGHBOR_OFFSETS[face].x,
                          y + NEIGHBOR_OFFSETS[face].y,
                          worldZ + NEIGHBOR_OFFSETS[face].z
                      );
                     // Map the 0-15 light level to a 0.1 - 1.0 float range
                     // We use 0.1 as a minimum so caves aren't completely pitch black
                     float lightIntensity = 0.1f + (static_cast<float>(lightValue) / 15.0f) * 0.9f;
                     // Create your color vector based on the light
                     our::Color vertexColor = {
                         static_cast<uint8_t>(faceColor.r * lightIntensity),
                         static_cast<uint8_t>(faceColor.g * lightIntensity),
                         static_cast<uint8_t>(faceColor.b * lightIntensity),
                         faceColor.a
                     };

                     for (int v = 0; v < 4; ++v) {
                         our::Vertex vertex;
                         vertex.position = blockPos + FACE_VERTICES[face][v];
                         vertex.normal = FACE_NORMALS[face];
                         vertex.color = vertexColor;
                         vertex.tex_coord = FACE_UVS[v];
                         meshData.vertices.push_back(vertex);
                     }

                    meshData.elements.push_back(startIndex + 0);
                    meshData.elements.push_back(startIndex + 1);
                    meshData.elements.push_back(startIndex + 2);

                    meshData.elements.push_back(startIndex + 2);
                    meshData.elements.push_back(startIndex + 3);
                    meshData.elements.push_back(startIndex + 0);
                }
            }
        }
    }

    return meshData;
}

our::Mesh* our::mesh_utils::buildChunkMesh(const voxel::Chunk& chunk, const voxel::World& world, int blockTypeFilter, FaceCategory faceCategory) {
    auto meshData = buildChunkMeshData(chunk, world, blockTypeFilter, faceCategory);
    if (meshData.elements.empty()) {
        return nullptr;
    }
    return new Mesh(meshData.vertices, meshData.elements);
}

void our::mesh_utils::updateChunkMesh(Mesh* mesh, const voxel::Chunk& chunk, const voxel::World& world, int blockTypeFilter, FaceCategory faceCategory) {
    if (!mesh) return;
    auto meshData = buildChunkMeshData(chunk, world, blockTypeFilter, faceCategory);
    mesh->updateBuffers(meshData.vertices, meshData.elements);
}

our::Mesh* our::mesh_utils::cubeEdges() {
    std::vector<our::Vertex> vertices = {
        // Bottom 4 vertices
        {{-0.5f, -0.5f, -0.5f}, {255, 255, 255, 255}, {0, 0}, {0, 0, 0}},
        {{ 0.5f, -0.5f, -0.5f}, {255, 255, 255, 255}, {1, 0}, {0, 0, 0}},
        {{ 0.5f, -0.5f,  0.5f}, {255, 255, 255, 255}, {1, 1}, {0, 0, 0}},
        {{-0.5f, -0.5f,  0.5f}, {255, 255, 255, 255}, {0, 1}, {0, 0, 0}},
        // Top 4 vertices
        {{-0.5f,  0.5f, -0.5f}, {255, 255, 255, 255}, {0, 0}, {0, 0, 0}},
        {{ 0.5f,  0.5f, -0.5f}, {255, 255, 255, 255}, {1, 0}, {0, 0, 0}},
        {{ 0.5f,  0.5f,  0.5f}, {255, 255, 255, 255}, {1, 1}, {0, 0, 0}},
        {{-0.5f,  0.5f,  0.5f}, {255, 255, 255, 255}, {0, 1}, {0, 0, 0}}
    };

    std::vector<unsigned int> elements = {
        // Bottom square
        0, 1, 1, 2, 2, 3, 3, 0,
        // Top square
        4, 5, 5, 6, 6, 7, 7, 4,
        // Vertical edges
        0, 4, 1, 5, 2, 6, 3, 7
    };

    return new Mesh(vertices, elements, GL_LINES);
}