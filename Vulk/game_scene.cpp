#include "game_scene.h"
#include "scene_manager.h"
#include "main_menu_scene.h"
#include "renderer.h"
#include "asset_manager.h"
#include "input.h"
#include "imgui.h"
#include "audio.h"

extern ControlMode g_CurrentMode;

void GameplayScene::OnEnter() {
    std::cout << "[GameplayScene] Entering scene. Freezing to load assets...\n";
    glfwSetInputMode(m_ctx.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    g_AssetManager.LoadSound("assets/sounds/hello.mp3", "hello_sfx");

    g_AssetManager.LoadTextureFromFile("assets/textures/butterfly_albedo.dds", "butterfly");

    g_AssetManager.AutoLoadLodGroup("assets/models/tree/tree_lod0.glb", "TreeGroup");
    g_AssetManager.AutoLoadLodGroup("assets/models/rock/rock_lod0.glb", "RockGroup");
    g_AssetManager.AutoLoadLodGroup("assets/models/tent/tent_lod0.glb", "TentGroup");
    g_AssetManager.AutoLoadLodGroup("assets/models/campfire/campfire_lod0.glb", "CampfireGroup");

    m_chunk.Init(*m_ctx.renderer);
    m_chunk.SetRandomSeed();

    CameraData initialCam = {
        glm::vec3(100.0f, 60.0f, 250.0f),
        glm::normalize(glm::vec3(0.0f, sin(glm::radians(-30.0f)), -cos(glm::radians(-30.0f)))),
        glm::vec3(0.0f, 1.0f, 0.0f)
    };
    m_ctx.renderer->UpdateUniformBuffer(initialCam);
    m_chunk.Update(initialCam.pos, m_scene, *m_ctx.renderer);

    m_playerEntity = m_scene.GetRegistry().CreateEntity();

    TransformComponent pTransform;
    pTransform.position = glm::vec3(100.0f, 60.0f, 250.0f);
    m_scene.GetRegistry().AddComponent<TransformComponent>(m_playerEntity, pTransform);

    PhysicsComponent pPhysics;
    m_scene.GetRegistry().AddComponent<PhysicsComponent>(m_playerEntity, pPhysics);

    PlayerComponent pPlayer;
    pPlayer.minSlopeDot = std::cos(glm::radians(pPlayer.maxSlopeAngle));
    m_scene.GetRegistry().AddComponent<PlayerComponent>(m_playerEntity, pPlayer);

    LightComponent pLight;
    pLight.color = glm::vec3(1.0f, 0.65f, 0.2f); // Warm fire light
    pLight.intensity = 1.0f;
    pLight.range = 35.0f;
    m_scene.GetRegistry().AddComponent<LightComponent>(m_playerEntity, pLight);

    m_ctx.renderer->UpdateScene(m_scene);
}
void GameplayScene::OnExit() {
    // Clean up world resources when quitting back to menu
    g_AudioEngine.Cleanup();
    m_chunk.Shutdown();
    m_scene.Clear();
    m_ctx.renderer->ClearHeightCache();
}

void GameplayScene::Update(float deltaTime) {
    static bool escPressedLast = false;
    bool escPressed = glfwGetKey(m_ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (escPressed && !escPressedLast) {
        m_isPaused = !m_isPaused;
        glfwSetInputMode(m_ctx.window, GLFW_CURSOR, m_isPaused ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
    }
    escPressedLast = escPressed;

    if (m_isPaused) return;

    // 1. Get raw camera data from Input (This holds the NoClip position!)
    CameraData cam = m_ctx.input->ProcessInput(m_ctx.window);

    g_AudioEngine.Tick();
    g_AudioEngine.UpdateListener(cam.pos, cam.front, cam.up);

    auto& transform = m_scene.GetRegistry().GetComponent<TransformComponent>(m_playerEntity);
    auto& physics = m_scene.GetRegistry().GetComponent<PhysicsComponent>(m_playerEntity);
    auto& playerOpt = m_scene.GetRegistry().GetComponent<PlayerComponent>(m_playerEntity);

    // 2. TELEPORT LOGIC
    static ControlMode lastMode = g_CurrentMode;
    if (lastMode != g_CurrentMode) {
        if (g_CurrentMode == ControlMode::Player) {
            // NoClip -> Player: Drop the physical body exactly where the camera is
            transform.position = cam.pos - glm::vec3(0.0f, playerOpt.playerHeight, 0.0f);

            // Zero out velocity so you don't inherit old falling momentum
            physics.velocity = glm::vec3(0.0f);
            transform.isDirty = true;
        }
        else if (g_CurrentMode == ControlMode::NoClip) {
            // Player -> NoClip: Snap the camera to exactly where the player's eyes are
            glm::vec3 eyePos = transform.position + glm::vec3(0.0f, playerOpt.playerHeight, 0.0f);

            m_ctx.input->SetCameraPosition(eyePos);
            cam.pos = eyePos;
        }
        lastMode = g_CurrentMode;
    }

    // 3. Process WASD Intentions
    if (g_CurrentMode == ControlMode::Player) {
        Player::Update(m_scene.GetRegistry(), m_playerEntity, m_ctx.window, cam.front, cam.up, deltaTime);
    }
    else {
        physics.velocity.x = 0.0f;
        physics.velocity.z = 0.0f;
    }

    // 4. Run Physics FIRST so gravity pulls the body down
    PhysicsSystem::Update(m_scene.GetRegistry(), deltaTime);

    // 5. Sync cameras AFTER gravity is applied
    if (g_CurrentMode == ControlMode::Player) {
        // The body has moved/fallen. Calculate the new eye position.
        glm::vec3 eyePos = transform.position + glm::vec3(0.0f, playerOpt.playerHeight, 0.0f);

        // Force the Input system's internal ghost camera to fall with us
        m_ctx.input->SetCameraPosition(eyePos);

        // Lock the visual renderer camera to the post-gravity body
        cam.pos = eyePos;
    }

    // 6. Update rendering
    m_ctx.renderer->UpdateUniformBuffer({ cam.pos, cam.front, cam.up });
    m_chunk.Update(cam.pos, m_scene, *m_ctx.renderer);

    m_dayNight.Tick(deltaTime);

    m_ctx.renderer->SetSkyParams(m_dayNight.zenithColor, m_dayNight.horizonColor, m_dayNight.starFade, m_dayNight.cloudTime, m_dayNight.coverage);

    std::vector<Light> activeLights;
    activeLights.push_back(m_dayNight.sunLight);
    if (m_dayNight.moonLight.has_value()) {
        activeLights.push_back(m_dayNight.moonLight.value());
    }

    auto lightGroup = m_scene.GetRegistry().QueryGroup<TransformComponent, LightComponent>();
    for (Entity e : lightGroup) {
        auto& t = m_scene.GetRegistry().GetComponent<TransformComponent>(e);
        auto& l = m_scene.GetRegistry().GetComponent<LightComponent>(e);
        // Elevate the light slightly so it isn't clipping through the floor
        activeLights.push_back(Light::Point(t.position + glm::vec3(0.0f, 2.0f, 0.0f), l.color, l.intensity, l.range));
    }
    m_scene.SetLights(activeLights);

    if (m_scene.NeedsStaticUpdate() || m_scene.HasModifiedLights()) {
        m_ctx.renderer->UpdateScene(m_scene);
        m_scene.ClearModifiedLightsFlag();
    }
}

void GameplayScene::Render() {
    if (m_isPaused) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f - 100, ImGui::GetIO().DisplaySize.y * 0.5f - 80), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(200, 160), ImGuiCond_Always);

        if (ImGui::Begin("Paused", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove)) {
            if (ImGui::Button("Resume", ImVec2(-1, 30))) {
                m_isPaused = false;
                glfwSetInputMode(m_ctx.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            if (ImGui::Button("Return to Menu", ImVec2(-1, 30))) {
                m_ctx.sceneManager->ChangeScene<MainMenuScene>();
            }
        }
        ImGui::End();
    }
}