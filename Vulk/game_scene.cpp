#include "game_scene.h"
#include "scene_manager.h"
#include "main_menu_scene.h"
#include "renderer.h"
#include "asset_manager.h"
#include "input.h"
#include "imgui.h"

extern ControlMode g_CurrentMode;

void GameplayScene::OnEnter() {
    std::cout << "[GameplayScene] Entering scene. Freezing to load assets...\n";
    glfwSetInputMode(m_ctx.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    g_AssetManager.LoadTextureFromFile("assets/textures/butterfly_albedo.dds", "butterfly");

    g_AssetManager.LoadMesh("assets/models/tree/tree_lod0.glb");
    g_AssetManager.LoadMesh("assets/models/tree/tree_lod1.glb");
    g_AssetManager.LoadMesh("assets/models/tree/tree_lod2.glb");
    g_AssetManager.LoadMesh("assets/models/tree/tree_lod3.glb");
    g_AssetManager.LoadMesh("assets/models/rock/rock.glb");
    
    InitBiomes();

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

    CameraData cam = m_ctx.input->ProcessInput(m_ctx.window);

    auto& transform = m_scene.GetRegistry().GetComponent<TransformComponent>(m_playerEntity);
    auto& physics = m_scene.GetRegistry().GetComponent<PhysicsComponent>(m_playerEntity);
    auto& playerOpt = m_scene.GetRegistry().GetComponent<PlayerComponent>(m_playerEntity);

    if (g_CurrentMode == ControlMode::Player) {
        PlayerSystem::Update(m_scene.GetRegistry(), m_ctx.window, cam.front, cam.up, deltaTime);
        cam.pos = transform.position + glm::vec3(0.0f, playerOpt.playerHeight, 0.0f);
    }
    else {
        transform.position = cam.pos - glm::vec3(0.0f, playerOpt.playerHeight, 0.0f);
        physics.velocity = glm::vec3(0.0f);
        transform.isDirty = true;
    }

    m_ctx.renderer->UpdateUniformBuffer({ cam.pos, cam.front, cam.up });
    m_chunk.Update(cam.pos, m_scene, *m_ctx.renderer);

    // --- NEW: Light Aggregation ---
    m_dayNight.Tick(deltaTime);

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

    m_ctx.renderer->UpdateScene(m_scene);
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