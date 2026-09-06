#pragma once

#include <memory>
#include "scene_interface.h"

class SceneManager {
private:
    AppContext m_ctx;
    std::unique_ptr<IScene> m_currentScene = nullptr;
    std::unique_ptr<IScene> m_pendingScene = nullptr;

    void ApplyPendingScene();

public:
    void Init(GLFWwindow* window, VulkanRenderer* renderer, Input* input);

    template<typename T, typename... Args>
    void ChangeScene(Args&&... args) {
        m_pendingScene = std::make_unique<T>(m_ctx, std::forward<Args>(args)...);
    }

    void Update(float deltaTime);
    void Render();
    void Shutdown();

    AppContext& GetContext() { return m_ctx; }
};