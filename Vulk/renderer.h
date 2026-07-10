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

#include "mesh.h"
#include "gpu_instances.h"
#include "scene.h"
#include "scene_types.h"
#include "settings.h"
#include "assetManager.h"
#include "chunk.h"
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

struct PendingUpload {
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkBuffer stagingVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingVertexMemory = VK_NULL_HANDLE;
    VkBuffer stagingIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingIndexMemory = VK_NULL_HANDLE;
    int64_t chunkKey;
    TerrainChunkGPU chunk;
    bool cancelled = false; // set true if the chunk was removed while the upload was still in flight
};
struct PendingDeletion {
    std::vector<VkBuffer> buffers;
    std::vector<VkDeviceMemory> memories;
    uint64_t safeFrame;
};
struct PendingSpanReturn {
    FreeSpan vertexSpan;
    FreeSpan indexSpan;
    uint64_t safeFrame;
};
struct PendingGrassUpload {
    VkFence fence = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    int64_t chunkKey = 0;
    GrassChunkGPU grassChunk;
    bool cancelled = false; // set true if the chunk was removed while this upload was still in flight
};

struct FrustumPlane {
    glm::vec3 normal;
    float distance;
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
    VkSampleCountFlagBits m_currentMsaaSamples = VK_SAMPLE_COUNT_4_BIT;

    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    const int MAX_FRAMES_IN_FLIGHT = 3;
    size_t currentFrame = 0;

    std::vector<PendingDeletion> m_pendingDeletionsGlobal;
    std::vector<PendingSpanReturn> m_pendingTerrainSpanReturns;
    uint64_t m_globalFrameCounter = 0;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

    void CreateSwapChain();
    void RecreateSwapChain();

    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
    void CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels,
        VkSampleCountFlagBits numSamples, VkFormat format,
        VkImageTiling tiling, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage& image,
        VkDeviceMemory& imageMemory);
    void CreateImageViews();

    void CreateDepthResources();
    void CreateColorResources();

    void CreateRenderPass();
    void CreateFrameBuffers();
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

public:
    void DrawFrame();

private:
    Settings currentSettings;
    bool wireframeMode = false;

    void RecreateGraphicsPipeline();
    void ToggleFullscreen();
public:
    void ApplySettings();

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

    std::vector<FreeSpan> freeVertexSpans;
    std::vector<FreeSpan> freeIndexSpans;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;

    void* uniformBufferMapped = nullptr;

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

    std::vector<std::string> m_lastInstanceMeshNames;
    std::vector<std::vector<uint32_t>> m_objectDrawEntryIndices;

    std::vector<PendingUpload> pendingUploads;
    VkCommandPool uploadCommandPool;

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory);

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSet();
    void CreateGraphicsPipeline();
    void CreateGlobalBuffers();
    void CreateUniformBuffer();

    static std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);

    void DrawStaticMeshes(VkCommandBuffer commandBuffer);

public:
    void UpdateUniformBuffer(const CameraData& cam);
    void UploadStaticSceneData(const std::vector<ModelVertex>& verts, const std::vector<uint32_t>& idxs);

private:
    VkBuffer lightBuffer = VK_NULL_HANDLE;
    VkDeviceMemory lightBufferMemory = VK_NULL_HANDLE;
    std::vector<Light> currentLights;
    const uint32_t MAX_LIGHTS = 256;

    void* lightBufferMapped = nullptr;

    void CreateLightBuffer();

public:
    void SetLights(const std::vector<Light>& lights);

private:
    VkBuffer m_globalStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_globalStagingMemory = VK_NULL_HANDLE;
    void* m_stagingBufferMapped = nullptr;

    uint32_t m_stagingRingOffset = 0;
    const uint32_t STAGING_BUFFER_SIZE = 32 * 1024 * 1024; // 32MB 
    std::mutex m_stagingBufferMutex;

    VkBuffer globalTerrainVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalTerrainVertexMemory = VK_NULL_HANDLE;

    VkBuffer globalTerrainIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalTerrainIndexMemory = VK_NULL_HANDLE;

    std::atomic<uint32_t> nextTerrainVertexOffset{ 0 };
    std::atomic<uint32_t> nextTerrainIndexOffset{ 0 };
    std::mutex terrainAllocMutex;

    const VkDeviceSize MAX_TERRAIN_VERTICES = 5'000'000;
    const VkDeviceSize MAX_TERRAIN_INDICES = 10'000'000;

    std::unordered_map<int64_t, TerrainChunkGPU> terrainChunks;
    std::vector<ChunkSlot> chunkSlots;

    float m_terrainChunkSize; // Is set in chunk::Init

    std::unordered_set<VkBuffer> m_allocatedTerrainBuffers;
    std::unordered_set<VkDeviceMemory> m_allocatedTerrainMemory;

    std::vector<DrawEntry> terrainDrawList;
    std::vector<DrawEntry> visibleTerrainDrawList;

    void UpdateTextureDescriptors(const Scene& scene);

    void CreateTerrainBuffers();

    void DrawTerrain(VkCommandBuffer commandBuffer);
    void UploadTerrainChunkAsync(
        TerrainChunkGPU& chunk,
        const std::vector<ModelVertex>& verts,
        const std::vector<uint32_t>& indices);
    bool IsChunkOccluded(const glm::vec3& chunkCenter, float chunkRadius);

public:
    void AddTerrainChunk(
        int64_t key,
        int cx,
        int cz,
        int lod,
        const std::vector<ModelVertex>& vertices,
        const std::vector<uint32_t>& indices);
    void RemoveTerrainChunk(
        int64_t key);
    void SetTerrainChunkSize(float size) { m_terrainChunkSize = size; }

private:
    VkPipeline skyboxPipeline = VK_NULL_HANDLE;
    VkPipelineLayout skyboxPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule skyboxVertModule = VK_NULL_HANDLE;
    VkShaderModule skyboxFragModule = VK_NULL_HANDLE;

    Texture m_skyboxTexture;

    void CreateSkyboxTexture();
    void UpdateSkyboxDescriptor();

    void CreateSkyboxPipeline();
    void DrawSkybox(VkCommandBuffer commandBuffer);

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

private:
    float m_fogStart = 1600.0f;
    float m_fogEnd = 1700.0f;

    glm::vec3 m_lastOcclusionCameraPos = glm::vec3(0.0f);
    bool m_firstOcclusionUpdate = true;
public:
    void SetFogParams(float start, float end);

private:
    VkPipeline waterPipeline = VK_NULL_HANDLE;
    VkPipelineLayout waterPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule waterVertModule = VK_NULL_HANDLE;
    VkShaderModule waterFragModule = VK_NULL_HANDLE;

    std::vector<WaterBodyGPU> m_waterBodies;
    std::chrono::high_resolution_clock::time_point m_waterStartTime = std::chrono::high_resolution_clock::now();

    std::unordered_map<int64_t, size_t> m_waterBodyLookup;
    std::vector<StaleWaterBuffers> m_staleWaterQueue;

    void CreateWaterPipeline();
    void DrawWater(VkCommandBuffer commandBuffer);
    size_t CreateWaterBodyGPU(const WaterMesh& mesh,
        const std::string& normalMapPath,
        float tiling, float waveStrength);
public:
    void AddWaterBodyForChunk(int64_t chunkKey, const WaterMesh& mesh,
        const std::string& normalMapPath,
        float tiling = 8.0f, float waveStrength = 0.15f);
    void RemoveWaterBody(int64_t chunkKey);
    void AddWaterBody(const WaterMesh& mesh, const std::string& normalMapTexturePath, float tiling = 8.0f, float waveStrength = 0.15f);

private:
    VkPipeline grassPipeline = VK_NULL_HANDLE;
    VkPipelineLayout grassPipelineLayout = VK_NULL_HANDLE;

    std::unordered_map<int64_t, GrassChunkGPU> m_grassChunks;
    std::vector<PendingGrassUpload> pendingGrassUploads;

    void CreateGrassPipeline();
    void DrawGrass(VkCommandBuffer commandBuffer);

public:
    void AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances);

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};