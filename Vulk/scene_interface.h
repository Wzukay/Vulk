#pragma once

#include <GLFW/glfw3.h>

class VulkanRenderer;
class Input;
class SceneManager;

struct AppContext {
    GLFWwindow* window = nullptr;
    VulkanRenderer* renderer = nullptr;
    Input* input = nullptr;
    SceneManager* sceneManager = nullptr;
};

class IScene {
protected:
    AppContext& m_ctx;

public:
    explicit IScene(AppContext& ctx) : m_ctx(ctx) {}
    virtual ~IScene() = default;

    // Called once when the scene becomes active (load models, textures, spawn entities)
    virtual void OnEnter() = 0;

    // Called once when switching to another scene (free chunk memory, entities, etc.)
    virtual void OnExit() = 0;

    // Logic, physics, network, camera updates
    virtual void Update(float deltaTime) = 0;

    // Custom UI passes or scene-specific draw preparation
    virtual void Render() = 0;
};