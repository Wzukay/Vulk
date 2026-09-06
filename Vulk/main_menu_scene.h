#pragma once

#include "scene_interface.h"
#include "scene_manager.h"
#include "scene.h"
#include "imgui.h"
#include "game_scene.h"

class MainMenuScene : public IScene {
private:
    Scene m_emptyWorld;

public:
    using IScene::IScene;

    void OnEnter() override {
        // Unlock cursor for menu interaction
        glfwSetInputMode(m_ctx.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        // Tell renderer there are no 3D world meshes right now
        m_emptyWorld.Clear();
        m_ctx.renderer->UpdateScene(m_emptyWorld);
    }

    void OnExit() override {
        // Leaving menu
    }

    void Update(float deltaTime) override {
        // Update menu background animations or music here if needed
    }

    void Render() override {
        // Render UI using ImGui
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("Vulk Engine");
            ImGui::Separator();

            if (ImGui::Button("Start Game", ImVec2(-1, 40))) {
                m_ctx.sceneManager->ChangeScene<GameplayScene>();
            }

            if (ImGui::Button("Exit", ImVec2(-1, 40))) {
                glfwSetWindowShouldClose(m_ctx.window, GLFW_TRUE);
            }
        }
        ImGui::End();
    }
};