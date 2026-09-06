#include "game.h"
#include "main_menu_scene.h"

void Game::Init() {
    g_Settings.LoadFromFile();
    renderer.Initialize(g_Settings.windowWidth, g_Settings.windowHeight, "Vulk");
    window = renderer.GetWindow();

    glfwSetWindowUserPointer(window, &input);
    glfwSetCursorPosCallback(window, [](GLFWwindow* window, double xpos, double ypos) {
        ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos); // Let ImGui see the mouse!

        Input* inputPtr = reinterpret_cast<Input*>(glfwGetWindowUserPointer(window));
        if (inputPtr) inputPtr->ProcessMouse(window, xpos, ypos);
        });

    // Chain the Mouse Button Callback
    glfwSetMouseButtonCallback(window, [](GLFWwindow* window, int button, int action, int mods) {
        ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods); // Let ImGui click!
        });

    // Initialize the Scene Manager and boot into the Main Menu
    sceneManager.Init(window, &renderer, &input);
    sceneManager.ChangeScene<MainMenuScene>();

    isRunning = true;
}

void Game::Loop() {
    double lastFrameTime = glfwGetTime();

    while (!renderer.ShouldClose() && isRunning) {
        auto frameStartTime = std::chrono::high_resolution_clock::now();

        double currentFrameTime = glfwGetTime();
        float deltaTime = std::min(static_cast<float>(currentFrameTime - lastFrameTime), 0.1f);
        lastFrameTime = currentFrameTime;

        renderer.PollEvents();

        // 1. Start the UI Frame
        renderer.BeginUI();

        // 2. Update active scene (logic, physics, chunk streaming)
        sceneManager.Update(deltaTime);

        // 3. Render active scene UI (Main Menu, Pause Screen, etc.)
        sceneManager.Render();

        // 4. End the UI Frame (Draws global stats & finalizes ImGui)
        renderer.EndUI();

        // 5. Present the 3D Vulkan frame + UI
        renderer.DrawFrame();

        // Framerate limiter
        if (g_Settings.frameCap > 0 && !g_Settings.vsync) {
            double targetMs = 1000.0 / g_Settings.frameCap;
            while (true) {
                auto currentTime = std::chrono::high_resolution_clock::now();
                double elapsedMs = std::chrono::duration<double, std::milli>(currentTime - frameStartTime).count();
                if (elapsedMs >= targetMs) break;
                if (targetMs - elapsedMs > 1.0) std::this_thread::yield();
            }
        }
    }

    sceneManager.Shutdown();
    g_Settings.SaveToFile();
    renderer.Cleanup();
}