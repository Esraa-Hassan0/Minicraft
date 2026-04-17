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
// then adds shininess uniform and optional specular map on texture unit 1.
void LitMaterial::setup() const {
        TexturedMaterial::setup();
        if(shader) {
            shader->set("shininess", shininess);
            if(texture && sampler) {
                glActiveTexture(GL_TEXTURE0);
                texture->bind();
                sampler->bind(0);
                shader->set("albedoTex", 0);
            } else {
                static our::Texture2D* fallbackWhite = nullptr;
                if(!fallbackWhite) {
                    fallbackWhite = new our::Texture2D();
                    fallbackWhite->bind();
                    unsigned char white[4] = {255, 255, 255, 255};
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
                    fallbackWhite->unbind();
                }
                glActiveTexture(GL_TEXTURE0);
                fallbackWhite->bind();
                if(sampler) sampler->bind(0);
                shader->set("albedoTex", 0);
            }
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
    }

}