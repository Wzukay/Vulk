#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <optional>
#include <chrono>
#include <unordered_set>

#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_core.h>
#include <fstream>

#include "renderMesh.h"
#include "scene.h"
#include "scene_types.h"
#include "settings.h"
#include "assetManager.h"

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

struct UniformBufferObject {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::vec3 cameraPos;
    float ambient;
    float specularPower;
    uint32_t lightCount;
};

struct PushConstants {
    glm::mat4 modelMatrix;      // 64 bytes
    uint32_t textureId;         // 4 bytes
    uint32_t normalTextureId;   // 4 bytes
    uint32_t objectId;          // 4 bytes
};

struct ChunkSlot {
    uint32_t vertexOffset;
    uint32_t indexOffset;
    uint32_t indexCount;
    bool isAllocated;
    std::string objectName;
};

struct DrawEntry {
    uint32_t objectIndex;
    uint32_t subMeshIndex; // index into globalSubMeshes
    float distSq;
    glm::vec3 worldCenter;
    float worldRadius;
    int64_t chunkKey = -1;
};
struct TerrainChunkGPU
{
    int64_t key;
    glm::vec3 center;
    float radius;
    std::string meshName;
    glm::mat4 transform;

    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    uint32_t indexCount = 0;

    std::vector<SubMesh> subMeshes;
};
struct PendingDeletion {
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
};

struct FrustumPlane {
    glm::vec3 normal;
    float distance;
};

struct Light {
    alignas(16) glm::vec4 positionOrDir; // w: 0 = directional, 1 = point
    alignas(16) glm::vec4 color;         // rgb = color, a = intensity
    alignas(16) glm::vec4 params;        // x = range (point lights)

    static Light Directional(const glm::vec3& direction, const glm::vec3& color, float intensity = 1.0f) {
        Light l{};
        l.positionOrDir = glm::vec4(glm::normalize(direction), 0.0f);
        l.color = glm::vec4(color, intensity);
        l.params = glm::vec4(0.0f);
        return l;
    }

    static Light Point(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float range = 10.0f) {
        Light l{};
        l.positionOrDir = glm::vec4(position, 1.0f);
        l.color = glm::vec4(color, intensity);
        l.params = glm::vec4(range, 0.0f, 0.0f, 0.0f);
        return l;
    }
};

class VulkanRenderer {
private:
    int windowedPosX = 0, windowedPosY = 0;
    int windowedWidth = 1280, windowedHeight = 720;

    bool m_isShuttingDown = false;

    GLFWwindow* window = nullptr;

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

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
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

    VkDevice GetLogicalDevice() const { return logicalDevice; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
    VkCommandPool GetCommandPool() const { return commandPool; }
    VkQueue GetGraphicsQueue() const { return graphicsQueue; }
    GLFWwindow* GetWindow() const { return window; }

private:
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;

    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews; // Handles to wrap our images

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    void CreateSwapChain();
    void CreateImageViews();

    void RecreateSwapChain();
    void CleanupSwapChain();

    void CreateDepthResources();
    VkFormat FindDepthFormat();
    VkFormat FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

    // Swapchain configuration helpers
    SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);
    VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    const int MAX_FRAMES_IN_FLIGHT = 3;
    size_t currentFrame = 0;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

    // New phase declarations
    void CreateRenderPass();
    void CreateFrameBuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

public:
    void DrawFrame();

private:
    Settings currentSettings;  // tracks what is currently applied
    bool wireframeMode = false;

    void RecreateGraphicsPipeline();
    void ToggleFullscreen();
public:
    void ApplySettings();

private:
    static std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);

private:
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;

    VkBuffer globalVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalVertexBufferMemory = VK_NULL_HANDLE;

    VkBuffer globalIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalIndexBufferMemory = VK_NULL_HANDLE;

    const VkDeviceSize MAX_GLOBAL_VERTICES = 5'000'000;
    const VkDeviceSize MAX_GLOBAL_INDICES = 10'000'000;
    const VkDeviceSize MAX_GLOBAL_SUBMESHES = 1'000;

    std::unordered_map<int64_t, TerrainChunkGPU> terrainChunks;
    std::vector<ChunkSlot> chunkSlots;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;

    void* uniformBufferMapped = nullptr;

    VkBuffer lightBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lightBufferMemory = VK_NULL_HANDLE;
    std::vector<Light> currentLights;
    const uint32_t MAX_LIGHTS = 256;

    void* lightBufferMapped = nullptr;

    std::vector<SceneObject> currentSceneObjects;
    std::vector<SubMesh> currentSceneSubMeshes;

    uint32_t sceneTotalVertices = 0;
    uint32_t sceneTotalIndices = 0;
    uint32_t m_lastTotalVertices = 0;
    uint32_t m_lastTotalIndices = 0;

    uint32_t drawCallCount = 0;
    uint32_t culledCount = 0;
    uint32_t m_lastDrawCallCount = 0;
    uint32_t m_lastCulledCount = 0;

    std::vector<DrawEntry> staticDrawList;
    std::vector<DrawEntry> visibleStaticDrawList;

    std::unordered_set<VkBuffer> m_allocatedTerrainBuffers;
    std::unordered_set<VkDeviceMemory> m_allocatedTerrainMemory;

    std::vector<DrawEntry> terrainDrawList;
    std::vector<DrawEntry> visibleTerrainDrawList;
    std::array<PendingDeletion, 3> pendingDeletions;

    void CreateGraphicsPipeline(VkPolygonMode polygonMode);
	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSet();

    void UpdateTextureDescriptors(const Scene& scene);

    void CreateGlobalBuffers();
    void CreateUniformBuffer();
    void CreateLightBuffer();

    void DrawStaticMeshes(VkCommandBuffer commandBuffer);
    void DrawTerrain(VkCommandBuffer commandBuffer);
    void UploadTerrainChunk(
        TerrainChunkGPU& chunk,
        const std::vector<ModelVertex>& verts,
        const std::vector<uint32_t>& indices);

public:
    void UpdateUniformBuffer(const CameraData& cam);
    void SetLights(const std::vector<Light>& lights);

    void UploadStaticSceneData(const std::vector<ModelVertex>& verts, const std::vector<uint32_t>& idxs);
    
    void AddTerrainChunk(
        int64_t key,
        int cx,
        int cz,
        const std::vector<ModelVertex>& vertices,
        const std::vector<uint32_t>& indices);
    void RemoveTerrainChunk(
        int64_t key);

private:
    bool showSettingsPanel = false;
    VkDescriptorPool imguiDescriptorPool;

    void CreateImGuiDescriptorPool();
    void CreateImGui();
    void DrawGUI();

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    std::array<FrustumPlane, 6> frustumPlanes;
    void UpdateFrustumPlanes(const glm::mat4& viewProj);
    bool IsSphereInFrustum(const glm::vec3& center, float radius) const;

public:
    bool IsWorldSphereInFrustum(const glm::vec3& center, float radius) const { // NEW
        return IsSphereInFrustum(center, radius);
    }
    

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};