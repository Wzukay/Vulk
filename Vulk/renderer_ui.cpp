#include "renderer_ui.h"
#include "settings.h"
#include "asset_manager.h"
#include <stdexcept>

void UIRenderer::CreateDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[2] = {};
    pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    pool_sizes[0].descriptorCount = 100;
    pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    pool_sizes[1].descriptorCount = 100;

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 0;
    for (VkDescriptorPoolSize& size : pool_sizes) {
        pool_info.maxSets += size.descriptorCount;
    }
    pool_info.poolSizeCount = 2;
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imguiPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool.");
    }
}

void UIRenderer::Init(GLFWwindow* window, VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t queueFamily, VkQueue graphicsQueue, uint32_t imageCount, VkRenderPass renderPass) {
    m_device = device;
    CreateDescriptorPool();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = m_device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_imguiPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.Allocator = nullptr;
    init_info.UseDynamicRendering = false;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);

    std::cout << "[UI] Initialized\n";
}

void UIRenderer::Recreate(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t queueFamily, VkQueue graphicsQueue, uint32_t imageCount, VkRenderPass renderPass) {
    ImGui_ImplVulkan_Shutdown();
    vkResetDescriptorPool(m_device, m_imguiPool, 0);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = m_device;
    init_info.QueueFamily = queueFamily;
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_imguiPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = imageCount;
    init_info.Allocator = nullptr;
    init_info.UseDynamicRendering = false;
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);
}

void UIRenderer::Cleanup() {
    if (m_device == VK_NULL_HANDLE) return;

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (m_imguiPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_imguiPool, nullptr);
        m_imguiPool = VK_NULL_HANDLE;
    }
    m_device = VK_NULL_HANDLE;
}

void UIRenderer::BeginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void UIRenderer::DrawDebugStats(uint32_t sceneTotalIndices, uint32_t sceneTotalVertices, uint32_t drawCallCount, uint32_t culledCount) {
    uint32_t triangleCount = sceneTotalIndices / 3;
    uint32_t texturesLoaded = static_cast<uint32_t>(g_AssetManager.GetTextureRegistry().size());

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowPos(ImVec2(1280 - 240, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.35f);

    ImGui::Begin("Stats", nullptr, windowFlags);

    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "PERFORMANCE");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Ms/Frame: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "PACING & SCALING");
    if (g_Settings.frameCap == 0) ImGui::Text("Frame Cap: Uncapped");
    else ImGui::Text("Frame Cap: %d FPS", g_Settings.frameCap);

    if (g_Settings.enableDRS) {
        ImGui::Text("DRS: Active (Target %d FPS)", g_Settings.targetFPS);
        ImGui::Text("Active Scale: %.0f%%", g_Settings.renderScale * 100.0f);
    }
    else {
        ImGui::Text("DRS: Disabled");
        ImGui::Text("Static Scale: %.0f%%", g_Settings.renderScale * 100.0f);
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "GEOMETRY");
    ImGui::Text("Triangles: %u", triangleCount);
    ImGui::Text("Vertices:  %u", sceneTotalVertices);
    ImGui::Text("Indices:   %u", sceneTotalIndices);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "PIPELINE");
    ImGui::Text("Draw Calls: %-8u", drawCallCount);
    ImGui::Text("Culled:     %-8u", culledCount);
    ImGui::Text("Textures:   %-8u", texturesLoaded);

    ImGui::End();
}

void UIRenderer::DrawPlayerHUD(float currentHealthPercentage) {
    ImGuiWindowFlags hudFlags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoInputs;

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // 1. Health Bar (Bottom Center)
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y - 40.0f),
        ImGuiCond_Always, ImVec2(0.5f, 1.0f)
    );

    ImGui::Begin("InGameHUD", nullptr, hudFlags);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();

    float barWidth = 350.0f;
    float barHeight = 18.0f;

    drawList->AddRectFilled(p, ImVec2(p.x + barWidth, p.y + barHeight), IM_COL32(30, 30, 30, 200));
    drawList->AddRectFilled(p, ImVec2(p.x + (barWidth * currentHealthPercentage), p.y + barHeight), IM_COL32(40, 200, 60, 255));
    drawList->AddRect(p, ImVec2(p.x + barWidth, p.y + barHeight), IM_COL32(255, 255, 255, 255), 0.0f, 0, 1.5f);

    ImGui::Dummy(ImVec2(barWidth, barHeight));
    ImGui::End();

    // 2. Crosshair (Absolute Center)
    ImVec2 center = ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
        viewport->WorkPos.y + viewport->WorkSize.y * 0.5f);

    ImDrawList* bgDrawList = ImGui::GetBackgroundDrawList();
    ImU32 crosshairColor = IM_COL32(0, 255, 0, 255);
    float thickness = 2.0f;
    float gap = 5.0f;
    float length = 12.0f;

    //bgDrawList->AddLine(ImVec2(center.x - gap - length, center.y), ImVec2(center.x - gap, center.y), crosshairColor, thickness);
    //bgDrawList->AddLine(ImVec2(center.x + gap, center.y), ImVec2(center.x + gap + length, center.y), crosshairColor, thickness);
    //bgDrawList->AddLine(ImVec2(center.x, center.y - gap - length), ImVec2(center.x, center.y - gap), crosshairColor, thickness);
    //bgDrawList->AddLine(ImVec2(center.x, center.y + gap), ImVec2(center.x, center.y + gap + length), crosshairColor, thickness);
    bgDrawList->AddCircle(ImVec2(center.x, center.y), gap / 2.0f, crosshairColor, 32);
}

void UIRenderer::EndFrame() {
    ImGui::Render();
}

void UIRenderer::RecordCommands(VkCommandBuffer commandBuffer) {
    if (ImGui::GetDrawData() != nullptr) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
    }
}