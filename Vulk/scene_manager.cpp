#include "scene_manager.h"
#include <iostream>

void SceneManager::Init(GLFWwindow* window, VulkanRenderer* renderer, Input* input) {
    m_ctx.window = window;
    m_ctx.renderer = renderer;
    m_ctx.input = input;
    m_ctx.sceneManager = this;
}

void SceneManager::ApplyPendingScene() {
    if (!m_pendingScene) return;

    if (m_currentScene) {
        m_currentScene->OnExit();
    }

    m_currentScene = std::move(m_pendingScene);
    m_currentScene->OnEnter();
}

void SceneManager::Update(float deltaTime) {
    ApplyPendingScene();

    if (m_currentScene) {
        m_currentScene->Update(deltaTime);
    }
}

void SceneManager::Render() {
    if (m_currentScene) {
        m_currentScene->Render();
    }
}

void SceneManager::Shutdown() {
    if (m_currentScene) {
        m_currentScene->OnExit();
        m_currentScene.reset();
    }
    m_pendingScene.reset();
}