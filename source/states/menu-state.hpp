#pragma once

#include <application.hpp>
#include <shader/shader.hpp>
#include <texture/texture2d.hpp>
#include <texture/texture-utils.hpp>
#include <material/material.hpp>
#include <mesh/mesh.hpp>
#include <mesh/mesh-utils.hpp>
#include <ecs/world.hpp>
#include <systems/forward-renderer.hpp>
#include <systems/free-camera-controller.hpp>
#include <components/camera.hpp>
#include <asset-loader.hpp>
#include <voxel/world.hpp>
#include <components/mesh-renderer.hpp>
#include <functional>
#include <array>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <audio/audio.hpp>


struct MenuButton {
    glm::vec2 position, size;
    std::string label;
    std::function<void()> action;

    // This function returns true if the given vector v is inside the button. Otherwise, false is returned.
    // This is used to check if the mouse is hovering over the button.
    bool isInside(const glm::vec2& v) const {
        return position.x <= v.x && position.y <= v.y &&
            v.x <= position.x + size.x &&
            v.y <= position.y + size.y;
    }

    // This function returns the local to world matrix to transform a rectangle of size 1x1
    // (and whose top-left corner is at the origin) to be the button.
    glm::mat4 getLocalToWorld() const {
        return glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, 0.0f)) * 
            glm::scale(glm::mat4(1.0f), glm::vec3(size.x, size.y, 1.0f));
    }
};

class Menustate: public our::State {

    // World & renderer for the background scene
    our::World bgWorld;
    our::ForwardRenderer bgRenderer;
    our::FreeCameraControllerSystem bgCameraController;
    bool bgWorldLoaded = false;

    // Blur framebuffers (ping-pong)
    // Render scene -> sceneFBO -> blur passes -> screen
    GLuint blurFBO[2]     = {0, 0};
    GLuint blurTex[2]     = {0, 0};
    GLuint sceneFBO       = 0;   // stores rendered scene
    GLuint sceneTex       = 0;
    GLuint sceneDepthRBO  = 0;
    glm::ivec2 fbSize     = {0, 0};

    // Shaders
    our::ShaderProgram* blurShader      = nullptr;
    our::ShaderProgram* blitShader      = nullptr;   // fullscreen textured quad
    our::ShaderProgram* titleShader     = nullptr;
    our::ShaderProgram* panelShader     = nullptr;   // draw colored rectangles
    our::ShaderProgram* highlightShader = nullptr;

    // Textures 
    our::Texture2D* titleTexture = nullptr;   // "MINICRAFT" logo

    // Meshes 
    our::Mesh* quad = nullptr;   // to render UI rectange or fullscreen quad

    //  UI state 
    float time = 0.0f;
    float menuCameraYaw = 0.0f;
    std::array<MenuButton, 2> buttons;
    std::vector<our::Mesh*> bgTerrainChunkMeshes;

public:
    // Transition flags from play-state
    bool isGameOver = false;
    bool isWin = false;
private:

    our::Material* getMaterialForBlockType(int blockType) {
        switch (blockType) {
            case voxel::STONE: return our::AssetLoader<our::Material>::get("stone");
            case voxel::GRASS:
            case voxel::DIRT: return our::AssetLoader<our::Material>::get("grass");
            case voxel::SAND: return our::AssetLoader<our::Material>::get("sand");
            case voxel::WATER: return our::AssetLoader<our::Material>::get("water");
            default: return our::AssetLoader<our::Material>::get("default");
        }
    }


    void onInitialize() override {

        //  Load background world (reuse play scene config) 
        auto& app = *getApp();
        auto& cfg = app.getConfig();

        bgCameraController.enter(getApp());

        if (cfg.contains("scene")) {
            try {
                // Check if there's a world definition in the config
                if(cfg["scene"].contains("world") || cfg["scene"].contains("terrain")) {
                    // First, deserialize assets globally so the renderer can use them
                    if(cfg["scene"].contains("assets")){
                        our::deserializeAllAssets(cfg["scene"]["assets"]);
                    }
                    // Then deserialize the world
                    if(cfg["scene"].contains("world")) {
                        bgWorld.deserialize(cfg["scene"]["world"]);
                    }

                    // Generate terrain if present
                    if(cfg["scene"].contains("terrain")) {
                        voxel::World terrainWorld;
                        terrainWorld.deserialize(cfg["scene"]["terrain"]);
                        int menuChunkRadius = cfg["scene"]["terrain"].value("menu-chunk-radius", 1);
                        for (int dz = -menuChunkRadius; dz <= menuChunkRadius; ++dz) {
                            for (int dx = -menuChunkRadius; dx <= menuChunkRadius; ++dx) {
                                terrainWorld.generateChunk(dx, dz);
                            }
                        }

                        for (const auto& entry : terrainWorld.activeChunks) {
                            const auto& chunk = entry.second;

                            const int meshBlockTypes[] = {
                                voxel::STONE,
                                voxel::GRASS,
                                voxel::DIRT,
                                voxel::SAND,
                                voxel::WATER,
                                voxel::WOOD,
                                voxel::LEAF,
                                voxel::Diamond,
                                voxel::Glass
                            };

                            glm::vec3 chunkOrigin(
                                static_cast<float>(chunk.chunkX * voxel::Chunk::CHUNK_SIZE),
                                0.0f,
                                static_cast<float>(chunk.chunkZ * voxel::Chunk::CHUNK_SIZE)
                            );

                            for (int blockType : meshBlockTypes) {
                                our::Material* material = getMaterialForBlockType(blockType);
                                if (!material) continue;

                                our::Mesh* chunkMesh = our::mesh_utils::buildChunkMesh(chunk, terrainWorld, blockType);
                                if (!chunkMesh) continue;

                                our::Entity* chunkEntity = bgWorld.add();
                                chunkEntity->localTransform.position = chunkOrigin;

                                auto* meshRenderer = chunkEntity->addComponent<our::MeshRendererComponent>();
                                meshRenderer->mesh = chunkMesh;
                                meshRenderer->material = material;

                                bgTerrainChunkMeshes.push_back(chunkMesh);
                            }
                        }
                    }

                    glm::ivec2 size = app.getFrameBufferSize();
                    bgRenderer.initialize(size, app.getConfig()["scene"].value("renderer", nlohmann::json{}));
                    bgWorldLoaded = true;
                } else {
                    // No world definition in config -> just load assets for menu
                    if(cfg["scene"].contains("assets")){
                        our::deserializeAllAssets(cfg["scene"]["assets"]);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[Menu] Could not load background scene: " << e.what() << "\n";
            }
        }

        //  Create framebuffers 
        fbSize = app.getFrameBufferSize();
        createFramebuffers(fbSize);

        //  Build shaders 
        blurShader = new our::ShaderProgram();
        blurShader->attach("assets/shaders/fullscreen.vert", GL_VERTEX_SHADER);
        blurShader->attach("assets/shaders/postprocess/blur.frag", GL_FRAGMENT_SHADER);
        blurShader->link();

        blitShader = new our::ShaderProgram();
        blitShader->attach("assets/shaders/fullscreen.vert", GL_VERTEX_SHADER);
        blitShader->attach("assets/shaders/blit.frag", GL_FRAGMENT_SHADER);
        blitShader->link();

        titleShader = new our::ShaderProgram();
        titleShader->attach("assets/shaders/textured.vert", GL_VERTEX_SHADER);
        titleShader->attach("assets/shaders/postprocess/menu-title.frag", GL_FRAGMENT_SHADER);
        titleShader->link();
        panelShader = new our::ShaderProgram();
        panelShader->attach("assets/shaders/tinted.vert", GL_VERTEX_SHADER);
        panelShader->attach("assets/shaders/tinted.frag", GL_FRAGMENT_SHADER);
        panelShader->link();

        highlightShader = new our::ShaderProgram();
        highlightShader->attach("assets/shaders/tinted.vert", GL_VERTEX_SHADER);
        highlightShader->attach("assets/shaders/tinted.frag", GL_FRAGMENT_SHADER);
        highlightShader->link();

        //  Load title texture 
        titleTexture = our::texture_utils::loadImage("assets/textures/minicraft-title.png");

        //  Unit quad mesh 
        // Top-left = (0,0), bottom-right = (1,1). UV (0,1) at top-left so that
        // the orthographic projection (y-down) maps correctly.
        quad = new our::Mesh({
            {{0.0f, 0.0f, 0.0f}, {255,255,255,255}, {0.0f, 1.0f}, {0,0,1}},
            {{1.0f, 0.0f, 0.0f}, {255,255,255,255}, {1.0f, 1.0f}, {0,0,1}},
            {{1.0f, 1.0f, 0.0f}, {255,255,255,255}, {1.0f, 0.0f}, {0,0,1}},
            {{0.0f, 1.0f, 0.0f}, {255,255,255,255}, {0.0f, 0.0f}, {0,0,1}},
        },{
            0, 1, 2,  2, 3, 0,
        });

        time = 0.0f;

        //  Button layout 
        buttons[0].label  = "PLAY GAME";
        buttons[0].action = [this](){ 
            our::AudioSystem::playSound("assets/sounds/uiBotton.wav");
            getApp()->changeState("play"); 
        };

        buttons[1].label  = "EXIT";
        buttons[1].action = [this](){ getApp()->close(); };
    }

    void onDraw(double deltaTime) override {

        time += static_cast<float>(deltaTime);
        menuCameraYaw += static_cast<float>(deltaTime) * glm::radians(18.0f);  // rotates camera

        auto& app = *getApp();
        glm::ivec2 size = app.getFrameBufferSize();

        // Recreate framebuffers if window was resized
        if (size != fbSize) {
            destroyFramebuffers();
            createFramebuffers(size);
            fbSize = size;
            if (bgWorldLoaded)
                bgRenderer.initialize(size, app.getConfig().value("renderer", nlohmann::json{}));
        }

        //  Keyboard shortcuts 
        auto& kb = app.getKeyboard();
        if (kb.justPressed(GLFW_KEY_SPACE) || kb.justPressed(GLFW_KEY_ENTER)) {
            our::AudioSystem::playSound("assets/sounds/uiBotton.wav");
            app.changeState("play");
        }
        else if (kb.justPressed(GLFW_KEY_ESCAPE)) {
            app.close();
        }

        //  Mouse 
        auto& mouse = app.getMouse();
        glm::vec2 mousePos = mouse.getMousePosition();

        if (mouse.justPressed(0)) {
            for (auto& btn : buttons)
                if (btn.isInside(mousePos)) btn.action();
        }

        // 1 — render world, then copy result into sceneFBO for blurring
        if (bgWorldLoaded) {
            // Rotate the camera entity
            for (auto* entity : bgWorld.getEntities()) {
                if (entity && entity->getComponent<our::CameraComponent>()) {
                    entity->localTransform.rotation.y = menuCameraYaw;
                    entity->localTransform.rotation.x = glm::radians(-12.0f);
                    break;
                }
            }
            // The ForwardRenderer renders to its own postprocess FBO,
            // then writes the final result to framebuffer 0 (the screen).
            bgRenderer.render(&bgWorld);

            // Now blit from framebuffer 0 (where the renderer just drew)
            // into our sceneFBO so the blur pipeline can process it.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sceneFBO);
            glBlitFramebuffer(0, 0, size.x, size.y,
                              0, 0, size.x, size.y,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
            // No world —> clear sceneFBO to dark
            glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
            glViewport(0, 0, size.x, size.y);
            glClearColor(0.05f, 0.08f, 0.12f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        // 2 — blur the scene (4 ping-pong passes for a strong blur)
        bool horizontal = true;
        GLuint blurInput = sceneTex;
        const int BLUR_PASSES = 8;   // more passes = more blur

        blurShader->use();
        blurShader->set("texelSize", glm::vec2(1.0f / size.x, 1.0f / size.y));
        glDisable(GL_DEPTH_TEST);

        for (int i = 0; i < BLUR_PASSES; i++) {
            int dst = horizontal ? 0 : 1;
            glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[dst]);
            glViewport(0, 0, size.x, size.y);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, blurInput);
            blurShader->set("tex", 0);
            blurShader->set("horizontal", horizontal);
            drawFullscreenQuad();
            blurInput  = blurTex[dst];
            horizontal = !horizontal;
        }

        // 3 — composite UI on top of blurred background
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, size.x, size.y);
        glDisable(GL_DEPTH_TEST);

        // Blit the blurred background to screen
        blitShader->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blurInput);
        blitShader->set("tex", 0);
        drawFullscreenQuad();

        // Dark vignette overlay (deepens the edges)
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Orthographic projection for all 2D UI elements
        glm::mat4 VP = glm::ortho(0.0f, (float)size.x, (float)size.y, 0.0f, 1.0f, -1.0f);

        // Fade-in alpha
        float fadeAlpha = glm::smoothstep(0.0f, 1.8f, time);

        //  Full-screen dim overlay (semi-transparent dark layer) 
        drawPanel(VP,
            glm::vec2(0, 0),
            glm::vec2(size),
            glm::vec4(0.0f, 0.0f, 0.0f, 0.45f * fadeAlpha));

        // Title Area
        // Centered horizontally, 20% from top
        float titleW = size.x * 0.55f;
        float titleH = titleW * 0.22f;   // aspect ratio of the title image
        float titleX = (size.x - titleW) * 0.5f;
        float titleY = size.y * 0.10f;

        if (titleTexture) {
            titleShader->use();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, titleTexture->getOpenGLName());
            titleShader->set("tex", 0);

            glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(titleX, titleY, 0.0f)) *
                          glm::scale(glm::mat4(1.0f), glm::vec3(titleW, titleH, 1.0f));
            titleShader->set("transform", VP * M);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            quad->draw();
        } else {
            // if texture not found -> draw a placeholder rectangle
            drawPanel(VP, glm::vec2(titleX, titleY), glm::vec2(titleW, titleH),
                      glm::vec4(0.15f, 0.13f, 0.10f, 0.9f * fadeAlpha));
        }

        // Welcome panel
        // Dark semi-transparent box, centered, below the title
        float panelW = size.x * 0.60f;
        float panelH = size.y * 0.14f;
        float panelX = (size.x - panelW) * 0.5f;
        float panelY = size.y * 0.42f;

        // Panel background
        drawPanel(VP,
            glm::vec2(panelX, panelY),
            glm::vec2(panelW, panelH),
            glm::vec4(0.0f, 0.0f, 0.0f, 0.82f * fadeAlpha));

        float border = 2.0f; // thickness of the border

        glm::vec4 borderColor = glm::vec4(0.8f, 0.8f, 0.8f, fadeAlpha * 0.3f);

        // Top
        drawPanel(VP,
            glm::vec2(panelX, panelY),
            glm::vec2(panelW, border),
            borderColor);

        // Bottom
        drawPanel(VP,
            glm::vec2(panelX, panelY + panelH - border),
            glm::vec2(panelW, border),
            borderColor);

        // Left
        drawPanel(VP,
            glm::vec2(panelX, panelY),
            glm::vec2(border, panelH),
            borderColor);

        // Right
        drawPanel(VP,
            glm::vec2(panelX + panelW - border, panelY),
            glm::vec2(border, panelH),
            borderColor);

        // Buttons
        float btnW   = size.x * 0.28f;
        float btnH   = size.y * 0.075f;
        float btnGap = size.x * 0.04f;
        float totalBtnW = btnW * 2 + btnGap;
        float btnStartX = (size.x - totalBtnW) * 0.5f;
        float btnY   = size.y * 0.62f;

        // Lay out the two buttons side-by-side
        buttons[0].position = { btnStartX, btnY };
        buttons[0].size     = { btnW, btnH };
        buttons[1].position = { btnStartX + btnW + btnGap, btnY };
        buttons[1].size     = { btnW, btnH };

        for (auto& btn : buttons) {
            bool hovered = btn.isInside(mousePos);

            // Outer outline
            glm::vec4 outlineColor = hovered 
                ? glm::vec4(1.0f, 1.0f, 1.0f, fadeAlpha) // white border when hovered
                : glm::vec4(0.0f, 0.0f, 0.0f, fadeAlpha); // black border normally

            drawPanel(VP, btn.position - glm::vec2(2.0f, 2.0f), btn.size + glm::vec2(4.0f, 4.0f), outlineColor);

            // Button body
            glm::vec4 btnColor = hovered
                ? glm::vec4(0.45f, 0.45f, 0.45f, 1.0f * fadeAlpha) 
                : glm::vec4(0.43f, 0.43f, 0.43f, 1.0f * fadeAlpha); 

            drawPanel(VP, btn.position, btn.size, btnColor);

            // Button top highlight edge
            drawPanel(VP,
                btn.position,
                glm::vec2(btn.size.x, 3.0f),
                glm::vec4(0.9f, 0.9f, 0.9f, fadeAlpha * 0.7f));

            // Button bottom shadow edge
            drawPanel(VP,
                btn.position + glm::vec2(0, btn.size.y - 3.0f),
                glm::vec2(btn.size.x, 3.0f),
                glm::vec4(0.1f, 0.1f, 0.1f, fadeAlpha * 0.8f));

            // Button left edge
            drawPanel(VP,
                btn.position,
                glm::vec2(3.0f, btn.size.y - 3.0f),
                glm::vec4(0.9f, 0.9f, 0.9f, fadeAlpha * 0.7f));

            // Button right edge
            drawPanel(VP,
                btn.position + glm::vec2(btn.size.x - 3.0f, 0),
                glm::vec2(3.0f, btn.size.y),
                glm::vec4(0.1f, 0.1f, 0.1f, fadeAlpha * 0.8f));
        }

        glDisable(GL_BLEND);
    }

    void onImmediateGui() override {
        auto& app = *getApp();
        glm::ivec2 size = app.getFrameBufferSize();
        glm::vec2 mousePos = app.getMouse().getMousePosition();

        float fadeAlpha = glm::smoothstep(0.0f, 1.8f, time);
        float panelW = size.x * 0.60f;
        float panelH = size.y * 0.14f;
        float panelX = (size.x - panelW) * 0.5f;
        float panelY = size.y * 0.42f;

        renderImGuiOverlay(size, panelX, panelY, panelW, panelH, fadeAlpha, mousePos);
    }

    void onDestroy() override {
        destroyFramebuffers();

        delete blurShader;
        delete blitShader;
        delete titleShader;
        delete panelShader;
        delete highlightShader;

        if (titleTexture) delete titleTexture;
        delete quad;

        for (auto* mesh : bgTerrainChunkMeshes) {
            delete mesh;
        }
        bgTerrainChunkMeshes.clear();

        if (bgWorldLoaded) {
            bgRenderer.destroy();
            bgWorld.clear();
            bgCameraController.exit();
        }
    }

    // ImGui text overlay
    void renderImGuiOverlay(glm::ivec2 size,
                            float panelX, float panelY,
                            float panelW, float panelH,
                            float fadeAlpha,
                            glm::vec2 mousePos)
    {
        // I use a full-screen transparent ImGui window as a canvas
        // so I can position text anywhere without ImGui's layout overhead.
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)size.x, (float)size.y));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoInputs     |
            ImGuiWindowFlags_NoNav        |
            ImGuiWindowFlags_NoMove       |
            ImGuiWindowFlags_NoSavedSettings;

        ImGui::Begin("##menuoverlay", nullptr, flags);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Fade colour helper
        auto fadeColor = [&](ImVec4 c) -> ImU32 {
            c.w *= fadeAlpha;
            return ImGui::ColorConvertFloat4ToU32(c);
        };

        // Welcome text inside the panel 
        {
            const char* line1 = "Welcome to MiniCraft";
            const char* line2 = "We hope you enjoy playing our game";
            ImVec4 titleColor = {0.95f, 0.95f, 0.95f, 1.0f};

            if (isGameOver) {
                line1 = "GAME OVER";
                line2 = "You have perished. Try again!";
                titleColor = {1.0f, 0.2f, 0.2f, 1.0f};
            } else if (isWin) {
                line1 = "YOU WIN!";
                line2 = "You have survived and conquered the world!";
                titleColor = {1.0f, 0.84f, 0.0f, 1.0f};
            }

            float textScale = size.x / 1280.0f;  // scale with window width
            ImGui::SetWindowFontScale(textScale * 1.7f);

            ImVec2 sz1 = ImGui::CalcTextSize(line1);
            ImVec2 sz2 = ImGui::CalcTextSize(line2);

            float lineH   = sz1.y + sz2.y + 10.0f;
            float y1 = panelY + (panelH - lineH) * 0.5f;
            float y2 = y1 + sz1.y + 10.0f;

            // Line 1
            float x1 = panelX + (panelW - sz1.x) * 0.5f;
            float textShadow = textScale * 3.0f;
            dl->AddText(ImVec2(x1 + textShadow, y1 + textShadow), fadeColor({0.35f, 0.35f, 0.35f, 1.0f}), line1); // shadow
            dl->AddText(ImVec2(x1,     y1),     fadeColor(titleColor), line1);

            // Line 2
            float x2 = panelX + (panelW - sz2.x) * 0.5f;
            dl->AddText(ImVec2(x2 + textShadow, y2 + textShadow), fadeColor({0.35f, 0.35f, 0.35f, 1.0f}), line2); // shadow
            dl->AddText(ImVec2(x2,     y2),     fadeColor({0.90f, 0.90f, 0.90f, 1.0f}), line2);

            ImGui::SetWindowFontScale(1.0f);
        }

        // Button labels
        {
            float textScale = size.x / 1280.0f;
            ImGui::SetWindowFontScale(textScale * 1.35f);

            for (auto& btn : buttons) {
                bool hovered = btn.isInside(mousePos);
                ImVec4 txtColor = hovered
                    ? ImVec4(1.0f, 1.0f, 0.4f, 1.0f)   // yellow when hovered
                    : ImVec4(0.9f, 0.9f, 0.9f, 1.0f);  // white normally

                ImVec2 labelSize = ImGui::CalcTextSize(btn.label.c_str());
                float  lx = btn.position.x + (btn.size.x - labelSize.x) * 0.5f;
                float  ly = btn.position.y + (btn.size.y - labelSize.y) * 0.5f;

                // Drop shadow
                float btnShadow = textScale * 2.5f;
                dl->AddText(ImVec2(lx + btnShadow, ly + btnShadow),
                    fadeColor({0.15f, 0.15f, 0.15f, 1.0f}),
                    btn.label.c_str());
                // Main text
                dl->AddText(ImVec2(lx, ly),
                    fadeColor(txtColor),
                    btn.label.c_str());
            }
            ImGui::SetWindowFontScale(1.0f);
        }

        ImGui::End();
    }


    // Draw a solid-colour rectangle using the panel (tinted) shader
    void drawPanel(const glm::mat4& VP,
                   glm::vec2 pos, glm::vec2 sz, glm::vec4 color)
    {
        glm::mat4 M = glm::translate(glm::mat4(1.0f), glm::vec3(pos.x, pos.y, 0.0f)) *
                      glm::scale(glm::mat4(1.0f), glm::vec3(sz.x, sz.y, 1.0f));
        panelShader->use();
        panelShader->set("transform", VP * M);
        panelShader->set("tint", color);
        quad->draw();
    }

    // Draw a fullscreen NDC quad (for post-process)
    void drawFullscreenQuad() {
        // Use the engine's existing fullscreen VAO trick:
        // Render a single triangle that covers NDC, letting the vertex shader
        // generate the coordinates. The "fullscreen.vert" already does this.
        // We just need to call draw with 3 vertices / no VBO.
        static GLuint fsVAO = 0;
        if (fsVAO == 0) glGenVertexArrays(1, &fsVAO);
        glBindVertexArray(fsVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    // Framebuffer management
    void createFramebuffers(glm::ivec2 sz) {
        // Scene FBO (colour + depth)
        glGenFramebuffers(1, &sceneFBO);
        glBindFramebuffer(GL_FRAMEBUFFER, sceneFBO);

        glGenTextures(1, &sceneTex);
        glBindTexture(GL_TEXTURE_2D, sceneTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sz.x, sz.y, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTex, 0);

        glGenRenderbuffers(1, &sceneDepthRBO);
        glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, sz.x, sz.y);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRBO);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "[Menu] Scene FBO is not complete!\n";
        } else {
            std::cout << "[Menu] Scene FBO created successfully\n";
        }

        // Two ping-pong blur FBOs (colour only)
        glGenFramebuffers(2, blurFBO);
        glGenTextures(2, blurTex);
        for (int i = 0; i < 2; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, blurFBO[i]);
            glBindTexture(GL_TEXTURE_2D, blurTex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sz.x, sz.y, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, blurTex[i], 0);
            
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "[Menu] Blur FBO " << i << " is not complete!\n";
            } else {
                std::cout << "[Menu] Blur FBO " << i << " created successfully\n";
            }
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void destroyFramebuffers() {
        if (sceneFBO)     { glDeleteFramebuffers(1, &sceneFBO);     sceneFBO = 0; }
        if (sceneTex)     { glDeleteTextures(1, &sceneTex);         sceneTex = 0; }
        if (sceneDepthRBO){ glDeleteRenderbuffers(1, &sceneDepthRBO); sceneDepthRBO = 0; }
        if (blurFBO[0])   { glDeleteFramebuffers(2, blurFBO);   blurFBO[0] = blurFBO[1] = 0; }
        if (blurTex[0])   { glDeleteTextures(2, blurTex);       blurTex[0] = blurTex[1] = 0; }
    }
};