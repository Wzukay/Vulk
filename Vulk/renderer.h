#pragma once

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include <unordered_set>
#include <mutex>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_core.h>
#include <fstream>
#include <random>

#include "mesh.h"

#include "gpu_instances.h"
#include "gpu_async.h"

#include "scene.h"
#include "scene_types.h"

#include "settings.h"
#include "asset_manager.h"

#include "renderer_skybox.h"
#include "renderer_water.h"
#include "renderer_grass.h"
#include "renderer_terrain.h"
#include "renderer_static.h"
#include "renderer_boid.h"

#include "ring_buffer_uploader.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

struct CameraData;
class Scene;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class VulkanRenderer {
private:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 3;
    static constexpr uint32_t MAX_LIGHTS = 256;

private:
    int windowedPosX = 0, windowedPosY = 0;
    int windowedWidth = 1280, windowedHeight = 720;

    bool m_isShuttingDown = false;

    GLFWwindow* window = nullptr;

    RingBufferUploader m_uploader;
    const Scene* currentScene = nullptr;
    AssetManager* assetManager = &g_AssetManager;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice logicalDevice = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    void InitWindow(int width, int height, const std::string& title);
    void InitVulkan();

    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();

    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
    bool IsDeviceSuitable(VkPhysicalDevice device);
    std::vector<const char*> GetRequiredExtensions();

    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

public:
    void Initialize(int width, int height, const std::string& title);
    void Cleanup();
    bool ShouldClose();
    void PollEvents();
    void UpdateScene(const Scene& scene);

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    VkDevice GetLogicalDevice() const { return logicalDevice; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
    VkCommandPool GetCommandPool() const { return commandPool; }
    VkQueue GetGraphicsQueue() const { return graphicsQueue; }
    GLFWwindow* GetWindow() const { return window; }

private:
    uint32_t drawCallCount = 0, culledCount = 0, sceneTotalVertices = 0, sceneTotalIndices = 0;
    uint64_t m_globalFrameCounter = 0;
    size_t currentFrame = 0;

    VkSampleCountFlagBits m_currentMsaaSamples = VK_SAMPLE_COUNT_4_BIT;

    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;

    // --- Offscreen 3D Targets ---
    VkImage offscreenResolveImage = VK_NULL_HANDLE;
    VkDeviceMemory offscreenResolveImageMemory = VK_NULL_HANDLE;
    VkImageView offscreenResolveImageView = VK_NULL_HANDLE;
    VkSampler offscreenSampler = VK_NULL_HANDLE;

    // --- Composition Pipeline (Native Res UI) ---
    VkPipeline compositionPipeline = VK_NULL_HANDLE;
    VkPipelineLayout compositionPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositionDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool compositionDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet compositionDescriptorSet = VK_NULL_HANDLE;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    DeferredQueue<BufferDeletion> m_pendingDeletionsGlobal;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

    uint32_t GetInternalWidth() const {
        uint32_t w = std::max(1u, static_cast<uint32_t>(swapChainExtent.width * g_Settings.renderScale));
        return w + (w % 2);
    }
    uint32_t GetInternalHeight() const {
        uint32_t h = std::max(1u, static_cast<uint32_t>(swapChainExtent.height * g_Settings.renderScale));
        return h + (h % 2);
    }

    uint32_t GetSSAOWidth() const { return std::max(1u, GetInternalWidth() / 2); }
    uint32_t GetSSAOHeight() const { return std::max(1u, GetInternalHeight() / 2); }

    void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageAspectFlags aspect);

    void CreateSwapChain();
    void RecreateSwapChain();

    void CreateOffscreenResolve();
    void CreateCompositionPipeline();

    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
    void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels,
        VkSampleCountFlagBits numSamples, VkFormat format,
        VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image,
        VkDeviceMemory& imageMemory);
    void CreateImageViews();

    void CreateDepthResources();
    void CreateColorResources();

    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    VkSampleCountFlagBits GetMaxUsableSampleCount();
    VkSampleCountFlagBits IntToSampleCount(int samples);
    VkFormat FindDepthFormat();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    void GenerateSSAOResources();

public:
    void DrawFrame();

private:
    Settings currentSettings;

    void RecreateGraphicsPipeline();
    void ToggleFullscreen();
public:
    void ApplySettings();

private:
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    
    VkPipeline terrainPipeline = VK_NULL_HANDLE;
    VkPipeline staticPipeline = VK_NULL_HANDLE;
    VkPipeline instancedPipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;

    void* uniformBufferMapped = nullptr;

    std::unordered_map<uint64_t, uint32_t> m_memoryTypeCache;

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSet();
    void CreateGraphicsPipeline();
    void CreateUniformBuffer();

public:
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory);

    void DeferBufferDeletion(BufferDeletion&& del);

    static std::vector<char> ReadFile(const std::string& filename);
    static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code);

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    uint64_t GetFrameCounter() const { return m_globalFrameCounter; }

    void UpdateUniformBuffer(const CameraData& cam);
    void UpdateTextureDescriptors(const Scene& scene);

private:
    VkBuffer lightBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lightBufferMemory = VK_NULL_HANDLE;
    std::vector<Light> currentLights;

    void* lightBufferMapped = nullptr;

    void CreateLightBuffer();

public:
    void SetLights(const std::vector<Light>& lights);

private:
    StaticMeshRenderer m_staticMeshRenderer;
public:


private:
    TerrainRenderer m_terrainRenderer;

public:
    void AddTerrainChunk(int64_t key, int cx, int cz, int lod,
        const std::vector<ModelVertex>& vertices,
        const std::vector<uint32_t>& indices);
    void RemoveTerrainChunk(int64_t key);
    void SetTerrainChunkSize(float size);
    void ClearHeightCache();

private:
    SkyboxRenderer m_skybox;

private:
    VkRenderPass compositionRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    void CreateCompositionPass();
    void CreateFrameBuffers();

    VkDescriptorPool imguiDescriptorPool;

    void CreateImGuiDescriptorPool();
    void CreateImGui();
    void DrawGUI();

public:
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    std::array<FrustumPlane, 6> frustumPlanes;
    void UpdateFrustumPlanes(const glm::mat4& viewProj);

public:
    const std::array<FrustumPlane, 6>& GetFrustumPlanes() const { return frustumPlanes; }
    const glm::vec3& GetCameraPosition() const { return cameraPosition; }
    bool IsSphereInFrustum(const glm::vec3& center, float radius) const;
    bool IsWorldSphereInFrustum(const glm::vec3& center, float radius) const { // NEW
        return IsSphereInFrustum(center, radius);
    }

private:
    float m_fogStart = 1600.0f;
    float m_fogEnd = 1700.0f;
public:
    void SetFogParams(float start, float end);

private:
    WaterRenderer m_waterRenderer;
public:
    void AddWaterBodyForChunk(int64_t chunkKey, const WaterMesh& mesh,
        const std::string& normalMapPath,
        float tiling = 8.0f, float waveStrength = 0.15f);
    void RemoveWaterBody(int64_t chunkKey);
    void AddWaterBody(const WaterMesh& mesh, const std::string& normalMapTexturePath, float tiling = 8.0f, float waveStrength = 0.15f);

private:
    GrassRenderer m_grassRenderer;

public:
    void AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances);

private:
    // SSAO Resources
    VkImage ssaoImage = VK_NULL_HANDLE;
    VkDeviceMemory ssaoImageMemory = VK_NULL_HANDLE;
    VkImageView ssaoImageView = VK_NULL_HANDLE;
    VkSampler ssaoSampler = VK_NULL_HANDLE;
    VkSampler depthSampler = VK_NULL_HANDLE;

    VkPipeline ssaoPipeline = VK_NULL_HANDLE;
    VkPipelineLayout ssaoPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool ssaoDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet ssaoDescriptorSet = VK_NULL_HANDLE;

    VkBuffer ssaoUBO = VK_NULL_HANDLE;
    VkDeviceMemory ssaoUBOMemory = VK_NULL_HANDLE;
    SSAOUBO* ssaoUBOMapped = nullptr;

    // Ping-Pong blur targets
    VkImage ssaoPingPongImage = VK_NULL_HANDLE;
    VkDeviceMemory ssaoPingPongImageMemory = VK_NULL_HANDLE;
    VkImageView ssaoPingPongImageView = VK_NULL_HANDLE;

    VkImage ssaoBlurImage = VK_NULL_HANDLE;
    VkDeviceMemory ssaoBlurImageMemory = VK_NULL_HANDLE;
    VkImageView ssaoBlurImageView = VK_NULL_HANDLE;

    VkPipeline ssaoBlurPipeline = VK_NULL_HANDLE;
    VkPipelineLayout ssaoBlurPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoBlurDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool ssaoBlurDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet ssaoBlurDescriptorSetHorizontal = VK_NULL_HANDLE;
    VkDescriptorSet ssaoBlurDescriptorSetVertical = VK_NULL_HANDLE;

    void CreateSSAOResources();
    void CreateSSAOPipeline();
    void CreateSSAOBlurResources();
    void CreateSSAOBlurPipeline();

private:
    BoidRenderer m_boidRenderer;

public:
    void AddBoid(int64_t chunkKey, const std::vector<BoidInstance>& initialBoids, uint32_t textureId);
    void RemoveBoid(int64_t chunkKey);

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};