#include "renderer.h"
#include "input.h"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <set>
#include <unordered_map>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Proxy functions to handle loading Vulkan extension functions for debugging
VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) func(instance, debugMessenger, pAllocator);
}

// Validation layer callback logic to catch graphics pipeline runtime bugs
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserVoid) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[VULKAN VALIDATION]: " << pCallbackData->pMessage << "\n\n";
    }
    return VK_FALSE;
}

void VulkanRenderer::Initialize(int width, int height, const std::string& title) {
    InitWindow(width, height, title);
    InitVulkan();
}

void VulkanRenderer::InitWindow(int width, int height, const std::string& title) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Prevents GLFW from loading OpenGL contexts
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);   // Locks window bounds initially
    window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
}

void VulkanRenderer::InitVulkan() {
    CreateInstance();
    SetupDebugMessenger();
    CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();

    CreateSwapChain();
    CreateImageViews();
    CreateDepthResources();
    CreateRenderPass();  
    CreateFrameBuffers();

    CreateCommandPool();
    CreateCommandBuffers(); 
    CreateSyncObjects();

    CreateDescriptorSetLayout();
    CreateUniformBuffer();
    CreateDescriptorPool();
    CreateImGui();

    CreateGlobalBuffers();

    CreateDefaultTexture();
    CreateDescriptorSet();

    CreateGraphicsPipeline();   
}

void VulkanRenderer::CreateInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Procedural 3D Vulkan Game Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Custom Procedural Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3; // targeting standard core Vulkan 1.3 features

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    }
    else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Critical Failure: Unable to build raw Vulkan software runtime instance.");
    }
}

void VulkanRenderer::SetupDebugMessenger() {
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Critical Failure: Could not build validation messenger layer callback routing hook.");
    }
}

void VulkanRenderer::CreateSurface() {
    // Uses GLFW library context to handle drawing interfaces natively for Windows/Linux platforms
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("Critical Failure: Platform window surface generation failed.");
    }
}

void VulkanRenderer::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) throw std::runtime_error("Critical Failure: No physical graphics hardware devices supporting Vulkan detected.");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (IsDeviceSuitable(device)) {
            physicalDevice = device;
            break;
        }
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Critical Failure: Did not locate a suitable graphics device processing framework configuration.");
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::cout << "[Graphics Device Found]: " << deviceProperties.deviceName << "\n\n";
}

void VulkanRenderer::CreateLogicalDevice() {
    QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    // 2. Request modern Descriptor Indexing features (Vulkan 1.2 core features)
    VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{};
    indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
    indexingFeatures.runtimeDescriptorArray = VK_TRUE;
    indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
    indexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
    indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.pNext = &indexingFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = nullptr;

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice) != VK_SUCCESS) {
        throw std::runtime_error("Critical Failure: Unable to build device driver pipeline interface configuration.");
    }

    vkGetDeviceQueue(logicalDevice, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(logicalDevice, indices.presentFamily.value(), 0, &presentQueue);
}

bool VulkanRenderer::IsDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = FindQueueFamilies(device);
    bool extensionsSupported = CheckDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool VulkanRenderer::CheckDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }
    return requiredExtensions.empty();
}

QueueFamilyIndices VulkanRenderer::FindQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) break;
        i++;
    }
    return indices;
}

std::vector<const char*> VulkanRenderer::GetRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

bool VulkanRenderer::ShouldClose() { return glfwWindowShouldClose(window); }
void VulkanRenderer::PollEvents() { glfwPollEvents(); }

void VulkanRenderer::Cleanup() {
    // Wait until the GPU is totally idle before deleting resources
    if (logicalDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(logicalDevice);
    }

    vkDestroyBuffer(logicalDevice, globalIndexBuffer, nullptr);
    vkFreeMemory(logicalDevice, globalIndexBufferMemory, nullptr);

    vkDestroyBuffer(logicalDevice, globalVertexBuffer, nullptr);
    vkFreeMemory(logicalDevice, globalVertexBufferMemory, nullptr);

    // Destroy Descriptor objects
    vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);

    vkDestroyImageView(logicalDevice, depthImageView, nullptr);
    vkDestroyImage(logicalDevice, depthImage, nullptr);
    vkFreeMemory(logicalDevice, depthImageMemory, nullptr);

    defaultTexture.CleanUp(logicalDevice);

    for (auto& tex : globalTextureRegistry) {
        if (tex.sampler != VK_NULL_HANDLE) vkDestroySampler(logicalDevice, tex.sampler, nullptr);
        if (tex.imageView != VK_NULL_HANDLE) vkDestroyImageView(logicalDevice, tex.imageView, nullptr);
        if (tex.image != VK_NULL_HANDLE) vkDestroyImage(logicalDevice, tex.image, nullptr);
        if (tex.imageMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, tex.imageMemory, nullptr);
    }
    globalTextureRegistry.clear();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(logicalDevice, imguiDescriptorPool, nullptr);

    // Destroy UBO
    vkDestroyBuffer(logicalDevice, uniformBuffer, nullptr);
    vkFreeMemory(logicalDevice, uniformBufferMemory, nullptr);

    if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (imageAvailableSemaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(logicalDevice, imageAvailableSemaphores[i], nullptr);
        if (renderFinishedSemaphores[i] != VK_NULL_HANDLE) vkDestroySemaphore(logicalDevice, renderFinishedSemaphores[i], nullptr);
        if (inFlightFences[i] != VK_NULL_HANDLE) vkDestroyFence(logicalDevice, inFlightFences[i], nullptr);
    }

    if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice, commandPool, nullptr);

    for (auto framebuffer : swapChainFramebuffers) {
        vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
    }
    if (renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
    for (auto imageView : swapChainImageViews) {
        vkDestroyImageView(logicalDevice, imageView, nullptr);
    }
    if (swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(logicalDevice, swapChain, nullptr);
    }

    if (enableValidationLayers && debugMessenger != VK_NULL_HANDLE) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
    }
    if (logicalDevice != VK_NULL_HANDLE) vkDestroyDevice(logicalDevice, nullptr);
    if (surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance, surface, nullptr);
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);

    if (window) glfwDestroyWindow(window);
    glfwTerminate();
}

SwapChainSupportDetails VulkanRenderer::QuerySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkSurfaceFormatKHR VulkanRenderer::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    // Look for standard SRGB color formatting
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR VulkanRenderer::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    // Mailbox (Triple Buffering) is ideal for low latency without tearing, heavily supported by AMD
    for (const auto& availablePresentMode : availablePresentModes) {
        // Mailbox mode allows uncapped frame rates without visual tearing
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    // Fallback: Immediate mode turns off VSync completely (may cause tearing)
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return availablePresentMode;
        }
    }

    // Strict VSync fallback
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRenderer::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }
    else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        VkExtent2D actualExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height) };
        actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
        actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));
        return actualExtent;
    }
}

void VulkanRenderer::CreateSwapChain() {
    SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

    // Request triple buffering if possible, otherwise settle for double buffering
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // We draw directly to these images

    QueueFamilyIndices indices = FindQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(logicalDevice, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan Swapchain presentation engine.");
    }

    // Retrieve handles to the raw images allocated by the swapchain
    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(logicalDevice, swapChain, &imageCount, swapChainImages.data());

    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

void VulkanRenderer::CreateImageViews() {
    swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;

        // Components mapping allows swizzling color channels around if needed
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // Subresource range details what the image targets (color, mip levels, layers)
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(logicalDevice, &createInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map target device Image Views.");
        }
    }
}

void VulkanRenderer::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Clear screen before drawing
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Ready for swapchain presentation

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = FindDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create clear render pass layout.");
    }
}

void VulkanRenderer::CreateFrameBuffers() {
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = { swapChainImageViews[i], depthImageView };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = swapChainExtent.width;
        framebufferInfo.height = swapChainExtent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map pipeline framebuffers.");
        }
    }
}

void VulkanRenderer::CreateCommandPool() {
    QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(physicalDevice);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate graphics command allocation pool.");
    }
}

void VulkanRenderer::CreateCommandBuffers() {
    commandBuffers.resize(swapChainFramebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to distribute command routing tracks.");
    }
}

void VulkanRenderer::CreateSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Crucial: start opened

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {

            throw std::runtime_error("Failed to initialize multi-frame synchronization primitives.");
        }
    }
}

void VulkanRenderer::CreateGlobalBuffers() {
    VkDeviceSize vertexBufferSize = sizeof(ModelVertex) * MAX_GLOBAL_VERTICES;
    VkDeviceSize indexBufferSize = sizeof(uint32_t) * MAX_GLOBAL_INDICES;

    // 1. Allocate continuous GPU memory space for all combined scene vertices
    CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, globalVertexBuffer, globalVertexBufferMemory);

    // 2. Allocate continuous GPU memory space for all combined scene indices
    CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, globalIndexBuffer, globalIndexBufferMemory);

    std::cout << "[Memory Manager] Pre-allocated global monolithic vertex/index buffers successfully.\n";
}

void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    drawCallCount = 0;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to start recording graphics buffer channel.");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = swapChainExtent;

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { {0.1f, 0.15f, 0.25f, 1.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (graphicsPipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        if (globalVertexBuffer != VK_NULL_HANDLE && globalIndexBuffer != VK_NULL_HANDLE) {
            VkBuffer vertexBuffers[] = { globalVertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, globalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        PushConstants terrainConstants{};
        terrainConstants.modelMatrix = glm::mat4(1.0f);
        terrainConstants.objectId = 0;
        terrainConstants.textureId = 0;

        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &terrainConstants);

        if (terrainIndexCount > 0) {
            vkCmdDrawIndexed(commandBuffer, terrainIndexCount, 1, terrainFirstIndex, terrainVertexOffset, 0);
            drawCallCount++;
        }

        VkBuffer vertexBuffers[] = { globalVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(commandBuffer, globalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

        culledCount = 0;

        for (const auto& obj : sceneObjects) {
            for (uint32_t i = obj.firstSubMesh; i < obj.firstSubMesh + obj.subMeshCount; i++){

				const auto& sub = globalSubMeshes[i];

                glm::vec3 worldCenter = glm::vec3(obj.modelMatrix * glm::vec4(sub.boundingCenterLocal, 1.0f));

                float scaleX = glm::length(glm::vec3(obj.modelMatrix[0]));
                float scaleY = glm::length(glm::vec3(obj.modelMatrix[1]));
                float scaleZ = glm::length(glm::vec3(obj.modelMatrix[2]));
                float worldRadius = sub.boundingRadiusLocal * std::max({ scaleX, scaleY, scaleZ });

                if (!IsSphereInFrustum(worldCenter, worldRadius)) {
                    culledCount++;
                    continue; // Skip this submesh entirely — outside the view frustum
                }

                PushConstants constants{};
                constants.modelMatrix = obj.modelMatrix;
                constants.objectId = obj.objectId;
                constants.textureId = sub.textureId;

                vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &constants);

                vkCmdDrawIndexed(commandBuffer, sub.indexCount, 1, sub.firstIndex, sub.vertexOffset, 0);
                drawCallCount++;
            }
        }
    }
    else {
        // Safe console feedback if the pipeline failed to compile earlier
        static bool warned = false;
        if (!warned) {
            std::cerr << "[VULKAN RUNTIME] Pipeline is not initialized yet. Skipping scene geometry draw calls.\n";
            warned = true;
        }
    }

    if (ImGui::GetDrawData() != nullptr) {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
        drawCallCount++;
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record layout command instructions.");
    }
}

void VulkanRenderer::DrawFrame() {
    if (globalVertexBuffer == VK_NULL_HANDLE) {
        std::cout << "CRITICAL: vertexBuffer is still null when drawing!" << std::endl;
    }

    DrawGUI();

    // 1. Wait for the CURRENT frame slot's fence to open
    vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(logicalDevice, 1, &inFlightFences[currentFrame]);

    uint32_t imageIndex;
    // 2. Signal the current frame's image availability semaphore
    vkAcquireNextImageKHR(logicalDevice, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    // 3. Reset and record your command buffer target link
    vkResetCommandBuffer(commandBuffers[imageIndex], 0);
    RecordCommandBuffer(commandBuffers[imageIndex], imageIndex);

    // 4. Submit payload configuration
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    // Submit using the current frame's fence token tracking entry
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit execution instructions to graphics queue channels.");
    }

    // 5. Present the frame back onto the window screen
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = { swapChain };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    vkQueuePresentKHR(presentQueue, &presentInfo);

    // 6. Advance frame slot index counter (alternating 0, 1, 0, 1...)
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

std::vector<char> VulkanRenderer::ReadFile(const std::string& filename) {
    // Open the file at the end (ios::ate) and in binary mode (ios::binary)
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        throw std::runtime_error("Critical Failure: Could not open shader binary target path: " + filename);
    }

    // Since we opened at the end, tellg() instantly gives us the exact file size allocation needed
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);

    // Seek back to the beginning and pull all the data into our memory array
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

VkShaderModule VulkanRenderer::CreateShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();

    // Vulkan expects the bytecode pointer to be typed as uint32_t words rather than raw chars
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(logicalDevice, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Critical Failure: Failed to map graphics shader module bytecode allocation.");
    }

    return shaderModule;
}

void VulkanRenderer::CreateGraphicsPipeline() {
    // 1. Read our compiled binary shader files from disk
    auto vertShaderCode = ReadFile("shaders/vert.spv");
    auto fragShaderCode = ReadFile("shaders/frag.spv");

    // 2. Wrap them into hardware execution shader modules
    VkShaderModule vertShaderModule = CreateShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(fragShaderCode);

    // Assign Vertex Shader to the pipeline stage configuration
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main"; // Entry point function name inside the shader

    // Assign Fragment Shader to the pipeline stage configuration
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    // 3. Fixed Function: Vertex Input State
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // 4. Fixed Function: Input Assembly (Drawing topology)
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // Draw standard solid triangles
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 5. Fixed Function: Viewport & Scissors (Tells Vulkan how to map drawing to screen space)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent.width;
    viewport.height = (float)swapChainExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = swapChainExtent;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // 6. Fixed Function: Rasterizer (Handles geometry rendering properties)
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // Solid geometry fill mode
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;    // Back-face culling enabled
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // 7. Fixed Function: Multisampling (Basic anti-aliasing initialization configuration)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // 8. Fixed Function: Color Blending (Controls alpha blending transparency mechanics)
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 8.6 Depth Stencil State (CRITICAL: Must be defined even if unused)
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.stencilTestEnable = VK_FALSE;

    // 8.7 Dynamic State (If you want to resize window, you need this)
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    // 9. Pipeline Layout creation (Handles global constants/uniform variables pass-through configurations)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to build pipeline uniform layout layout settings object.");
    }

    // 10. Assemble the complete monolithic Pipeline Object
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass; // The clear frame routing loop layout we made earlier
    pipelineInfo.subpass = 0;

    VkResult result = vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline! Result code: " + std::to_string(result));
    }

    // Clean up temporary shader local modules since the code is baked directly inside the pipeline now
    vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
    vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
}

uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type for GPU buffer.");
}

void VulkanRenderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    if (logicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("CRITICAL: logicalDevice is NULL in CreateBuffer!");
    }
    
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create vertex buffer handle.");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(logicalDevice, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GPU memory for buffer.");
    }

    vkBindBufferMemory(logicalDevice, buffer, bufferMemory, 0);
}

void VulkanRenderer::CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 500; // Large upper bound allocation
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };

    VkDescriptorBindingFlags bindingFlags[2] = {
        0,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo layoutBindingFlags{};
    layoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    layoutBindingFlags.bindingCount = 2;
    layoutBindingFlags.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.pNext = &layoutBindingFlags;

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create bindless descriptor set layout.");
    }
}

void VulkanRenderer::CreateDescriptorPool() {
    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 500;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT; // Allow binding changes on the fly
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1; // Only need 1 global set now!

    if (vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create global bindless descriptor pool.");
    }
}

void VulkanRenderer::CreateDescriptorSet() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    // Configure variable descriptor size limitations
    VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
    countInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    countInfo.descriptorSetCount = 1;
    uint32_t maxBindingCounts = 500;
    countInfo.pDescriptorCounts = &maxBindingCounts;
    allocInfo.pNext = &countInfo;

    if (vkAllocateDescriptorSets(logicalDevice, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate global bindless descriptor set.");
    }

    // Connect the UBO to Binding 0 (Samplers are updated as textures load)
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

void VulkanRenderer::CreateUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    // Create the buffer
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer, uniformBufferMemory);
}

void VulkanRenderer::UpdateUniformBuffer(const CameraData& cam) {
    UniformBufferObject ubo{};

    ubo.view = glm::lookAt(cam.pos, cam.pos + cam.front, cam.up);
    ubo.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 1000.0f);
    ubo.proj[1][1] *= -1;

    UpdateFrustumPlanes(ubo.proj * ubo.view);

    void* data;
    vkMapMemory(logicalDevice, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(logicalDevice, uniformBufferMemory);
}

void VulkanRenderer::UpdateGeometry(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (globalVertexBuffer == VK_NULL_HANDLE || globalIndexBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("[Renderer Error] Cannot update geometry before global monolithic buffers are created.");
    }

    // Safety check to ensure the incoming procedural terrain grid fits in the pre-allocated pools
    if (vertices.size() > MAX_GLOBAL_VERTICES || indices.size() > MAX_GLOBAL_INDICES) {
        throw std::runtime_error("[Memory Manager] Incoming terrain data exceeds pre-allocated monolithic buffer bounds!");
    }

    // 1. Convert the project 'Vertex' layouts to your unified 'ModelVertex' layouts
    std::vector<ModelVertex> terrainVertices;
    terrainVertices.reserve(vertices.size());
    for (const auto& v : vertices) {
        ModelVertex mv{};
        mv.pos = v.pos;
        mv.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default smooth shading up-vector fallback
        mv.texCoord = v.texCoord;
        terrainVertices.push_back(mv);
    }

    // 2. Set the terrain offsets (terrain always occupies the very beginning of the GPU buffer)
    terrainVertexOffset = 0;
    terrainFirstIndex = 0;
    terrainIndexCount = static_cast<uint32_t>(indices.size());

    // 3. Calculate total byte dimensions for the transfer
    VkDeviceSize vertexSize = sizeof(ModelVertex) * terrainVertices.size();
    VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

    // 4. Create temporary local host-visible staging allocations
    VkBuffer stagingVert; VkDeviceMemory stagingVertMem;
    CreateBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVert, stagingVertMem);

    void* data;
    vkMapMemory(logicalDevice, stagingVertMem, 0, vertexSize, 0, &data);
    memcpy(data, terrainVertices.data(), vertexSize);
    vkUnmapMemory(logicalDevice, stagingVertMem);

    VkBuffer stagingInd; VkDeviceMemory stagingIndMem;
    CreateBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingInd, stagingIndMem);

    vkMapMemory(logicalDevice, stagingIndMem, 0, indexSize, 0, &data);
    memcpy(data, indices.data(), indexSize);
    vkUnmapMemory(logicalDevice, stagingIndMem);

    // 5. Execute direct device transfer pipeline, overwriting the front region (offset 0)
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferCopy vertCopyRegion{ 0, 0, vertexSize }; // Dst offset 0
    vkCmdCopyBuffer(commandBuffer, stagingVert, globalVertexBuffer, 1, &vertCopyRegion);

    VkBufferCopy indCopyRegion{ 0, 0, indexSize };   // Dst offset 0
    vkCmdCopyBuffer(commandBuffer, stagingInd, globalIndexBuffer, 1, &indCopyRegion);

    EndSingleTimeCommands(commandBuffer);

    // 6. Clean up staging resources immediately
    vkDestroyBuffer(logicalDevice, stagingVert, nullptr);
    vkFreeMemory(logicalDevice, stagingVertMem, nullptr);
    vkDestroyBuffer(logicalDevice, stagingInd, nullptr);
    vkFreeMemory(logicalDevice, stagingIndMem, nullptr);
}
void VulkanRenderer::CreateImGuiDescriptorPool() {
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
        { VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 0;
    for (VkDescriptorPoolSize& pool_size : pool_sizes) {
        pool_info.maxSets += pool_size.descriptorCount;
    }
    pool_info.poolSizeCount = static_cast<uint32_t>(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(logicalDevice, &pool_info, nullptr, &imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool.");
    }
}

void VulkanRenderer::CreateImGui() {
    // 1. Create Pool (Ensure this isn't done twice!)
    CreateImGuiDescriptorPool();

    // 2. Setup Context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);

    // 3. Init Backend matching your modern version layout
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = logicalDevice;
    init_info.QueueFamily = FindQueueFamilies(physicalDevice).graphicsFamily.value();
    init_info.Queue = graphicsQueue;
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = imguiDescriptorPool;
    init_info.MinImageCount = 2;
    init_info.ImageCount = static_cast<uint32_t>(swapChainImages.size());
    init_info.Allocator = nullptr;

    // --- THIS IS THE FIX FOR THE RENDERPASS ERROR ---
    init_info.PipelineInfoMain.RenderPass = renderPass;
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // Only takes ONE parameter now
    ImGui_ImplVulkan_Init(&init_info);
}

VkCommandBuffer VulkanRenderer::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(logicalDevice, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanRenderer::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue); // Wait for the font upload to finish

    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
}

void VulkanRenderer::DrawGUI() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    uint32_t totalVertices = 0;
    uint32_t totalIndices = 0;

    totalVertices += static_cast<uint32_t>(globalVertices.size());
    totalIndices += terrainIndexCount;

    for (const auto& sub : globalSubMeshes) {
        totalIndices += sub.indexCount;
    }

    totalVertices = static_cast<uint32_t>(globalVertices.size());

    uint32_t triangleCount = totalIndices / 3;
    uint32_t texturesLoaded = static_cast<uint32_t>(globalTextureRegistry.size());

    // 2. Render the custom styled overlay window block
    // Assuming a standard width configuration window position (e.g. 1280 wide screen)
    ImGui::SetNextWindowPos(ImVec2(1280 - 180, 10), ImGuiCond_Always);
    ImGui::Begin("Stats", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);

    // PERFORMANCE SECTION
    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "PERFORMANCE");
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Text("Ms/Frame: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

    ImGui::Separator();

    // GEOMETRY SECTION
    ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "GEOMETRY");
    ImGui::Text("Triangles: %u", triangleCount);
    ImGui::Text("Vertices:  %u", totalVertices);
    ImGui::Text("Indices:   %u", totalIndices);

    ImGui::Separator();

    // PIPELINE SECTION
    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "PIPELINE");
    ImGui::Text("Draw Calls: %-8u", drawCallCount);
    ImGui::Text("Textures:   %-8u", texturesLoaded); // Added texture count matching your interest
    ImGui::Text("Culled:   %-8u", culledCount);

    ImGui::End();

    ImGui::Render();
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type allocation properties!");
}

namespace std {
    template<> struct hash<ModelVertex> {
        size_t operator()(ModelVertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^
                (hash<glm::vec3>()(vertex.normal) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.texCoord) << 1);
        }
    };
}

void VulkanRenderer::ParseObjFileByMaterial(const std::string& filepath,
    std::vector<std::vector<ModelVertex>>& verticesPerMaterial,
    std::vector<std::vector<uint32_t>>& indicesPerMaterial,
    std::vector<std::string>& textureFilenames) {

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    std::string baseDir = "";
    size_t lastSlash = filepath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        baseDir = filepath.substr(0, lastSlash + 1);
    }

    bool result = tinyobj::LoadObj(
        &attrib, &shapes, &materials, &err,
        filepath.c_str(),
        baseDir.empty() ? nullptr : baseDir.c_str(),
        true
    );

    if (!result) {
        throw std::runtime_error("[Asset Error] Failed to parse .obj file structure: " + err);
    }

    // One bucket per material, plus one extra bucket at the end for "no material assigned"
    size_t materialCount = materials.size();
    size_t noMaterialIndex = materialCount; // faces with material_id == -1 go here

    verticesPerMaterial.resize(materialCount + 1);
    indicesPerMaterial.resize(materialCount + 1);
    textureFilenames.resize(materialCount + 1, "");

    for (size_t m = 0; m < materialCount; m++) {
        textureFilenames[m] = materials[m].diffuse_texname.empty()
            ? ""
            : baseDir + materials[m].diffuse_texname;
    }
    // Last bucket (no material) stays with an empty texture path -> falls back to defaultTexture

    std::vector<std::unordered_map<ModelVertex, uint32_t>> uniqueVerticesPerMaterial(materialCount + 1);

    for (const auto& shape : shapes) {
        for (size_t f = 0; f < shape.mesh.indices.size() / 3; f++) {
            int rawMaterialId = shape.mesh.material_ids[f];
            size_t materialId = (rawMaterialId >= 0) ? static_cast<size_t>(rawMaterialId) : noMaterialIndex;

            for (size_t v = 0; v < 3; v++) {
                tinyobj::index_t index = shape.mesh.indices[3 * f + v];

                ModelVertex vertex{};
                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                if (index.normal_index >= 0) {
                    vertex.normal = {
                        attrib.normals[3 * index.normal_index + 0],
                        attrib.normals[3 * index.normal_index + 1],
                        attrib.normals[3 * index.normal_index + 2]
                    };
                }

                if (index.texcoord_index >= 0) {
                    vertex.texCoord = {
                        attrib.texcoords[2 * index.texcoord_index + 0],
                        1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                    };
                }

                auto& uniqueVertices = uniqueVerticesPerMaterial[materialId];
                auto& vertices = verticesPerMaterial[materialId];
                auto& indices = indicesPerMaterial[materialId];

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
                    vertices.push_back(vertex);
                }
                indices.push_back(uniqueVertices[vertex]);
            }
        }
    }

    std::cout << "[Asset Manager] '" << filepath << "' split into " << materials.size()
        << " materials (+ possible untextured group)\n";
}

Texture& VulkanRenderer::GetOrLoadTexture(const std::string& path) {
    // 1. Check if the texture path has already been processed and indexed
    auto it = textureToIdMap.find(path);
    if (it != textureToIdMap.end()) {
        // Return a stable reference directly out of the persistent global array
        return globalTextureRegistry[it->second];
    }

    // 2. Attempt to create and load the new asset image structures
    Texture tex{};
    bool loadSuccessful = false;

    try {
        CreateTextureImage(path, tex);
        loadSuccessful = true;
    }
    catch (const std::exception& e) {
        std::cerr << "[Texture Warning] Failed to load '" << path << "': " << e.what()
            << " -- falling back to default texture.\n";
    }

    // 3. Handle fallback assignment if loading fails or file path is empty
    if (!loadSuccessful) {
        // If the path failed but default texture is already loaded, map the default slot
        if (textureToIdMap.find("default") != textureToIdMap.end()) {
            uint32_t defaultId = textureToIdMap["default"];
            textureToIdMap[path] = defaultId;
            return globalTextureRegistry[defaultId];
        }

        // Safety Fallback: Just return your defaultTexture object copy
        return defaultTexture;
    }

    // 4. Register the successfully loaded asset into the bindless track indices
    globalTextureRegistry.push_back(tex);
    uint32_t newTextureId = static_cast<uint32_t>(globalTextureRegistry.size() - 1);
    textureToIdMap[path] = newTextureId;

    return globalTextureRegistry[newTextureId];
}

void VulkanRenderer::LoadModelAsset(const std::string& filename, glm::vec3 position, float scale, const std::string& texturePath) {
    std::vector<std::vector<ModelVertex>> verticesPerMaterial;
    std::vector<std::vector<uint32_t>> indicesPerMaterial;
    std::vector<std::string> textureFilenames;

    ParseObjFileByMaterial(filename, verticesPerMaterial, indicesPerMaterial, textureFilenames);

    SceneObject obj{};

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::scale(model, glm::vec3(scale));
    obj.modelMatrix = model;
    obj.objectId = static_cast<uint32_t>(sceneObjects.size()) + 1;
    obj.firstSubMesh = static_cast<uint32_t>(globalSubMeshes.size());

    for (size_t m = 0; m < verticesPerMaterial.size(); m++) {
        auto& vertices = verticesPerMaterial[m];
        auto& indices = indicesPerMaterial[m];

        glm::vec3 minBound(FLT_MAX), maxBound(-FLT_MAX);
        for (const auto& v : vertices) {
            minBound = glm::min(minBound, v.pos);
            maxBound = glm::max(maxBound, v.pos);
        }
        glm::vec3 center = (minBound + maxBound) * 0.5f;
        float radius = glm::length(maxBound - center);

        if (indices.empty()) continue;

        // Flip winding to match your pipeline's frontFace convention
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            std::swap(indices[i + 1], indices[i + 2]);
        }

        // CALCULATE GLOBAL OFFSETS BEFORE APPENDING
        int32_t currentVertexOffset = static_cast<int32_t>(globalVertices.size());
        uint32_t currentFirstIndex = static_cast<uint32_t>(globalIndices.size());

        // Memory Bounds Check: Prevent spilling past pre-allocated giant monolithic buffer layouts
        if ((globalVertices.size() + vertices.size() > MAX_GLOBAL_VERTICES) ||
            (globalIndices.size() + indices.size() > MAX_GLOBAL_INDICES)) {
            throw std::runtime_error("[Memory Manager] Loading this asset exceeds pre-allocated monolithic buffer bounds!");
        }

        SubMesh sub{};
        sub.indexCount = static_cast<uint32_t>(indices.size());
        sub.firstIndex = currentFirstIndex;
        sub.vertexOffset = currentVertexOffset;
        sub.boundingCenterLocal = center;
        sub.boundingRadiusLocal = radius;

        // Append submesh data straight into the continuous global master lists
        globalVertices.insert(globalVertices.end(), vertices.begin(), vertices.end());
        globalIndices.insert(globalIndices.end(), indices.begin(), indices.end());

        // STAGING & STREAMING SUB-BUFFER TO GPU
        VkDeviceSize vertexSize = sizeof(ModelVertex) * vertices.size();
        VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

        // Staging Vertex Buffer
        VkBuffer stagingVert; VkDeviceMemory stagingVertMem;
        CreateBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingVert, stagingVertMem);
        void* vertData;
        vkMapMemory(logicalDevice, stagingVertMem, 0, vertexSize, 0, &vertData);
        memcpy(vertData, vertices.data(), vertexSize);
        vkUnmapMemory(logicalDevice, stagingVertMem);

        // Staging Index Buffer
        VkBuffer stagingInd; VkDeviceMemory stagingIndMem;
        CreateBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingInd, stagingIndMem);
        void* indData;
        vkMapMemory(logicalDevice, stagingIndMem, 0, indexSize, 0, &indData);
        memcpy(indData, indices.data(), indexSize);
        vkUnmapMemory(logicalDevice, stagingIndMem);

        // COPY ONLY TO THE SPECIFIC SUB-REGIONS WITHIN THE GLOBAL LARGE BUFFERS
        VkDeviceSize vertDestOffset = sizeof(ModelVertex) * currentVertexOffset;
        VkDeviceSize indDestOffset = sizeof(uint32_t) * currentFirstIndex;

        VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

        VkBufferCopy vertCopyRegion{};
        vertCopyRegion.srcOffset = 0;
        vertCopyRegion.dstOffset = vertDestOffset;
        vertCopyRegion.size = vertexSize;
        vkCmdCopyBuffer(commandBuffer, stagingVert, globalVertexBuffer, 1, &vertCopyRegion);

        VkBufferCopy indCopyRegion{};
        indCopyRegion.srcOffset = 0;
        indCopyRegion.dstOffset = indDestOffset;
        indCopyRegion.size = indexSize;
        vkCmdCopyBuffer(commandBuffer, stagingInd, globalIndexBuffer, 1, &indCopyRegion);

        EndSingleTimeCommands(commandBuffer);

        vkDestroyBuffer(logicalDevice, stagingVert, nullptr);
        vkFreeMemory(logicalDevice, stagingVertMem, nullptr);
        vkDestroyBuffer(logicalDevice, stagingInd, nullptr);
        vkFreeMemory(logicalDevice, stagingIndMem, nullptr);

        // Bindless Texture Index Resolution Routine
        std::string texPath = !texturePath.empty() ? texturePath : textureFilenames[m];
        uint32_t targetTextureId = 0;

        if (!texPath.empty()) {
            // Check if the texture is already registered anywhere in the system
            auto it = textureToIdMap.find(texPath);
            if (it != textureToIdMap.end()) {
                targetTextureId = it->second;
            }
            else {
                GetOrLoadTexture(texPath);

                targetTextureId = textureToIdMap[texPath];

                Texture& finalTexture = globalTextureRegistry[targetTextureId];

                // Write the new descriptor array slot update to inform Vulkan
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = finalTexture.imageView;
                imageInfo.sampler = finalTexture.sampler;

                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = descriptorSet;
                descriptorWrite.dstBinding = 1;
                descriptorWrite.dstArrayElement = targetTextureId;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfo;    

                vkUpdateDescriptorSets(logicalDevice, 1, &descriptorWrite, 0, nullptr);
            }
        }

        sub.textureId = targetTextureId;

        obj.subMeshCount = static_cast<uint32_t>(globalSubMeshes.size()) - obj.firstSubMesh;
        globalSubMeshes.push_back(sub);
    }

    sceneObjects.push_back(obj);
    std::cout << "[Asset Manager] Unified Global Memory Buffer Allocation Complete: " << filename << "\n";
}

VkFormat VulkanRenderer::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
            return format;
        }
        else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
            return format;
        }
    }
    throw std::runtime_error("Failed to find a supported depth format.");
}

VkFormat VulkanRenderer::FindDepthFormat() {
    return FindSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
    );
}

void VulkanRenderer::CreateDepthResources() {
    VkFormat depthFormat = FindDepthFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapChainExtent.width;
    imageInfo.extent.height = swapChainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &depthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image.");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(logicalDevice, depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &depthImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth image memory.");
    }
    vkBindImageMemory(logicalDevice, depthImage, depthImageMemory, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view.");
    }
}

void VulkanRenderer::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage, destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        throw std::invalid_argument("Unsupported layout transition!");
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { width, height, 1 };

    vkCmdCopyBufferToImage(commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    EndSingleTimeCommands(commandBuffer);
}

void VulkanRenderer::CreateTextureImage(const std::string& path, Texture& texture) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("[Texture Error] Failed to load texture image: " + path);
    }

    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(logicalDevice, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(logicalDevice, stagingBufferMemory);

    stbi_image_free(pixels);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image.");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(logicalDevice, texture.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &texture.imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate texture image memory.");
    }
    vkBindImageMemory(logicalDevice, texture.image, texture.imageMemory, 0);

    TransitionImageLayout(texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(stagingBuffer, texture.image, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
    TransitionImageLayout(texture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);

    CreateTextureImageView(texture, VK_FORMAT_R8G8B8A8_SRGB);
    CreateTextureSampler(texture);
}

void VulkanRenderer::CreateTextureImageView(Texture& texture, VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &texture.imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view.");
    }
}

void VulkanRenderer::CreateTextureSampler(Texture& texture) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler.");
    }
}

void VulkanRenderer::CreateDefaultTexture() {
    uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(logicalDevice, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, whitePixel, static_cast<size_t>(imageSize));
    vkUnmapMemory(logicalDevice, stagingBufferMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 1, 1, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &defaultTexture.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create default texture image.");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(logicalDevice, defaultTexture.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &defaultTexture.imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate default texture memory.");
    }
    vkBindImageMemory(logicalDevice, defaultTexture.image, defaultTexture.imageMemory, 0);

    TransitionImageLayout(defaultTexture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    CopyBufferToImage(stagingBuffer, defaultTexture.image, 1, 1);
    TransitionImageLayout(defaultTexture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);

    CreateTextureImageView(defaultTexture, VK_FORMAT_R8G8B8A8_SRGB);
    CreateTextureSampler(defaultTexture);
}

void VulkanRenderer::UpdateFrustumPlanes(const glm::mat4& viewProj) {
    glm::mat4 m = glm::transpose(viewProj); // row-major extraction

    // Left, Right, Bottom, Top, Near, Far — standard Gribb/Hartmann plane extraction
    frustumPlanes[0].normal = glm::vec3(m[3] + m[0]); frustumPlanes[0].distance = m[3].w + m[0].w; // Left
    frustumPlanes[1].normal = glm::vec3(m[3] - m[0]); frustumPlanes[1].distance = m[3].w - m[0].w; // Right
    frustumPlanes[2].normal = glm::vec3(m[3] + m[1]); frustumPlanes[2].distance = m[3].w + m[1].w; // Bottom
    frustumPlanes[3].normal = glm::vec3(m[3] - m[1]); frustumPlanes[3].distance = m[3].w - m[1].w; // Top
    frustumPlanes[4].normal = glm::vec3(m[3] + m[2]); frustumPlanes[4].distance = m[3].w + m[2].w; // Near
    frustumPlanes[5].normal = glm::vec3(m[3] - m[2]); frustumPlanes[5].distance = m[3].w - m[2].w; // Far

    for (auto& plane : frustumPlanes) {
        float length = glm::length(plane.normal);
        plane.normal /= length;
        plane.distance /= length;
    }
}

bool VulkanRenderer::IsSphereInFrustum(const glm::vec3& center, float radius) const {
    for (const auto& plane : frustumPlanes) {
        if (glm::dot(plane.normal, center) + plane.distance + radius < 0.0f) {
            return false; // Fully outside this plane
        }
    }
    return true;
}