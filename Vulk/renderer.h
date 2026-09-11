#pragma once

#define GLFW_INCLUDE_VULKAN

#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_set>
#include <mutex>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_core.h>
#include <fstream>
#include <random>
#include <future>
#include <array>

#include "mesh.h"

#include "gpu_instances.h"
#include "gpu_async.h"

#include "scene.h"
#include "mesh_types.h"
#include "light.h"

#include "settings.h"
#include "asset_manager.h"

#include "renderer_skybox.h"
#include "renderer_grass.h"
#include "renderer_terrain.h"
#include "renderer_static.h"
#include "renderer_boid.h"
#include "renderer_water.h"

#include "ring_buffer_uploader.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

struct CameraData;
class Scene;

enum class PipelineVertexType {
    Terrain,
    Static,
    Instanced
};

class VulkanRenderer {
private:
    static constexpr uint32_t MAX_SUPPORTED_FRAMES_IN_FLIGHT = 3;
    uint32_t m_framesInFlight = 3;
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
    void BeginUI();
    void EndUI();

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    VkDevice GetLogicalDevice() const { return logicalDevice; }
    VkPhysicalDevice GetPhysicalDevice() const { return physicalDevice; }
    VkCommandPool GetCommandPool() const { return commandPool; }
    VkQueue GetGraphicsQueue() const { return graphicsQueue; }
    GLFWwindow* GetWindow() const { return window; }
    uint32_t GetFramesInFlight() const {
        return m_framesInFlight;
    }

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
    RenderTarget offscreenTarget;
    RenderTarget waterTarget;

    // --- Composition Pipeline (Native Res UI) ---
    VkPipeline compositionPipeline = VK_NULL_HANDLE;
    VkPipelineLayout compositionPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositionDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool compositionDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet compositionDescriptorSet = VK_NULL_HANDLE;

    RenderTarget depthTarget;  // .sampler is the depth-read sampler used by SSAO/HZB
    RenderTarget colorTarget;  // MSAA target; no dedicated sampler

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    static constexpr uint32_t NUM_RENDER_THREADS = 4;
    std::vector<std::vector<VkCommandPool>> threadCommandPools;
    std::vector<std::vector<VkCommandBuffer>> threadCommandBuffers;

    DeferredQueue<BufferDeletion> m_pendingDeletionsGlobal;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

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

public:
    void DrawFrame();
    void UpdateDRS();

    uint32_t GetInternalWidth() const {
        uint32_t w = std::max(1u, static_cast<uint32_t>(swapChainExtent.width * g_Settings.renderScale));
        return w + (w % 2);
    }
    uint32_t GetInternalHeight() const {
        uint32_t h = std::max(1u, static_cast<uint32_t>(swapChainExtent.height * g_Settings.renderScale));
        return h + (h % 2);
    }
    glm::vec2 GetInternalUvScale() const {
        return glm::vec2(
            static_cast<float>(GetInternalWidth()) / static_cast<float>(swapChainExtent.width),
            static_cast<float>(GetInternalHeight()) / static_cast<float>(swapChainExtent.height)
        );
    }

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

    RenderTarget hzbTarget;
    std::vector<VkImageView> hzbMipViews;
    uint32_t hzbMipLevels = 1;
    glm::vec2 hzbDimensions = { 0.0f, 0.0f };

    VkPipeline hzbPipeline = VK_NULL_HANDLE;
    VkPipelineLayout hzbPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout hzbDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool hzbDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> hzbDescriptorSets;

    void CreateHZBResources();
    void CreateHZBPipeline();
    void GenerateHZB(VkCommandBuffer commandBuffer);

public:
    void AddTerrainChunk(int64_t key, int cx, int cz, int lod,
        const std::vector<ModelVertex>& vertices,
        const std::vector<uint32_t>& indices);
    void RemoveTerrainChunk(int64_t key);
    void SetTerrainChunkSize(float size);
    void ClearHeightCache();

private:
    SkyboxRenderer m_skybox;
    SkyParams m_skyParams;
public:
    void SetSkyParams(const glm::vec3& z, const glm::vec3& h, float sf, float ct, float cv) {
        m_skyParams.zenithColor = z;
        m_skyParams.horizonColor = h;
        m_skyParams.starFade = sf;
        m_skyParams.cloudTime = ct;
        m_skyParams.coverage = cv;
    }

private:
    VkRenderPass compositionRenderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapChainFramebuffers;

    void CreateCompositionPass();
    void CreateFrameBuffers();

    VkDescriptorPool imguiDescriptorPool;

    void CreateImGuiDescriptorPool();
    void CreateImGui();

public:
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::mat4 m_currentViewProj = glm::mat4(1.0f);
    glm::mat4 m_previousViewProj = glm::mat4(1.0f);
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
    GrassRenderer m_grassRenderer;
public:
    void AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances);
    void RemoveGrass(int64_t key);

private:
    // SSAO Resources
    RenderTarget ssaoTarget;         // .sampler is reused for reading ssaoBlurTarget/ssaoPingPongTarget too
    RenderTarget ssaoPingPongTarget; // no dedicated sampler — sampled via ssaoTarget.sampler
    RenderTarget ssaoBlurTarget;     // no dedicated sampler — sampled via ssaoTarget.sampler
    RenderTarget ssaoNoiseTarget;

    VkPipeline ssaoPipeline = VK_NULL_HANDLE;
    VkPipelineLayout ssaoPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool ssaoDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet ssaoDescriptorSet = VK_NULL_HANDLE;

    VkBuffer ssaoUBO = VK_NULL_HANDLE;
    VkDeviceMemory ssaoUBOMemory = VK_NULL_HANDLE;
    SSAOUBO* ssaoUBOMapped = nullptr;

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
    void AddSwarms(int64_t chunkKey, const std::vector<SwarmData>& swarms);
    void RemoveSwarms(int64_t chunkKey);

private:
    WaterRenderer m_waterRenderer;

    void CreateWaterTarget();
public:
    void AddWaterChunk(int64_t key, const std::vector<ModelVertex>& vertices, const std::vector<uint32_t>& indices);
    void RemoveWaterChunk(int64_t key);

private:
    static constexpr uint32_t CLUSTER_GRID_X = 16;
    static constexpr uint32_t CLUSTER_GRID_Y = 9;
    static constexpr uint32_t CLUSTER_GRID_Z = 24;
    static constexpr uint32_t TOTAL_CLUSTERS = CLUSTER_GRID_X * CLUSTER_GRID_Y * CLUSTER_GRID_Z;
    static constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 64;

    struct ClusterAABB { glm::vec4 minPoint; glm::vec4 maxPoint; };
    struct ClusterRecord { uint32_t offset; uint32_t count; };

    VkBuffer m_clusterAABBBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_clusterAABBMemory = VK_NULL_HANDLE;

    std::vector<VkBuffer> m_clusterDataBuffers;
    std::vector<VkDeviceMemory> m_clusterDataMemories;
    std::vector<void*> m_clusterDataMapped;

    VkDescriptorSetLayout m_clusterSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_clusterForwardSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_clusterDescriptorPool = VK_NULL_HANDLE;

    VkPipelineLayout m_clusterPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_clusterPipeline = VK_NULL_HANDLE;

    std::vector<VkDescriptorSet> m_clusterDescriptorSets;
    std::vector<VkDescriptorSet> m_clusterForwardSets;

    void CreateClusterResources();
    void CreateClusterPipelines();
    void GenerateClusterAABBs();

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};