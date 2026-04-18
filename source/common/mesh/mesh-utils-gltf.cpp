#include "mesh-utils.hpp"

#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_IMPLEMENTATION
#include <tinygltf/tiny_gltf.h>

#include <stb/stb_image.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

namespace {

// tinygltf needs this for embedded images in .glb; we reuse the same stb_image
// implementation as texture-utils.cpp (no second STB_IMAGE_IMPLEMENTATION).
static bool ourGltfLoadImage(tinygltf::Image* image, int image_idx, std::string* err, std::string* warn,
                             int req_width, int req_height, const unsigned char* bytes, int size, void*) {
    (void)warn;
    int w = 0;
    int h = 0;
    int comp = 0;
    const int req_comp = 4;
    unsigned char* data = stbi_load_from_memory(bytes, size, &w, &h, &comp, req_comp);
    if (!data) {
        if (err) {
            (*err) += "stbi_load_from_memory failed for glTF image[" + std::to_string(image_idx) + "] name=\"" +
                      image->name + "\".\n";
        }
        return false;
    }
    if (w < 1 || h < 1) {
        stbi_image_free(data);
        if (err) {
            (*err) += "Invalid image dimensions for image[" + std::to_string(image_idx) + "].\n";
        }
        return false;
    }
    if (req_width > 0 && req_width != w) {
        stbi_image_free(data);
        return false;
    }
    if (req_height > 0 && req_height != h) {
        stbi_image_free(data);
        return false;
    }

    image->width = w;
    image->height = h;
    image->component = req_comp;
    image->bits = 8;
    image->pixel_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
    image->image.resize(static_cast<size_t>(w * h * req_comp));
    std::memcpy(image->image.data(), data, image->image.size());
    stbi_image_free(data);
    return true;
}


uint32_t readIndex(const tinygltf::Accessor& acc, const tinygltf::BufferView& idxBv,
                   const unsigned char* data, size_t i) {
    const int cs = tinygltf::GetComponentSizeInBytes(static_cast<uint32_t>(acc.componentType));
    int step = acc.ByteStride(idxBv);
    if (step < 0) {
        step = cs;
    }
    const unsigned char* p = data + static_cast<size_t>(step) * i;
    switch (acc.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            return static_cast<uint32_t>(*p);
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            return static_cast<uint32_t>(*reinterpret_cast<const uint16_t*>(p));
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            return *reinterpret_cast<const uint32_t*>(p);
        default:
            return 0;
    }
}

bool appendPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& prim,
                     std::vector<our::Vertex>& vertices, std::vector<GLuint>& elements) {
    if (prim.extensions.find("KHR_draco_mesh_compression") != prim.extensions.end()) {
        std::cerr << "mesh_utils::loadGLTF: Draco-compressed meshes are not supported.\n";
        return false;
    }

    if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) {
        return true;
    }

    const auto posIt = prim.attributes.find("POSITION");
    if (posIt == prim.attributes.end()) {
        return true;
    }

    const int posAccIdx = posIt->second;
    const tinygltf::Accessor& posAcc = model.accessors[posAccIdx];
    if (posAcc.type != TINYGLTF_TYPE_VEC3 || posAcc.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
        std::cerr << "mesh_utils::loadGLTF: POSITION must be VEC3 FLOAT.\n";
        return false;
    }

    const tinygltf::BufferView& posBv = model.bufferViews[posAcc.bufferView];
    const tinygltf::Buffer& posBuf = model.buffers[posBv.buffer];
    const int posStride = posAcc.ByteStride(posBv);
    if (posStride < 0) {
        return false;
    }
    const unsigned char* posBase =
        &posBuf.data[posBv.byteOffset + posAcc.byteOffset];

    const tinygltf::Accessor* nrmAcc = nullptr;
    const unsigned char* nrmBase = nullptr;
    int nrmStride = 0;
    const auto nrmIt = prim.attributes.find("NORMAL");
    if (nrmIt != prim.attributes.end()) {
        nrmAcc = &model.accessors[nrmIt->second];
        if (nrmAcc->type == TINYGLTF_TYPE_VEC3 && nrmAcc->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            const tinygltf::BufferView& bv = model.bufferViews[nrmAcc->bufferView];
            const tinygltf::Buffer& buf = model.buffers[bv.buffer];
            nrmStride = nrmAcc->ByteStride(bv);
            if (nrmStride >= 0) {
                nrmBase = &buf.data[bv.byteOffset + nrmAcc->byteOffset];
            }
        }
    }

    const tinygltf::Accessor* uvAcc = nullptr;
    const unsigned char* uvBase = nullptr;
    int uvStride = 0;
    const auto uvIt = prim.attributes.find("TEXCOORD_0");
    if (uvIt != prim.attributes.end()) {
        uvAcc = &model.accessors[uvIt->second];
        if (uvAcc->type == TINYGLTF_TYPE_VEC2 && uvAcc->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            const tinygltf::BufferView& bv = model.bufferViews[uvAcc->bufferView];
            const tinygltf::Buffer& buf = model.buffers[bv.buffer];
            uvStride = uvAcc->ByteStride(bv);
            if (uvStride >= 0) {
                uvBase = &buf.data[bv.byteOffset + uvAcc->byteOffset];
            }
        }
    }

    const GLuint base = static_cast<GLuint>(vertices.size());

    for (size_t vi = 0; vi < posAcc.count; ++vi) {
        const float* pf = reinterpret_cast<const float*>(posBase + static_cast<size_t>(posStride) * vi);
        glm::vec3 pos(pf[0], pf[1], pf[2]);

        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        if (nrmBase && nrmAcc && nrmStride >= 0 && nrmAcc->count == posAcc.count) {
            const float* nf = reinterpret_cast<const float*>(nrmBase + static_cast<size_t>(nrmStride) * vi);
            normal = glm::vec3(nf[0], nf[1], nf[2]);
        }

        glm::vec2 uv(0.0f);
        if (uvBase && uvAcc && uvStride >= 0 && vi < uvAcc->count) {
            const float* uf = reinterpret_cast<const float*>(uvBase + static_cast<size_t>(uvStride) * vi);
            uv = glm::vec2(uf[0], uf[1]);
        }

        our::Color color(255, 255, 255, 255);
        vertices.push_back({pos, color, uv, normal});
    }

    if (prim.indices >= 0) {
        const tinygltf::Accessor& idxAcc = model.accessors[prim.indices];
        const tinygltf::BufferView& idxBv = model.bufferViews[idxAcc.bufferView];
        const tinygltf::Buffer& idxBuf = model.buffers[idxBv.buffer];
        const int idxStride = idxAcc.ByteStride(idxBv);
        if (idxStride < 0) {
            return false;
        }
        const unsigned char* idxBase = &idxBuf.data[idxBv.byteOffset + idxAcc.byteOffset];
        for (size_t ii = 0; ii < idxAcc.count; ++ii) {
            elements.push_back(base + readIndex(idxAcc, idxBv, idxBase, ii));
        }
    } else {
        for (size_t ii = 0; ii + 2 < posAcc.count; ii += 3) {
            elements.push_back(base + static_cast<GLuint>(ii));
            elements.push_back(base + static_cast<GLuint>(ii + 1));
            elements.push_back(base + static_cast<GLuint>(ii + 2));
        }
    }

    return true;
}

void normalizeMesh(std::vector<our::Vertex>& vertices) {
    if (vertices.empty()) {
        return;
    }

    glm::vec3 mn(std::numeric_limits<float>::max());
    glm::vec3 mx(-std::numeric_limits<float>::max());
    for (const auto& v : vertices) {
        mn = glm::min(mn, v.position);
        mx = glm::max(mx, v.position);
    }

    const glm::vec3 center = (mn + mx) * 0.5f;
    const glm::vec3 ext = mx - mn;
    const float maxAxis = std::max(ext.x, std::max(ext.y, ext.z));
    if (maxAxis < 1e-8f) {
        return;
    }

    const float s = 0.9f / maxAxis;
    for (auto& v : vertices) {
        v.position = (v.position - center) * s;
        v.normal = glm::normalize(v.normal);
    }
}

} // namespace

our::Mesh* our::mesh_utils::loadGLTF(const std::string& filename) {
    tinygltf::TinyGLTF loader;
    loader.SetImageLoader(ourGltfLoadImage, nullptr);
    tinygltf::Model model;
    std::string err;
    std::string warn;

    bool ok = false;
    if (filename.size() >= 4 && filename.compare(filename.size() - 4, 4, ".glb") == 0) {
        ok = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    } else {
        ok = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    }

    if (!warn.empty()) {
        std::cout << "GLTF warn (" << filename << "): " << warn << std::endl;
    }
    if (!ok) {
        std::cerr << "Failed to load glTF \"" << filename << "\": " << err << std::endl;
        return nullptr;
    }

    std::vector<our::Vertex> vertices;
    std::vector<GLuint> elements;

    for (const auto& mesh : model.meshes) {
        for (const auto& prim : mesh.primitives) {
            if (!appendPrimitive(model, prim, vertices, elements)) {
                return nullptr;
            }
        }
    }

    if (vertices.empty() || elements.empty()) {
        std::cerr << "mesh_utils::loadGLTF: no geometry in \"" << filename << "\"\n";
        return nullptr;
    }

    normalizeMesh(vertices);
    return new our::Mesh(vertices, elements);
}
