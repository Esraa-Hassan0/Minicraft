#pragma once

#include "pipeline-state.hpp"
#include "../texture/texture2d.hpp"
#include "../texture/sampler.hpp"
#include "../shader/shader.hpp"

#include <glm/vec4.hpp>
#include <json/json.hpp>

namespace our {

    // This is the base class for all the materials
    // It contains the 3 essential components required by any material
    // 1- The pipeline state when drawing objects using this material
    // 2- The shader program used to draw objects using this material
    // 3- Whether this material is transparent or not
    // Materials that send uniforms to the shader should inherit from the is material and add the required uniforms
    class Material {
    public:
        PipelineState pipelineState;
        ShaderProgram* shader;
        bool transparent;
        
        // This function does 2 things: setup the pipeline state and set the shader program to be used
        virtual void setup() const;
        // This function read a material from a json object
        virtual void deserialize(const nlohmann::json& data);
    };

    // This material adds a uniform for a tint (a color that will be sent to the shader)
    // An example where this material can be used is when the whole object has only color which defined by tint
    class TintedMaterial : public Material {
    public:
        glm::vec4 tint;

        void setup() const override;
        void deserialize(const nlohmann::json& data) override;
    };

    // This material adds two uniforms (besides the tint from Tinted Material)
    // The uniforms are:
    // - "tex" which is a Sampler2D. "texture" and "sampler" will be bound to it.
    // - "alphaThreshold" which defined the alpha limit below which the pixel should be discarded
    // An example where this material can be used is when the object has a texture
    class TexturedMaterial : public TintedMaterial {
    public:
        Texture2D* texture;
        Sampler* sampler;
        float alphaThreshold;

        void setup() const override;
        void deserialize(const nlohmann::json& data) override;
    };

    // Material supporting Blinn-Phong lighting with texture maps.
// Inherits from TexturedMaterial (albedo texture) and adds specular, roughness, AO, and emission maps.
// Uses lit.vert/lit.frag shaders for per-fragment lighting.
     class LitMaterial : public TexturedMaterial {
         public:
         // Specular map (unit 1): grayscale texture controlling specular intensity
         // White = full specular reflection, black = no specular
         Texture2D* specularMap = nullptr;
         Sampler* specularSampler = nullptr;
         // Roughness map (unit 2): grayscale controlling surface roughness
         // White = rough (broad highlight), black = smooth (sharp highlight)
         Texture2D* roughnessMap = nullptr;
         Sampler* roughnessSampler = nullptr;
         // Ambient occlusion map (unit 3): darkens areas without light access
         // White = full lighting, black = fully shadowed
         Texture2D* aoMap = nullptr;
         Sampler* aoSampler = nullptr;
         // Emission map (unit 4): areas that emit light (glow)
         Texture2D* emissionMap = nullptr;
         Sampler* emissionSampler = nullptr;
         // Shininess exponent: higher = smaller, sharper specular highlight
         // Typical range: 8.0 (rough) to 128.0 (shiny)
         float shininess = 32.0f;
         // Specular tint from JSON: multiplies the final specular highlight
         // vec3(0,0,0) = no specular (matte), vec3(1,1,1) = full specular (shiny)
         glm::vec3 specular = glm::vec3(1.0f);
         // Emission color: light emitted by this surface
         glm::vec3 emission = glm::vec3(0.0f);

         // Sets up pipeline state and binds texture maps to units 1-4
         void setup() const override;
         // Reads material properties from JSON
         void deserialize(const nlohmann::json& data) override;
     };

    // This function returns a new material instance based on the given type
    inline Material* createMaterialFromType(const std::string& type){
        if(type == "tinted"){
            return new TintedMaterial();
        } else if(type == "textured"){
            return new TexturedMaterial();
        } else if(type == "lit"){
            return new LitMaterial();
        } else {
            return new Material();
        }
    }

}