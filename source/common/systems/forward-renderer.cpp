#include "forward-renderer.hpp"
#include "../mesh/mesh-utils.hpp"
#include "../texture/texture-utils.hpp"
#include "../material/material.hpp"

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

            // We can draw the sky using the lit shader instead of textured
            ShaderProgram *skyShader = new ShaderProgram();
            skyShader->attach("assets/shaders/lit.vert", GL_VERTEX_SHADER);
            skyShader->attach("assets/shaders/lit.frag", GL_FRAGMENT_SHADER);
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
            LitMaterial* litSkyMat = new LitMaterial();
            litSkyMat->shininess = 256.0f; // making it shiny
            this->skyMaterial = litSkyMat;
            this->skyMaterial->shader = skyShader;
            this->skyMaterial->texture = skyTexture;
            this->skyMaterial->sampler = skySampler;
            this->skyMaterial->pipelineState = skyPipelineState;
            this->skyMaterial->tint = glm::vec4(0.3f, 0.6f, 1.0f, 1.0f); // making it blue-like
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
    }

    void ForwardRenderer::render(World *world)
    {
        // Default sun data (will be overwritten if sun entity found)
        sunScreenPosition = glm::vec3(0.5f, 0.5f, 0.0f);
        sunColor = glm::vec3(1.0f, 0.9f, 0.7f);
        sunIntensity = 1.0f;

        // PHASE 1: Collect all renderable components from the ECS world.
        // We iterate through all entities looking for:
        //   - CameraComponent: required for rendering (provides view/projection)
        //   - MeshRendererComponent: defines geometry to draw
        //   - LightComponent: defines lights affecting lit materials

        CameraComponent *camera = nullptr;
        Entity* sunEntity = nullptr;
        opaqueCommands.clear();
        transparentCommands.clear();
        lights.clear();

        for (auto entity : world->getEntities())
        {
            // Find the first camera in the world (needed for rendering)
            if (!camera)
                camera = entity->getComponent<CameraComponent>();

            // Find the sun entity for god rays calculation
            if (!sunEntity && entity->name == "sun")
                sunEntity = entity;

            // Collect mesh renderer components into render commands
            if (auto meshRenderer = entity->getComponent<MeshRendererComponent>(); meshRenderer && meshRenderer->enabled)
            {
                RenderCommand command;
                command.localToWorld = meshRenderer->getOwner()->getLocalToWorldMatrix();
                command.center = glm::vec3(command.localToWorld * glm::vec4(0, 0, 0, 1));
                command.mesh = meshRenderer->mesh;
                command.material = meshRenderer->material;

                // Separate transparent and opaque objects for proper blending.
                // Transparent objects drawn back-to-front, opaque drawn in any order.
                if (command.material->transparent)
                {
                    transparentCommands.push_back(command);
                }
                else
                {
                    opaqueCommands.push_back(command);
                }
            }

            // Collect lights into light data array
            // Each LightComponent becomes a LightData sent to lit shaders each frame
            if (auto *lc = entity->getComponent<LightComponent>())
            {
                if (!lc->enabled) continue;
                LightData ld;
                ld.type = (int)lc->type;

                // Get world position from entity's transform matrix
                glm::mat4 ltw = entity->getLocalToWorldMatrix();
                ld.position = glm::vec3(ltw * glm::vec4(0, 0, 0, 1));

                // Direction: default down (-Y) transformed by entity rotation
                ld.direction = glm::normalize(glm::vec3(ltw * glm::vec4(0, -1, 0, 0)));

                // Copy light properties
                ld.color = lc->color;
                ld.ambient = lc->ambient;
                ld.attenConstant = lc->attenConstant;
                ld.attenLinear = lc->attenLinear;
                ld.attenQuadratic = lc->attenQuadratic;

                // Convert angles to cosines for shader (avoids runtime trig)
                ld.innerCutoff = glm::cos(glm::radians(lc->innerCutoffDeg));
                ld.outerCutoff = glm::cos(glm::radians(lc->outerCutoffDeg));

                lights.push_back(ld);
            }
        }

        // Cannot render without a camera
        if (camera == nullptr)
            return;

        // PHASE 2: Upload lights to shaders
        // Lambda that sends light array to a shader program.
        // Called for each LitMaterial draw call.
        // Note: MAX_LIGHTS is 8 in shader, so we clamp the count.
        auto uploadLights = [&](ShaderProgram *sh, const glm::vec3 &cameraPos)
        {
            int n = (int)std::min(lights.size(), (size_t)8);
            sh->set("numLights", n);
            sh->set("cameraPos", cameraPos);
            for (int i = 0; i < n; i++)
            {
                // Build uniform name: "lights[0].type", "lights[0].position", etc.
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

        // TODO: (Req 9) Modify the following line such that "cameraForward" contains a vector pointing the camera forward direction
        //  HINT: See how you wrote the CameraComponent::getViewMatrix, it should help you solve this one
        glm::vec3 cameraForward = camera->getOwner()->getLocalToWorldMatrix() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        std::sort(transparentCommands.begin(), transparentCommands.end(), [cameraForward](const RenderCommand &first, const RenderCommand &second)
                  {
            //TODO: (Req 9) Finish this function
            // HINT: the following return should return true "first" should be drawn before "second". 
            return glm::dot(cameraForward, first.center) > glm::dot(cameraForward, second.center); });

        // TODO: (Req 9) Get the camera ViewProjection matrix and store it in VP
        glm::mat4 VP = camera->getProjectionMatrix(windowSize) * camera->getViewMatrix();

        // Calculate sun screen position for god rays
        if (sunEntity)
        {
            glm::vec3 sunWorldPos = sunEntity->getLocalToWorldMatrix() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            glm::vec4 sunClipPos = VP * glm::vec4(sunWorldPos, 1.0f);
            if (sunClipPos.w > 0.0f)
            {
                sunScreenPosition = glm::vec3(
                    (sunClipPos.x / sunClipPos.w) * 0.5f + 0.5f,
                    (sunClipPos.y / sunClipPos.w) * 0.5f + 0.5f,
                    sunClipPos.w
                );
                // Extract sun color from light component if available
                if (auto* lc = sunEntity->getComponent<LightComponent>())
                {
                    sunColor = lc->color;
                    sunIntensity = lc->enabled ? 1.0f : 0.0f;
                }
            }
        }

        // TODO: (Req 9) Set the OpenGL viewport using viewportStart and viewportSize
        glViewport(0, 0, windowSize.x, windowSize.y);

        // TODO: (Req 9) Set the clear color to black and the clear depth to 1
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClearDepth(1.0f);

        // TODO: (Req 9) Set the color mask to true and the depth mask to true (to ensure the glClear will affect the framebuffer)
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);

        // If there is a postprocess material, bind the framebuffer
        if (postprocessMaterial)
        {
            // TODO: (Req 11) bind the framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, postprocessFrameBuffer);
        }

        // TODO: (Req 9) Clear the color and depth buffers
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Get camera world position for view direction in lighting calculations
        glm::vec3 cameraPosition = camera->getOwner()->getLocalToWorldMatrix() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        // PHASE 3: Draw opaque objects
        // For each render command, we set up uniforms based on material type.
        // LitMaterial gets extra lighting uniforms (transform, model, normalMatrix, lights).
        for (auto &command : opaqueCommands)
        {
            command.material->setup();
            auto *sh = command.material->shader;
            sh->set("transform", VP * command.localToWorld);

            // If using LitMaterial, also upload lighting data:
            // - model matrix: transforms vertex to world space
            // - normalMatrix: transforms normals to world space (inverse-transpose)
            // - lights array: all lights in scene
            if (dynamic_cast<LitMaterial *>(command.material))
            {
                sh->set("model", command.localToWorld);
                sh->set("normalMatrix", glm::mat3(glm::transpose(glm::inverse(command.localToWorld))));
                uploadLights(sh, cameraPosition);

                // Damage flash effect - check if any light has active flash timer
                // Iterate entities to find lights with active flash
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
        // If there is a sky material, draw the sky
        if (this->skyMaterial)
        {
            // TODO: (Req 10) setup the sky material
            this->skyMaterial->setup();
            // TODO: (Req 10) Get the camera position
            // TODO: (Req 10) Create a model matrix for the sy such that it always follows the camera (sky sphere center = camera position)
            glm::mat4 skyModel = glm::translate(glm::mat4(1.0f), cameraPosition);
            // TODO: (Req 10) We want the sky to be drawn behind everything (in NDC space, z=1)
            //  We can achieve this by forcing clip-space z to equal clip-space w.
            glm::mat4 alwaysBehindTransform = glm::mat4(1.0f);
            alwaysBehindTransform[2][2] = 0.0f;
            alwaysBehindTransform[3][2] = 1.0f;
            // TODO: (Req 10) set the "transform" uniform
            this->skyMaterial->shader->set("transform", alwaysBehindTransform * VP * skyModel);
            
            if (auto* litSky = dynamic_cast<LitMaterial*>(this->skyMaterial)) {
                auto* sh = litSky->shader;
                sh->set("model", skyModel);
                sh->set("normalMatrix", glm::mat3(glm::transpose(glm::inverse(skyModel))));
                uploadLights(sh, cameraPosition);
                
                // Disable damage flash entirely for the sky
                sh->set("flashStrength", 0.0f); 
                sh->set("flashColor", glm::vec3(0.0f));
            }

            // TODO: (Req 10) draw the sky sphere
            this->skySphere->draw();
        }
        // PHASE 4: Draw transparent objects
        // Transparent objects are drawn after opaque with back-to-front sorting
        // (handled by std::sort earlier using camera distance)
        for (auto &command : transparentCommands)
        {
            command.material->setup();
            auto *sh = command.material->shader;
            sh->set("transform", VP * command.localToWorld);

            // Same LitMaterial handling as opaque objects
            if (dynamic_cast<LitMaterial *>(command.material))
            {
                sh->set("model", command.localToWorld);
                sh->set("normalMatrix", glm::mat3(glm::transpose(glm::inverse(command.localToWorld))));
                uploadLights(sh, cameraPosition);

                // Damage flash effect for transparent objects
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

        // If there is a postprocess material, apply postprocessing
        if (postprocessMaterial)
        {
            // TODO: (Req 11) Return to the default framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            // TODO: (Req 11) Setup the postprocess material and draw the fullscreen triangle
            postprocessMaterial->setup();
            postprocessMaterial->shader->set("sunScreenPos", sunScreenPosition);
            postprocessMaterial->shader->set("sunColor", sunColor);
            postprocessMaterial->shader->set("sunIntensity", sunIntensity);
            postprocessMaterial->shader->set("sunDensity", 0.3f);
            postprocessMaterial->shader->set("sunWeight", 0.02f);
            postprocessMaterial->shader->set("sunDecay", 0.96f);

            // Bind depth texture to texture unit 1 for fog effect
            glActiveTexture(GL_TEXTURE1);
            depthSampler->bind(1);
            depthTarget->bind();
            postprocessMaterial->shader->set("depthTex", 1);

            // Get camera near/far planes for linear depth calculation
            if (camera)
            {
                postprocessMaterial->shader->set("cameraNear", camera->near);
                postprocessMaterial->shader->set("cameraFar", camera->far);
            }

            // Set fog enabled state (for day/night cycle)
            postprocessMaterial->shader->set("enableFog", fogEnabled);
            postprocessMaterial->shader->set("isNight", fogEnabled);

            glBindVertexArray(postProcessVertexArray);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    void ForwardRenderer::setFogEnabled(bool enabled)
    {
        this->fogEnabled = enabled;
    }

}