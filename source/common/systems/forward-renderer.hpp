#pragma once

#include "../ecs/world.hpp"
#include "../components/camera.hpp"
#include "../components/mesh-renderer.hpp"
#include "../components/light.hpp"
#include "../asset-loader.hpp"

#include <glad/gl.h>
#include <vector>
#include <algorithm>

namespace our
{
    
    // The render command stores command that tells the renderer that it should draw
    // the given mesh at the given localToWorld matrix using the given material
    // The renderer will fill this struct using the mesh renderer components
    struct RenderCommand {
        glm::mat4 localToWorld;
        glm::vec3 center;
        Entity* owner;
        Mesh* mesh;
        Material* material;
    };

    // Light data structure for passing to shaders each frame.
    // Mirrors the Light struct in lit.frag shader.
    // Filled from LightComponent each frame in ForwardRenderer::render().
    struct LightData {
        int type;              // 0=directional, 1=point, 2=spot
        glm::vec3 position;    // World position (point/spot lights)
        glm::vec3 direction;  // Direction vector (directional/spot axis)
        glm::vec3 color;       // Light diffuse/specular color
        glm::vec3 ambient;     // Ambient contribution
        float attenConstant;   // Attenuation constant term
        float attenLinear;     // Attenuation linear term
        float attenQuadratic;  // Attenuation quadratic term
        float innerCutoff;     // Cosine of inner spot angle
        float outerCutoff;      // Cosine of outer spot angle
    };

    // A forward renderer is a renderer that draw the object final color directly to the framebuffer
    // In other words, the fragment shader in the material should output the color that we should see on the screen
    // This is different from more complex renderers that could draw intermediate data to a framebuffer before computing the final color
    // In this project, we only need to implement a forward renderer
    class ForwardRenderer {
        // These window size will be used on multiple occasions (setting the viewport, computing the aspect ratio, etc.)
        glm::ivec2 windowSize;
        // These are two vectors in which we will store the opaque and the transparent commands.
        // We define them here (instead of being local to the "render" function) as an optimization to prevent reallocating them every frame
        std::vector<RenderCommand> opaqueCommands;
        std::vector<RenderCommand> transparentCommands;
        std::vector<LightData> lights;
        // Objects used for rendering a skybox
        Mesh* skySphere;
        TexturedMaterial* skyMaterial;
        // Objects used for Postprocessing
        GLuint postprocessFrameBuffer, postProcessVertexArray;
        Texture2D *colorTarget, *depthTarget;
        TexturedMaterial* postprocessMaterial;
    glm::vec3 sunScreenPosition;
        glm::vec3 sunColor;
        float sunIntensity;
        float sunDensity;
        float sunWeight;
        float sunDecay;

        Sampler* depthSampler;
        bool fogEnabled = false;
    public:
        void setSunData(const glm::vec3& screenPos, const glm::vec3& color, float intensity);

        // Initialize the renderer including the sky and the Postprocessing objects.
        // windowSize is the width & height of the window (in pixels).
        void initialize(glm::ivec2 windowSize, const nlohmann::json& config);
        // Clean up the renderer
        void destroy();
        // This function should be called every frame to draw the given world
        void render(World* world);
        // Set whether fog postprocessing is enabled (for day/night cycle)
        void setFogEnabled(bool enabled);


    };

}