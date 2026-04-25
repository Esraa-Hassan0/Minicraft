#include "material.hpp"

#include "../asset-loader.hpp"
#include "deserialize-utils.hpp"
#include "../texture/texture-utils.hpp"

namespace our {

    // This function should setup the pipeline state and set the shader to be used
    void Material::setup() const {
        //TODO: (Req 7) Write this function
        pipelineState.setup();
        if(shader) shader->use();
    }

    // This function read the material data from a json object
    void Material::deserialize(const nlohmann::json& data){
        if(!data.is_object()) return;

        if(data.contains("pipelineState")){
            pipelineState.deserialize(data["pipelineState"]);
        }
        shader = AssetLoader<ShaderProgram>::get(data["shader"].get<std::string>());
        transparent = data.value("transparent", false);
    }

    // This function should call the setup of its parent and
    // set the "tint" uniform to the value in the member variable tint 
    void TintedMaterial::setup() const {
        //TODO: (Req 7) Write this function
        Material::setup();
        if(shader) shader->set("tint", tint);
    }

    // This function read the material data from a json object
    void TintedMaterial::deserialize(const nlohmann::json& data){
        Material::deserialize(data);
        if(!data.is_object()) return;
        tint = data.value("tint", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    }

    // This function should call the setup of its parent and
    // set the "alphaThreshold" uniform to the value in the member variable alphaThreshold
    // Then it should bind the texture and sampler to a texture unit and send the unit number to the uniform variable "tex" 
    void TexturedMaterial::setup() const {
        //TODO: (Req 7) Write this function
        TintedMaterial::setup();
        if(shader){
            shader->set("alphaThreshold", alphaThreshold);
            if(texture && sampler){
                glActiveTexture(GL_TEXTURE0);
                texture->bind();
                sampler->bind(0);
                shader->set("tex", 0);
            }
        }
    }

    // This function read the material data from a json object
    void TexturedMaterial::deserialize(const nlohmann::json& data){
        TintedMaterial::deserialize(data);
        if(!data.is_object()) return;
        alphaThreshold = data.value("alphaThreshold", 0.0f);
        texture = AssetLoader<Texture2D>::get(data.value("texture", ""));
        sampler = AssetLoader<Sampler>::get(data.value("sampler", ""));
    }

    // Sets up the LitMaterial for rendering.
// Calls parent TexturedMaterial::setup() for albedo texture,
// then adds shininess uniform and texture maps on units 1-4.
void LitMaterial::setup() const {
        TexturedMaterial::setup();
        if(shader) {
            shader->set("shininess", shininess);
            shader->set("specular_tint", specular);
            shader->set("emission", emission);
            
            // Unit 0: Albedo (already set by parent)
            
            // Unit 1: Specular
            if(specularMap && specularSampler) {
                glActiveTexture(GL_TEXTURE1);
                specularMap->bind();
                specularSampler->bind(1);
                shader->set("specularTex", 1);
            } else {
                static our::Texture2D* fallbackBlack = nullptr;
                if(!fallbackBlack) {
                    fallbackBlack = new our::Texture2D();
                    fallbackBlack->bind();
                    unsigned char black[4] = {0, 0, 0, 255};
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
                    fallbackBlack->unbind();
                }
                glActiveTexture(GL_TEXTURE1);
                fallbackBlack->bind();
                if(specularSampler) specularSampler->bind(1);
                shader->set("specularTex", 1);
            }
            
            // Unit 2: Roughness
            if(roughnessMap && roughnessSampler) {
                glActiveTexture(GL_TEXTURE2);
                roughnessMap->bind();
                roughnessSampler->bind(2);
                shader->set("roughnessTex", 2);
            } else {
                static our::Texture2D* fallbackGray = nullptr;
                if(!fallbackGray) {
                    fallbackGray = new our::Texture2D();
                    fallbackGray->bind();
                    unsigned char gray[4] = {128, 128, 128, 255};
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
                    fallbackGray->unbind();
                }
                glActiveTexture(GL_TEXTURE2);
                fallbackGray->bind();
                if(roughnessSampler) roughnessSampler->bind(2);
                shader->set("roughnessTex", 2);
            }
            
            // Unit 3: Ambient Occlusion
            if(aoMap && aoSampler) {
                glActiveTexture(GL_TEXTURE3);
                aoMap->bind();
                aoSampler->bind(3);
                shader->set("aoTex", 3);
            } else {
                static our::Texture2D* fallbackWhite = nullptr;
                if(!fallbackWhite) {
                    fallbackWhite = new our::Texture2D();
                    fallbackWhite->bind();
                    unsigned char white[4] = {255, 255, 255, 255};
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
                    fallbackWhite->unbind();
                }
                glActiveTexture(GL_TEXTURE3);
                fallbackWhite->bind();
                if(aoSampler) aoSampler->bind(3);
                shader->set("aoTex", 3);
            }
            
            // Unit 4: Emission
            if(emissionMap && emissionSampler) {
                glActiveTexture(GL_TEXTURE4);
                emissionMap->bind();
                emissionSampler->bind(4);
                shader->set("emissiveTex", 4);
            } else {
                static our::Texture2D* fallbackBlack = nullptr;
                if(!fallbackBlack) {
                    fallbackBlack = new our::Texture2D();
                    fallbackBlack->bind();
                    unsigned char black[4] = {0, 0, 0, 255};
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
                    fallbackBlack->unbind();
                }
                glActiveTexture(GL_TEXTURE4);
                fallbackBlack->bind();
                if(emissionSampler) emissionSampler->bind(4);
                shader->set("emissiveTex", 4);
            }
        }
    }

    // Deserializes LitMaterial properties from JSON.
    // Expected fields:
    //   - shininess: Phong exponent (default: 32.0)
    //   - specularMap: name of specular texture in AssetLoader
    //   - specularSampler: name of sampler in AssetLoader
    void LitMaterial::deserialize(const nlohmann::json& data) {
        // Deserialize parent first (albedo, pipeline, shader)
        TexturedMaterial::deserialize(data);

        if(!data.is_object()) return;

        // Parse shininess exponent
        shininess = data.value("shininess", 32.0f);

        // Load specular map and sampler from AssetLoader by name
        specularMap = AssetLoader<Texture2D>::get(data.value("specularMap", ""));
        specularSampler = AssetLoader<Sampler>::get(data.value("specularSampler", ""));

        // Load roughness map and sampler
        roughnessMap = AssetLoader<Texture2D>::get(data.value("roughnessMap", ""));
        roughnessSampler = AssetLoader<Sampler>::get(data.value("roughnessSampler", ""));

        // Load AO map and sampler
        aoMap = AssetLoader<Texture2D>::get(data.value("aoMap", ""));
        aoSampler = AssetLoader<Sampler>::get(data.value("aoSampler", ""));

        // Load emission map and sampler
        emissionMap = AssetLoader<Texture2D>::get(data.value("emissionMap", ""));
        emissionSampler = AssetLoader<Sampler>::get(data.value("emissionSampler", ""));

        // Read the specular array from the JSON
        if (data.contains("specular") && data["specular"].is_array() && data["specular"].size() == 3) {
            specular = glm::vec3(data["specular"][0], data["specular"][1], data["specular"][2]);
        } else {
            specular = glm::vec3(1.0f); // Default to fully reflective
        }

        // Read the emission array from the JSON
        if (data.contains("emission") && data["emission"].is_array() && data["emission"].size() == 3) {
            emission = glm::vec3(data["emission"][0], data["emission"][1], data["emission"][2]);
        } else {
            emission = glm::vec3(0.0f); // Default to no emission
        }
    }

}