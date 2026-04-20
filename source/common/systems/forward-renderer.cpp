#include "forward-renderer.hpp"
#include "../mesh/mesh-utils.hpp"
#include "../texture/texture-utils.hpp"
#include "../material/material.hpp"
#include "../components/player.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace our
{

    void ForwardRenderer::setSunData(const glm::vec3& screenPos, const glm::vec3& color, float intensity)
    {
        this->sunScreenPosition = screenPos;
        this->sunColor = color;
        this->sunIntensity = intensity;
    }

    void ForwardRenderer::initialize(glm::ivec2 windowSize, const nlohmann::json &config)
    {
        // First, we store the window size for later use
        this->windowSize = windowSize;

        // Then we check if there is a sky texture in the configuration
        if (config.contains("sky"))
        {
            // First, we create a sphere which will be used to draw the sky
            this->skySphere = mesh_utils::sphere(glm::ivec2(16, 16));

            // Draw sky as an unlit textured dome so it is not affected by scene lights/shadows.
            ShaderProgram *skyShader = new ShaderProgram();
            skyShader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
            skyShader->attach("assets/shaders/textured.frag", GL_FRAGMENT_SHADER);
            skyShader->link();

            // TODO: (Req 10) Pick the correct pipeline state to draw the sky
            //  Hints: the sky will be draw after the opaque objects so we would need depth testing but which depth funtion should we pick?
            //  We will draw the sphere from the inside, so what options should we pick for the face culling.
            PipelineState skyPipelineState{};
            skyPipelineState.depthTesting.enabled = true;
            skyPipelineState.depthTesting.function = GL_LEQUAL;
            skyPipelineState.faceCulling.enabled = true;
            skyPipelineState.faceCulling.culledFace = GL_FRONT;

            // Load the sky texture (note that we don't need mipmaps since we want to avoid any unnecessary blurring while rendering the sky)
            std::string skyTextureFile = config.value<std::string>("sky", "");
            Texture2D *skyTexture = texture_utils::loadImage(skyTextureFile, false);

            // Setup a sampler for the sky
            Sampler *skySampler = new Sampler();
            skySampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            skySampler->set(GL_TEXTURE_WRAP_S, GL_REPEAT);
            skySampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Combine all the aforementioned objects (except the mesh) into a material
            this->skyMaterial = new TexturedMaterial();
            this->skyMaterial->shader = skyShader;
            this->skyMaterial->texture = skyTexture;
            this->skyMaterial->sampler = skySampler;
            this->skyMaterial->pipelineState = skyPipelineState;
            this->skyMaterial->tint = glm::vec4(1.0f);
            this->skyMaterial->alphaThreshold = 1.0f;
            this->skyMaterial->transparent = false;
        }

        // Then we check if there is a postprocessing shader in the configuration
        if (config.contains("postprocess"))
        {
            // TODO: (Req 11) Create a framebuffer
            glGenFramebuffers(1, &postprocessFrameBuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, postprocessFrameBuffer);

            // TODO: (Req 11) Create a color and a depth texture and attach them to the framebuffer
            //  Hints: The color format can be (Red, Green, Blue and Alpha components with 8 bits for each channel).
            //  The depth format can be (Depth component with 24 bits).
            colorTarget = texture_utils::empty(GL_RGBA8, windowSize);
            depthTarget = texture_utils::empty(GL_DEPTH_COMPONENT24, windowSize);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTarget->getOpenGLName(), 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTarget->getOpenGLName(), 0);

            // TODO: (Req 11) Unbind the framebuffer just to be safe
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            // Create a vertex array to use for drawing the texture
            glGenVertexArrays(1, &postProcessVertexArray);

            // Create a sampler to use for sampling the scene texture in the post processing shader
            Sampler *postprocessSampler = new Sampler();
            postprocessSampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            postprocessSampler->set(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            postprocessSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Create a sampler for the depth texture
            depthSampler = new Sampler();
            depthSampler->set(GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            depthSampler->set(GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            depthSampler->set(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            depthSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            // Create the post processing shader
            ShaderProgram *postprocessShader = new ShaderProgram();
            postprocessShader->attach("assets/shaders/fullscreen.vert", GL_VERTEX_SHADER);
            postprocessShader->attach(config.value<std::string>("postprocess", ""), GL_FRAGMENT_SHADER);
            postprocessShader->link();

            // Create a post processing material
            postprocessMaterial = new TexturedMaterial();
            postprocessMaterial->shader = postprocessShader;
            postprocessMaterial->texture = colorTarget;
            postprocessMaterial->sampler = postprocessSampler;
            // The default options are fine but we don't need to interact with the depth buffer
            // so it is more performant to disable the depth mask
            postprocessMaterial->pipelineState.depthMask = false;
            postprocessMaterial->pipelineState.depthTesting.enabled = false;
            postprocessMaterial->alphaThreshold = 0.0f;
            postprocessMaterial->tint = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        }

        // Initialize resources for directional shadow mapping.
        glGenFramebuffers(1, &shadowMapFrameBuffer);
        shadowDepthTarget = texture_utils::empty(GL_DEPTH_COMPONENT24, shadowMapSize);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFrameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowDepthTarget->getOpenGLName(), 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        shadowSampler = new Sampler();
        shadowSampler->set(GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        shadowSampler->set(GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        shadowSampler->set(GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        shadowSampler->set(GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        shadowSampler->set(GL_TEXTURE_BORDER_COLOR, glm::vec4(1.0f));

        shadowMapShader = new ShaderProgram();
        shadowMapShader->attach("assets/shaders/shadow-map.vert", GL_VERTEX_SHADER);
        shadowMapShader->attach("assets/shaders/shadow-map.frag", GL_FRAGMENT_SHADER);
        shadowMapShader->link();
    }

    void ForwardRenderer::destroy()
    {
        // Delete all objects related to the sky
        if (skyMaterial)
        {
            delete skySphere;
            delete skyMaterial->shader;
            delete skyMaterial->texture;
            delete skyMaterial->sampler;
            delete skyMaterial;
        }
        // Delete all objects related to post processing
        if (postprocessMaterial)
        {
            glDeleteFramebuffers(1, &postprocessFrameBuffer);
            glDeleteVertexArrays(1, &postProcessVertexArray);
            delete colorTarget;
            delete depthTarget;
            delete depthSampler;
            delete postprocessMaterial->sampler;
            delete postprocessMaterial->shader;
            delete postprocessMaterial;
        }

        if (shadowMapShader)
        {
            delete shadowMapShader;
            shadowMapShader = nullptr;
        }
        if (shadowSampler)
        {
            delete shadowSampler;
            shadowSampler = nullptr;
        }
        if (shadowDepthTarget)
        {
            delete shadowDepthTarget;
            shadowDepthTarget = nullptr;
        }
        if (shadowMapFrameBuffer != 0)
        {
            glDeleteFramebuffers(1, &shadowMapFrameBuffer);
            shadowMapFrameBuffer = 0;
        }
    }

    void ForwardRenderer::render(World *world)
    {
        // Default sun data (will be overwritten if sun entity found)
        sunScreenPosition = glm::vec3(0.5f, 0.5f, 0.0f);
        sunColor = glm::vec3(1.0f, 0.9f, 0.7f);
        sunIntensity = 1.0f;
        shadowsEnabled = false;
        shadowLightDirection = glm::vec3(0.0f, -1.0f, 0.0f);
        lightSpaceMatrix = glm::mat4(1.0f);

        CameraComponent *camera = nullptr;
        Entity *sunEntity = nullptr;
        opaqueCommands.clear();
        transparentCommands.clear();
        lights.clear();

        auto isSunEntityName = [](const std::string &name)
        {
            return name == "sun" || name == "Sun";
        };

        for (auto entity : world->getEntities())
        {
            if (!camera)
                camera = entity->getComponent<CameraComponent>();

            if (!sunEntity && isSunEntityName(entity->name))
                sunEntity = entity;

            if (auto meshRenderer = entity->getComponent<MeshRendererComponent>(); meshRenderer && meshRenderer->enabled)
            {
                RenderCommand command;
                command.localToWorld = meshRenderer->getOwner()->getLocalToWorldMatrix();
                command.center = glm::vec3(command.localToWorld * glm::vec4(0, 0, 0, 1));
                command.mesh = meshRenderer->mesh;
                command.material = meshRenderer->material;

                if (command.material->transparent)
                {
                    transparentCommands.push_back(command);
                }
                else
                {
                    opaqueCommands.push_back(command);
                }
            }

            if (auto *lc = entity->getComponent<LightComponent>())
            {
                if (!lc->enabled)
                    continue;

                LightData ld;
                ld.type = (int)lc->type;

                glm::mat4 ltw = entity->getLocalToWorldMatrix();
                ld.position = glm::vec3(ltw * glm::vec4(0, 0, 0, 1));
                ld.direction = glm::normalize(glm::vec3(ltw * glm::vec4(0, -1, 0, 0)));

                ld.color = lc->color;
                ld.ambient = lc->ambient;
                ld.attenConstant = lc->attenConstant;
                ld.attenLinear = lc->attenLinear;
                ld.attenQuadratic = lc->attenQuadratic;
                ld.innerCutoff = glm::cos(glm::radians(lc->innerCutoffDeg));
                ld.outerCutoff = glm::cos(glm::radians(lc->outerCutoffDeg));

                lights.push_back(ld);

                if (!shadowsEnabled && lc->type == LightType::DIRECTIONAL)
                {
                    shadowsEnabled = true;
                    shadowLightDirection = ld.direction;
                }
            }
        }

        if (camera == nullptr)
            return;

        bool isCameraUnderwater = false;
        if (auto* cameraPlayer = camera->getOwner()->getComponent<PlayerComponent>())
        {
            isCameraUnderwater = cameraPlayer->isUnderwater;
        }

        auto uploadLights = [&](ShaderProgram *sh, const glm::vec3 &cameraPos)
        {
            int n = (int)std::min(lights.size(), (size_t)8);
            sh->set("numLights", n);
            sh->set("cameraPos", cameraPos);
            for (int i = 0; i < n; i++)
            {
                std::string b = "lights[" + std::to_string(i) + "].";
                sh->set(b + "type", lights[i].type);
                sh->set(b + "position", lights[i].position);
                sh->set(b + "direction", lights[i].direction);
                sh->set(b + "color", lights[i].color);
                sh->set(b + "ambient", lights[i].ambient);
                sh->set(b + "attenConstant", lights[i].attenConstant);
                sh->set(b + "attenLinear", lights[i].attenLinear);
                sh->set(b + "attenQuadratic", lights[i].attenQuadratic);
                sh->set(b + "innerCutoff", lights[i].innerCutoff);
                sh->set(b + "outerCutoff", lights[i].outerCutoff);
            }
        };

        auto uploadShadowData = [&](ShaderProgram *sh)
        {
            sh->set("enableShadows", shadowsEnabled ? 1 : 0);
            sh->set("lightSpaceMatrix", lightSpaceMatrix);
            sh->set("shadowLightDir", shadowLightDirection);

            if (shadowDepthTarget && shadowSampler)
            {
                glActiveTexture(GL_TEXTURE2);
                shadowDepthTarget->bind();
                shadowSampler->bind(2);
                sh->set("shadowMap", 2);
            }
        };

        glm::vec3 cameraForward = camera->getOwner()->getLocalToWorldMatrix() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        std::sort(transparentCommands.begin(), transparentCommands.end(), [cameraForward](const RenderCommand &first, const RenderCommand &second)
                  {
                      return glm::dot(cameraForward, first.center) > glm::dot(cameraForward, second.center);
                  });

        glm::mat4 VP = camera->getProjectionMatrix(windowSize) * camera->getViewMatrix();
        glm::vec3 cameraPosition = camera->getOwner()->getLocalToWorldMatrix() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        if (shadowsEnabled && shadowMapShader && shadowDepthTarget && shadowSampler)
        {
            glm::vec3 lightDir = glm::normalize(shadowLightDirection);
            glm::vec3 up = (std::abs(lightDir.y) > 0.98f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

            constexpr float shadowDistance = 65.0f;
            constexpr float shadowNear = 1.0f;
            constexpr float shadowFar = 220.0f;

            glm::vec3 lightPosition = cameraPosition - lightDir * 90.0f;
            glm::mat4 lightView = glm::lookAt(lightPosition, cameraPosition, up);
            glm::mat4 lightProjection = glm::ortho(-shadowDistance, shadowDistance, -shadowDistance, shadowDistance, shadowNear, shadowFar);
            lightSpaceMatrix = lightProjection * lightView;

            glBindFramebuffer(GL_FRAMEBUFFER, shadowMapFrameBuffer);
            glViewport(0, 0, shadowMapSize.x, shadowMapSize.y);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glDepthMask(GL_TRUE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LESS);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            glClearDepth(1.0f);
            glClear(GL_DEPTH_BUFFER_BIT);

            shadowMapShader->use();
            for (auto &command : opaqueCommands)
            {
                shadowMapShader->set("transform", lightSpaceMatrix * command.localToWorld);
                command.mesh->draw();
            }

            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        if (sunEntity)
        {
            glm::vec3 sunWorldPos = sunEntity->getLocalToWorldMatrix() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            glm::vec4 sunClipPos = VP * glm::vec4(sunWorldPos, 1.0f);
            if (sunClipPos.w > 0.0f)
            {
                sunScreenPosition = glm::vec3(
                    (sunClipPos.x / sunClipPos.w) * 0.5f + 0.5f,
                    (sunClipPos.y / sunClipPos.w) * 0.5f + 0.5f,
                    sunClipPos.w);

                if (auto *lc = sunEntity->getComponent<LightComponent>())
                {
                    sunColor = lc->color;
                    sunIntensity = lc->enabled ? 1.0f : 0.0f;
                }
            }
        }

        glViewport(0, 0, windowSize.x, windowSize.y);
        if (isCameraUnderwater)
        {
            glClearColor(0.08f, 0.24f, 0.38f, 1.0f);
        }
        else
        {
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }
        glClearDepth(1.0f);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);

        if (postprocessMaterial)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, postprocessFrameBuffer);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (auto &command : opaqueCommands)
        {
            command.material->setup();
            auto *sh = command.material->shader;
            sh->set("transform", VP * command.localToWorld);

            if (dynamic_cast<LitMaterial *>(command.material))
            {
                sh->set("model", command.localToWorld);
                sh->set("normalMatrix", glm::mat3(glm::transpose(glm::inverse(command.localToWorld))));
                uploadLights(sh, cameraPosition);
                uploadShadowData(sh);

                float maxFlashStrength = 0.0f;
                glm::vec3 activeFlashColor = glm::vec3(1.0f, 0.0f, 0.0f);
                for (auto entity : world->getEntities())
                {
                    if (auto *lc = entity->getComponent<LightComponent>())
                    {
                        if (lc->flashTimer > 0.0f)
                        {
                            float strength = std::min(lc->flashTimer, 1.0f);
                            if (strength > maxFlashStrength)
                            {
                                maxFlashStrength = strength;
                                activeFlashColor = lc->flashColor;
                            }
                        }
                    }
                }
                sh->set("flashStrength", maxFlashStrength);
                sh->set("flashColor", activeFlashColor);
            }

            command.mesh->draw();
        }

        if (this->skyMaterial && !isCameraUnderwater)
        {
            this->skyMaterial->setup();
            glm::mat4 skyModel = glm::translate(glm::mat4(1.0f), cameraPosition);
            glm::mat4 alwaysBehindTransform = glm::mat4(1.0f);
            alwaysBehindTransform[2][2] = 0.0f;
            alwaysBehindTransform[3][2] = 1.0f;
            this->skyMaterial->shader->set("transform", alwaysBehindTransform * VP * skyModel);

            this->skySphere->draw();
        }

        for (auto &command : transparentCommands)
        {
            command.material->setup();
            auto *sh = command.material->shader;
            sh->set("transform", VP * command.localToWorld);

            if (dynamic_cast<LitMaterial *>(command.material))
            {
                sh->set("model", command.localToWorld);
                sh->set("normalMatrix", glm::mat3(glm::transpose(glm::inverse(command.localToWorld))));
                uploadLights(sh, cameraPosition);
                uploadShadowData(sh);

                float maxFlashStrength = 0.0f;
                glm::vec3 activeFlashColor = glm::vec3(1.0f, 0.0f, 0.0f);
                for (auto entity : world->getEntities())
                {
                    if (auto *lc = entity->getComponent<LightComponent>())
                    {
                        if (lc->flashTimer > 0.0f)
                        {
                            float strength = std::min(lc->flashTimer, 1.0f);
                            if (strength > maxFlashStrength)
                            {
                                maxFlashStrength = strength;
                                activeFlashColor = lc->flashColor;
                            }
                        }
                    }
                }
                sh->set("flashStrength", maxFlashStrength);
                sh->set("flashColor", activeFlashColor);
            }

            command.mesh->draw();
        }

        if (postprocessMaterial)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            postprocessMaterial->setup();
            postprocessMaterial->shader->set("sunScreenPos", sunScreenPosition);
            postprocessMaterial->shader->set("sunColor", sunColor);
            postprocessMaterial->shader->set("sunIntensity", sunIntensity);
            postprocessMaterial->shader->set("sunDensity", 0.3f);
            postprocessMaterial->shader->set("sunWeight", 0.02f);
            postprocessMaterial->shader->set("sunDecay", 0.96f);

            glActiveTexture(GL_TEXTURE1);
            depthSampler->bind(1);
            depthTarget->bind();
            postprocessMaterial->shader->set("depthTex", 1);

            if (camera)
            {
                postprocessMaterial->shader->set("cameraNear", camera->near);
                postprocessMaterial->shader->set("cameraFar", camera->far);
            }

            postprocessMaterial->shader->set("enableFog", fogEnabled);
            postprocessMaterial->shader->set("isNight", fogEnabled);
            postprocessMaterial->shader->set("isUnderwater", isCameraUnderwater ? 1 : 0);

            glBindVertexArray(postProcessVertexArray);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    void ForwardRenderer::setFogEnabled(bool enabled)
    {
        this->fogEnabled = enabled;
    }

}