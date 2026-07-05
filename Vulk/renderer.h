#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vector>
#include <string>
#include <optional>
#include <chrono>

#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan_core.h>
#include <fstream>

#include "vertex.h"
#include "renderMesh.h"
#include "texture.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

struct CameraData;

// Structure to hold indices of the hardware execution queues we need
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
};

struct PushConstants {
    glm::mat4 modelMatrix; // 64 bytes
    uint32_t textureId;     // 4 bytes
    uint32_t objectId;      // 4 bytes
};

struct SubMesh {
    uint32_t indexCount = 0;   // Number of indices to draw
    uint32_t firstIndex = 0;   // The starting index slot in the global Index Buffer
    int32_t  vertexOffset = 0; // The base vertex offset index in the global Vertex Buffer
    uint32_t textureId = 0;    // Bindless texture array index

    glm::vec3 boundingCenterLocal = glm::vec3(0.0f); // NEW — local-space bounding sphere center
    float boundingRadiusLocal = 0.0f;
};

struct SceneObject {
    uint32_t firstSubMesh = 0;
    uint32_t subMeshCount = 0;
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    uint32_t objectId = 0;
};

struct FrustumPlane {
    glm::vec3 normal;
    float distance;
};

class VulkanRenderer {
private:
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice logicalDevice = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;

    // Core Setup Phases
    void InitWindow(int width, int height, const std::string& title);
    void InitVulkan();

    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();

    // Helper Utility Functions
    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);
    bool IsDeviceSuitable(VkPhysicalDevice device);
    std::vector<const char*> GetRequiredExtensions();

    // Configuration Settings
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

    GLFWwindow* GetWindow() const { return window; }

private:
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews; // Handles to wrap our images

    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;

    // New pipeline functions to add to your setup sequence
    void CreateSwapChain();
    void CreateImageViews();

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

    const int MAX_FRAMES_IN_FLIGHT = 2;
    size_t currentFrame = 0;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    // New phase declarations
    void CreateRenderPass();
    void CreateFrameBuffers();
    void CreateCommandPool();
    void CreateCommandBuffers();
    void CreateSyncObjects();
    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

public:
    // Add this public function so Game::Loop can trigger rendering frames!
    void DrawFrame();

private:
    static std::vector<char> ReadFile(const std::string& filename);
    VkShaderModule CreateShaderModule(const std::vector<char>& code);

private:
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline graphicsPipeline = VK_NULL_HANDLE;
    void CreateGraphicsPipeline();
	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    VkBuffer globalVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalVertexBufferMemory = VK_NULL_HANDLE;

    VkBuffer globalIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory globalIndexBufferMemory = VK_NULL_HANDLE;

    std::vector<ModelVertex> globalVertices;
    std::vector<uint32_t> globalIndices;
    std::vector<SubMesh> globalSubMeshes;

    std::vector<Texture> globalTextureRegistry;
    std::unordered_map<std::string, uint32_t> textureToIdMap;

    const VkDeviceSize MAX_GLOBAL_VERTICES = 5'000'000;
    const VkDeviceSize MAX_GLOBAL_INDICES = 10'000'000;
    const VkDeviceSize MAX_GLOBAL_SUBMESHES = 1'000;

    int32_t terrainVertexOffset;
    uint32_t terrainFirstIndex;
    uint32_t terrainIndexCount;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;

    VkDescriptorPool imguiDescriptorPool;

    Texture defaultTexture;

    void CreateTextureImage(const std::string& path, Texture& texture);
    void CreateDefaultTexture();
    void CreateTextureImageView(Texture& texture, VkFormat format);
    void CreateTextureSampler(Texture& texture);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    Texture& GetOrLoadTexture(const std::string& path);
    void ParseObjFileByMaterial(const std::string& filepath,
        std::vector<std::vector<ModelVertex>>& verticesPerMaterial,
        std::vector<std::vector<uint32_t>>& indicesPerMaterial,
        std::vector<std::string>& textureFilenames);

    void CreateDescriptorSetLayout();
    void CreateDescriptorPool();
    void CreateDescriptorSet();

    void CreateImGuiDescriptorPool();
    void CreateImGui();
    void DrawGUI();

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    void CreateGlobalBuffers();
    void CreateUniformBuffer();

    uint32_t drawCallCount = 0;
    std::vector<SceneObject> sceneObjects;

public:
    void UpdateUniformBuffer(const CameraData& cam);
    void UpdateGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void LoadModelAsset(const std::string& filename, glm::vec3 position, float scale, const std::string& texturePath = "");


private:
    uint32_t culledCount = 0;

    std::array<FrustumPlane, 6> frustumPlanes;
    void UpdateFrustumPlanes(const glm::mat4& viewProj);
    bool IsSphereInFrustum(const glm::vec3& center, float radius) const;

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};