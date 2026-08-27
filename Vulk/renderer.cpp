#include "renderer.h"
#include "input.h"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <set>
#include <unordered_map>
#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/gtx/norm.hpp>

#include "tiny_obj_loader.h"

#include <algorithm>
#include <deque>
#include <unordered_set>

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}
void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr) func(instance, debugMessenger, pAllocator);
}
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserVoid) {
	if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
		std::cerr << "[VULKAN VALIDATION]: " << pCallbackData->pMessage << "\n\n";
	}
	return VK_FALSE;
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
std::vector<const char*> VulkanRenderer::GetRequiredExtensions() {
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
	std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
	if (enableValidationLayers) {
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}
	return extensions;
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
	if (g_Settings.vsync) {
		// Prefer FIFO (V-Sync on)
		return VK_PRESENT_MODE_FIFO_KHR;
	}
	else {
		// Prefer Mailbox or Immediate
		for (const auto& mode : availablePresentModes) {
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR) return mode;
		}
		return VK_PRESENT_MODE_IMMEDIATE_KHR;
	}
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
VkShaderModule VulkanRenderer::CreateShaderModule(VkDevice device, const std::vector<char>& code) {
	// same as before, but now takes device as parameter
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("Critical Failure: Failed to map graphics shader module bytecode allocation.");
	}
	return shaderModule;
}
uint32_t VulkanRenderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	uint64_t key = (static_cast<uint64_t>(typeFilter) << 32) | properties;
	auto it = m_memoryTypeCache.find(key);
	if (it != m_memoryTypeCache.end()) return it->second;

	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			m_memoryTypeCache[key] = i;
			return i;
		}
	}
	throw std::runtime_error("Failed to find suitable memory type for GPU buffer.");
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
VkSampleCountFlagBits VulkanRenderer::GetMaxUsableSampleCount() {
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice, &props);

	VkSampleCountFlags counts = props.limits.framebufferColorSampleCounts &
		props.limits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
	if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
	if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
	if (counts & VK_SAMPLE_COUNT_8_BIT)  return VK_SAMPLE_COUNT_8_BIT;
	if (counts & VK_SAMPLE_COUNT_4_BIT)  return VK_SAMPLE_COUNT_4_BIT;
	if (counts & m_currentMsaaSamples)  return m_currentMsaaSamples;
	return VK_SAMPLE_COUNT_1_BIT;
}
void VulkanRenderer::DeferBufferDeletion(BufferDeletion&& del) {
	m_pendingDeletionsGlobal.Push(std::move(del), m_globalFrameCounter + MAX_FRAMES_IN_FLIGHT);
}
VkSampleCountFlagBits VulkanRenderer::IntToSampleCount(int samples) {
	switch (samples) {
	case 1:  return VK_SAMPLE_COUNT_1_BIT;
	case 2:  return m_currentMsaaSamples;
	case 4:  return VK_SAMPLE_COUNT_4_BIT;
	case 8:  return VK_SAMPLE_COUNT_8_BIT;
	case 16: return VK_SAMPLE_COUNT_16_BIT;
	case 32: return VK_SAMPLE_COUNT_32_BIT;
	case 64: return VK_SAMPLE_COUNT_64_BIT;
	default: return VK_SAMPLE_COUNT_1_BIT;
	}
}

bool VulkanRenderer::ShouldClose() { return glfwWindowShouldClose(window); }
void VulkanRenderer::PollEvents() { glfwPollEvents(); }

void VulkanRenderer::Initialize(int width, int height, const std::string& title) {
	InitWindow(width, height, title);
	InitVulkan();
}
void VulkanRenderer::InitWindow(int width, int height, const std::string& title) {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Prevents GLFW from loading OpenGL contexts
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);   // Locks window bounds initially

	windowedWidth = width;
	windowedHeight = height;

	if (g_Settings.fullscreen) {
		GLFWmonitor* monitor = glfwGetPrimaryMonitor();
		const GLFWvidmode* mode = glfwGetVideoMode(monitor);
		window = glfwCreateWindow(mode->width, mode->height, title.c_str(), monitor, nullptr);
		// Update settings to match actual monitor resolution
		g_Settings.windowWidth = mode->width;
		g_Settings.windowHeight = mode->height;
	}
	else {
		window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
		// Store initial position
		glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
	}
}
void VulkanRenderer::InitVulkan() {
	CreateInstance();
	SetupDebugMessenger();
	CreateSurface();
	PickPhysicalDevice();
	CreateLogicalDevice();

	if (g_Settings.msaaSamples == 0) {
		m_currentMsaaSamples = GetMaxUsableSampleCount();
	}
	else {
		m_currentMsaaSamples = IntToSampleCount(g_Settings.msaaSamples);
		VkSampleCountFlagBits maxSupported = GetMaxUsableSampleCount();
		if (static_cast<int>(m_currentMsaaSamples) > static_cast<int>(maxSupported)) {
			m_currentMsaaSamples = maxSupported;
			g_Settings.msaaSamples = static_cast<int>(m_currentMsaaSamples);
		}
	}

	CreateSwapChain();
	CreateImageViews();
	CreateOffscreenResolve();
	CreateDepthResources();
	CreateColorResources();
	CreateRenderPass();
	CreateCompositionPass();
	CreateFrameBuffers();

	CreateCommandPool();
	CreateCommandBuffers();
	CreateSyncObjects();

	m_uploader.Init(this, 32 * 1024 * 1024);

	CreateDescriptorSetLayout();

	CreateUniformBuffer();
	CreateLightBuffer();

	CreateDescriptorPool();
	CreateDescriptorSet();
	CreateImGui();

	g_AssetManager.SetRenderer(this);
	g_AssetManager.SetDescriptorSet(descriptorSet);
	g_AssetManager.CreateDefaultTexture();
	g_AssetManager.CreateDefaultNormalTexture();

	CreateGraphicsPipeline();
	CreateCompositionPipeline();

	m_staticMeshRenderer.Init(logicalDevice, this);

	m_skybox.Init(logicalDevice, renderPass, descriptorSetLayout, m_currentMsaaSamples);
	m_skybox.LoadTexture();
	m_skybox.UpdateDescriptor(logicalDevice, descriptorSet);

	m_waterRenderer.Init(logicalDevice, renderPass, descriptorSetLayout, m_currentMsaaSamples, this);

	m_terrainRenderer.Init(logicalDevice, this, &m_uploader, 5'000'000, 10'000'000);

	m_grassRenderer.Init(logicalDevice, this, &m_uploader, renderPass, descriptorSetLayout, m_currentMsaaSamples);

	currentSettings = g_Settings;
}

void VulkanRenderer::CreateInstance() {
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Vulk";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Vulk Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	// Setup debug messenger (validation layers only)
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	debugCreateInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugCreateInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugCreateInfo.pfnUserCallback = DebugCallback;
	debugCreateInfo.pNext = nullptr;   // No validation features

	// Build instance create info
	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	auto extensions = GetRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	if (enableValidationLayers) {
		createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
		createInfo.ppEnabledLayerNames = validationLayers.data();
		createInfo.pNext = &debugCreateInfo;   // only debug messenger
	}
	else {
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}

	if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
		throw std::runtime_error("Critical Failure: Unable to build raw Vulkan software runtime instance.");
	}
}
void VulkanRenderer::CreateSurface() {
	// Uses GLFW library context to handle drawing interfaces natively for Windows/Linux platforms
	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
		throw std::runtime_error("Critical Failure: Platform window surface generation failed.");
	}
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
	indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

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

void VulkanRenderer::CreateSwapChain() {
	SwapChainSupportDetails swapChainSupport = QuerySwapChainSupport(physicalDevice);

	VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);
	if (extent.width == 0 || extent.height == 0) {
		extent.width = 1280;
		extent.height = 720;
	}

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
void VulkanRenderer::RecreateSwapChain() {
	// 1. Wait for the GPU to finish all work
	vkDeviceWaitIdle(logicalDevice);

	// 2. Destroy old swapchain-dependent resources
	// (depth resources, framebuffers, image views, swapchain itself)

	// Destroy framebuffers
	for (auto framebuffer : swapChainFramebuffers) {
		vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
	}
	swapChainFramebuffers.clear();

	// Destroy image views
	for (auto imageView : swapChainImageViews) {
		vkDestroyImageView(logicalDevice, imageView, nullptr);
	}
	swapChainImageViews.clear();

	// Destroy depth resources
	if (depthImageView != VK_NULL_HANDLE) {
		vkDestroyImageView(logicalDevice, depthImageView, nullptr);
		depthImageView = VK_NULL_HANDLE;
	}
	if (depthImage != VK_NULL_HANDLE) {
		vkDestroyImage(logicalDevice, depthImage, nullptr);
		depthImage = VK_NULL_HANDLE;
	}
	if (depthImageMemory != VK_NULL_HANDLE) {
		vkFreeMemory(logicalDevice, depthImageMemory, nullptr);
		depthImageMemory = VK_NULL_HANDLE;
	}

	if (colorImageView != VK_NULL_HANDLE) vkDestroyImageView(logicalDevice, colorImageView, nullptr);
	if (colorImage != VK_NULL_HANDLE) vkDestroyImage(logicalDevice, colorImage, nullptr);
	if (colorImageMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, colorImageMemory, nullptr);

	// Destroy swapchain
	if (swapChain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(logicalDevice, swapChain, nullptr);
		swapChain = VK_NULL_HANDLE;
	}

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		if (imageAvailableSemaphores[i]) {
			vkDestroySemaphore(logicalDevice, imageAvailableSemaphores[i], nullptr);
			imageAvailableSemaphores[i] = VK_NULL_HANDLE;
		}
		if (renderFinishedSemaphores[i]) {
			vkDestroySemaphore(logicalDevice, renderFinishedSemaphores[i], nullptr);
			renderFinishedSemaphores[i] = VK_NULL_HANDLE;
		}
		if (inFlightFences[i]) {
			vkDestroyFence(logicalDevice, inFlightFences[i], nullptr);
			inFlightFences[i] = VK_NULL_HANDLE;
		}
	}

	if (offscreenFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(logicalDevice, offscreenFramebuffer, nullptr);
	if (offscreenResolveImageView != VK_NULL_HANDLE) vkDestroyImageView(logicalDevice, offscreenResolveImageView, nullptr);
	if (offscreenResolveImage != VK_NULL_HANDLE) vkDestroyImage(logicalDevice, offscreenResolveImage, nullptr);
	if (offscreenResolveImageMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, offscreenResolveImageMemory, nullptr);
	if (offscreenSampler != VK_NULL_HANDLE) vkDestroySampler(logicalDevice, offscreenSampler, nullptr);

	// 3. Recreate swapchain and its dependent resources
	CreateSwapChain();      // re-creates swapChainImages, swapChainImageFormat, swapChainExtent
	CreateImageViews();
	CreateOffscreenResolve();
	CreateColorResources();
	CreateDepthResources();
	CreateFrameBuffers();

	vkFreeCommandBuffers(logicalDevice, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
	CreateCommandBuffers(); // re-allocates commandBuffers with new framebuffers
	CreateSyncObjects();

	currentFrame = 0;

	ImGui_ImplVulkan_Shutdown();

	vkResetDescriptorPool(logicalDevice, imguiDescriptorPool, 0);

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
	init_info.PipelineInfoMain.RenderPass = compositionRenderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);

	CreateCompositionPipeline();
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
	std::vector<VkAttachmentDescription> attachments;
	VkAttachmentReference colorRef{};
	VkAttachmentReference resolveRef{};
	VkAttachmentReference depthRef{};

	if (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
		VkAttachmentDescription multisampledAttachment{};
		multisampledAttachment.format = swapChainImageFormat;
		multisampledAttachment.samples = m_currentMsaaSamples;
		multisampledAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		multisampledAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		multisampledAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		multisampledAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		multisampledAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		multisampledAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		attachments.push_back(multisampledAttachment);

		VkAttachmentDescription resolveAttachment{};
		resolveAttachment.format = swapChainImageFormat;
		resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		// NEW: Output to shader read instead of presentation!
		resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments.push_back(resolveAttachment);

		colorRef.attachment = 0;
		colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		resolveRef.attachment = 1;
		resolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	else {
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = swapChainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		// NEW: Output to shader read instead of presentation!
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		attachments.push_back(colorAttachment);

		colorRef.attachment = 0;
		colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		resolveRef.attachment = VK_ATTACHMENT_UNUSED;
	}

	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = FindDepthFormat();
	depthAttachment.samples = m_currentMsaaSamples;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments.push_back(depthAttachment);

	depthRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pResolveAttachments = (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) ? &resolveRef : nullptr;
	subpass.pDepthStencilAttachment = &depthRef;

	// Dependency to ensure it's ready for the composition pass to read
	VkSubpassDependency dependency{};
	dependency.srcSubpass = 0;
	dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create 3D render pass.");
	}
}
void VulkanRenderer::CreateFrameBuffers() {
	// 1. Create the single offscreen framebuffer (Internal Resolution)
	std::vector<VkImageView> offscreenAttachments;
	if (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
		offscreenAttachments = { colorImageView, offscreenResolveImageView, depthImageView };
	}
	else {
		offscreenAttachments = { offscreenResolveImageView, depthImageView };
	}

	VkFramebufferCreateInfo offscreenInfo{};
	offscreenInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	offscreenInfo.renderPass = renderPass; // The 3D pass
	offscreenInfo.attachmentCount = static_cast<uint32_t>(offscreenAttachments.size());
	offscreenInfo.pAttachments = offscreenAttachments.data();
	offscreenInfo.width = GetInternalWidth();
	offscreenInfo.height = GetInternalHeight();
	offscreenInfo.layers = 1;

	if (vkCreateFramebuffer(logicalDevice, &offscreenInfo, nullptr, &offscreenFramebuffer) != VK_SUCCESS)
		throw std::runtime_error("Failed to create offscreen framebuffer");

	// 2. Create the swapchain framebuffers for UI/Composition (Native Resolution)
	swapChainFramebuffers.resize(swapChainImageViews.size());
	for (size_t i = 0; i < swapChainImageViews.size(); i++) {
		VkImageView attachment[] = { swapChainImageViews[i] };
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = compositionRenderPass; // The UI pass
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = attachment;
		fbInfo.width = swapChainExtent.width;
		fbInfo.height = swapChainExtent.height;
		fbInfo.layers = 1;

		if (vkCreateFramebuffer(logicalDevice, &fbInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS)
			throw std::runtime_error("Failed to create swapchain framebuffer");
	}
}
void VulkanRenderer::CreateCommandPool() {
	QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(physicalDevice);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
	if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create command pool");
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

	imagesInFlight.assign(swapChainImages.size(), VK_NULL_HANDLE);

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

void VulkanRenderer::CreateOffscreenResolve() {
	CreateImage(GetInternalWidth(), GetInternalHeight(), 1, VK_SAMPLE_COUNT_1_BIT, swapChainImageFormat,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, offscreenResolveImage, offscreenResolveImageMemory);

	offscreenResolveImageView = CreateImageView(offscreenResolveImage, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &offscreenSampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create offscreen sampler.");
	}
}

void VulkanRenderer::CreateCompositionPass() {
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = swapChainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;

	if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &compositionRenderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create composition render pass.");
	}
}

void VulkanRenderer::CreateCompositionPipeline() {
	// Layout
	VkDescriptorSetLayoutBinding samplerBinding{};
	samplerBinding.binding = 0;
	samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerBinding.descriptorCount = 1;
	samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &samplerBinding;
	vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &compositionDescriptorSetLayout);

	// Pool
	VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;
	vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &compositionDescriptorPool);

	// Set
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = compositionDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &compositionDescriptorSetLayout;
	vkAllocateDescriptorSets(logicalDevice, &allocInfo, &compositionDescriptorSet);

	// Update Set
	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = offscreenResolveImageView;
	imageInfo.sampler = offscreenSampler;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = compositionDescriptorSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(logicalDevice, 1, &descriptorWrite, 0, nullptr);

	// Pipeline Layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &compositionDescriptorSetLayout;
	vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &compositionPipelineLayout);

	// Pipeline (No vertex buffers!)
	auto vertCode = ReadFile("shaders/quad_vert.spv");
	auto fragCode = ReadFile("shaders/quad_frag.spv");
	VkShaderModule vMod = CreateShaderModule(logicalDevice, vertCode);
	VkShaderModule fMod = CreateShaderModule(logicalDevice, fragCode);

	VkPipelineShaderStageCreateInfo stages[] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main" },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main" }
	};

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
	VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
	VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 1.0f, nullptr, VK_FALSE, VK_FALSE };
	VkPipelineColorBlendAttachmentState colorBlendAttachment{ VK_FALSE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
	VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &colorBlendAttachment };
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynamicStates };

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

	VkGraphicsPipelineCreateInfo pInfo{};
	pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pInfo.stageCount = 2;
	pInfo.pStages = stages;
	pInfo.pVertexInputState = &vertexInputInfo;
	pInfo.pInputAssemblyState = &inputAssembly;
	pInfo.pViewportState = &viewportState;
	pInfo.pRasterizationState = &rasterizer;
	pInfo.pMultisampleState = &multisampling;
	pInfo.pDepthStencilState = &depthStencil;
	pInfo.pColorBlendState = &colorBlending;
	pInfo.pDynamicState = &dynamicState;
	pInfo.layout = compositionPipelineLayout;
	pInfo.renderPass = compositionRenderPass;

	vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pInfo, nullptr, &compositionPipeline);
	vkDestroyShaderModule(logicalDevice, fMod, nullptr);
	vkDestroyShaderModule(logicalDevice, vMod, nullptr);
}

void VulkanRenderer::UpdateTextureDescriptors(const Scene& scene) {
	if (descriptorSet == VK_NULL_HANDLE) {
		std::cerr << "[Renderer] descriptorSet is null in UpdateTextureDescriptors!\n";
		return;
	}

	// Collect all unique texture paths used by the scene's instances
	std::unordered_set<std::string> uniquePaths;
	const auto& instances = scene.GetInstances();
	for (const auto& inst : instances) {
		const MeshAsset* mesh = g_AssetManager.GetMesh(inst.meshName);
		if (!mesh) continue;
		for (const auto& texPath : mesh->materialTextures) {
			if (!texPath.empty()) uniquePaths.insert(texPath);
		}
	}

	// Ensure all textures are loaded
	for (const auto& path : uniquePaths) {
		g_AssetManager.GetTexture(path);
	}

	// Write descriptor sets for all textures in the asset manager's registry
	const auto& textureMap = g_AssetManager.GetTextureMap();

	std::vector<VkWriteDescriptorSet> writes;
	std::deque<VkDescriptorImageInfo> imageInfos;

	for (const auto& pair : textureMap) {
		const std::string& path = pair.first;
		if (path == "default") continue;
		uint32_t id = pair.second;
		const Texture* tex = g_AssetManager.GetTexture(pair.first);
		if (!tex) continue;

		VkDescriptorImageInfo info{};
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.imageView = tex->imageView;
		info.sampler = tex->sampler;
		imageInfos.push_back(info);

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = descriptorSet;
		write.dstBinding = 2; // texture array binding
		write.dstArrayElement = id;
		write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		write.descriptorCount = 1;
		write.pImageInfo = &imageInfos.back();
		writes.push_back(write);
	}

	if (!writes.empty()) {
		vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}
}

void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		throw std::runtime_error("Failed to start recording graphics buffer.");
	}

	// ================================================================
	// PASS 1: OFFSCREEN 3D SCENE (Low Res)
	// ================================================================
	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = renderPass;
	renderPassInfo.framebuffer = offscreenFramebuffer;
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = { GetInternalWidth(), GetInternalHeight() };

	std::vector<VkClearValue> clearValues;
	if (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
		clearValues.resize(3);
		clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
		clearValues[1].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
		clearValues[2].depthStencil = { 1.0f, 0 };
	}
	else {
		clearValues.resize(2);
		clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
		clearValues[1].depthStencil = { 1.0f, 0 };
	}
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport{ 0.0f, 0.0f, (float)GetInternalWidth(), (float)GetInternalHeight(), 0.0f, 1.0f };
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	VkRect2D scissor{ {0, 0}, {GetInternalWidth(), GetInternalHeight()} };
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	if (currentScene != nullptr) {
		m_skybox.Draw(commandBuffer, descriptorSet);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		if (staticPipeline != VK_NULL_HANDLE && instancedPipeline != VK_NULL_HANDLE) {
			m_staticMeshRenderer.Draw(commandBuffer, pipelineLayout, descriptorSet, cameraPosition, frustumPlanes, currentFrame, staticPipeline, instancedPipeline, drawCallCount, culledCount, sceneTotalVertices, sceneTotalIndices);
		}
		if (terrainPipeline != VK_NULL_HANDLE) {
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline);
			m_terrainRenderer.Draw(commandBuffer, pipelineLayout, descriptorSet, cameraPosition, drawCallCount, culledCount, sceneTotalVertices, sceneTotalIndices);
		}
		m_waterRenderer.Draw(commandBuffer, descriptorSet, drawCallCount);
		m_grassRenderer.Draw(commandBuffer, descriptorSet, drawCallCount);
	}
	vkCmdEndRenderPass(commandBuffer);

	// ================================================================
	// PASS 2: COMPOSITION & UI (Native Res)
	// ================================================================
	VkRenderPassBeginInfo compPassInfo{};
	compPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	compPassInfo.renderPass = compositionRenderPass;
	compPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
	compPassInfo.renderArea.offset = { 0, 0 };
	compPassInfo.renderArea.extent = swapChainExtent;

	VkClearValue compClearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} };
	compPassInfo.clearValueCount = 1;
	compPassInfo.pClearValues = &compClearColor;

	vkCmdBeginRenderPass(commandBuffer, &compPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport nativeViewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
	vkCmdSetViewport(commandBuffer, 0, 1, &nativeViewport);
	VkRect2D nativeScissor{ {0, 0}, swapChainExtent };
	vkCmdSetScissor(commandBuffer, 0, 1, &nativeScissor);

	// Draw the upscaled scene!
	if (compositionPipeline != VK_NULL_HANDLE) {
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipelineLayout, 0, 1, &compositionDescriptorSet, 0, nullptr);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0); // Big triangle
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

void VulkanRenderer::UpdateScene(const Scene& scene) {
	m_staticMeshRenderer.UpdateScene(scene);

	// Handle texture updates
	if (g_AssetManager.IsTextureDirty()) {
		UpdateTextureDescriptors(scene);
		g_AssetManager.ClearTextureDirty();
	}

	// Handle light updates
	if (scene.HasModifiedLights()) {
		std::vector<Light> lights;
		lights.reserve(scene.GetLights().size());
		for (const auto& sl : scene.GetLights()) {
			lights.push_back(sl.isPoint
				? Light::Point(sl.position, sl.color, sl.intensity, sl.range)
				: Light::Directional(sl.direction, sl.color, sl.intensity));
		}
		SetLights(lights);
		scene.ClearModifiedLightsFlag();
	}

	currentScene = &scene;
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
void VulkanRenderer::DestroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) {
	if (buffer != VK_NULL_HANDLE) {
		vkDestroyBuffer(logicalDevice, buffer, nullptr);
		buffer = VK_NULL_HANDLE;
	}
	if (memory != VK_NULL_HANDLE) {
		vkFreeMemory(logicalDevice, memory, nullptr);
		memory = VK_NULL_HANDLE;
	}
}

void VulkanRenderer::CreateGraphicsPipeline() {
	std::cout << "[Renderer] Compiling Multi-Pipeline Architecture...\n";

	// 1. Create Layout Descriptor Shared Bounds (shared across both pipelines)
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(PushConstants);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to build shared pipeline uniform layout object.");
	}

	// 2. Define Shared Vertex input/assembly fixed configurations
	auto bindingDescription = ModelVertex::getBindingDescription();
	auto attributeDescriptions = ModelVertex::getAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{ 0.0f, 0.0f, (float)swapChainExtent.width, (float)swapChainExtent.height, 0.0f, 1.0f };
	VkRect2D scissor{ {0, 0}, swapChainExtent };
	VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, &viewport, 1, &scissor };

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = m_currentMsaaSamples;

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &colorBlendAttachment };

	VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO, nullptr, 0, VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS, VK_FALSE, VK_FALSE };
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynamicStates };

	// Lambda Helper to avoid massive duplicate code blocks while generating the pipelines
	auto compilePipelineHandle = [&](const std::string& vertPath, const std::string& fragPath, bool isInstanced) -> VkPipeline {
		auto vertCode = ReadFile(vertPath);
		auto fragCode = ReadFile(fragPath);
		VkShaderModule vMod = CreateShaderModule(logicalDevice, vertCode);
		VkShaderModule fMod = CreateShaderModule(logicalDevice, fragCode);

		VkPipelineShaderStageCreateInfo vStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main" };
		VkPipelineShaderStageCreateInfo fStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main" };
		VkPipelineShaderStageCreateInfo stages[] = { vStage, fStage };

		// DYNAMIC VERTEX BINDING REPLACEMENT
		auto bindingDescription = ModelVertex::getBindingDescription();
		auto attributeDescriptions = ModelVertex::getAttributeDescriptions();

		std::vector<VkVertexInputBindingDescription> bindings = { bindingDescription };
		std::vector<VkVertexInputAttributeDescription> attributes(attributeDescriptions.begin(), attributeDescriptions.end());

		if (isInstanced) {
			bindings.push_back(InstanceData::getBindingDescription());
			auto instAttrs = InstanceData::getAttributeDescriptions();
			attributes.insert(attributes.end(), instAttrs.begin(), instAttrs.end());
		}

		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
		vertexInputInfo.pVertexBindingDescriptions = bindings.data();
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
		vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

		VkGraphicsPipelineCreateInfo pInfo{};
		pInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pInfo.stageCount = 2;
		pInfo.pStages = stages;
		pInfo.pVertexInputState = &vertexInputInfo;
		pInfo.pInputAssemblyState = &inputAssembly;
		pInfo.pViewportState = &viewportState;
		pInfo.pRasterizationState = &rasterizer;
		pInfo.pMultisampleState = &multisampling;
		pInfo.pColorBlendState = &colorBlending;
		pInfo.pDepthStencilState = &depthStencil;
		pInfo.pDynamicState = &dynamicState;
		pInfo.layout = pipelineLayout;
		pInfo.renderPass = renderPass;

		VkPipeline pipeline;
		if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pInfo, nullptr, &pipeline) != VK_SUCCESS) {
			throw std::runtime_error("Failed to compile graphics sub-pipeline layout: " + vertPath);
		}
		vkDestroyShaderModule(logicalDevice, fMod, nullptr);
		vkDestroyShaderModule(logicalDevice, vMod, nullptr);
		return pipeline;
		};

	terrainPipeline = compilePipelineHandle("shaders/vert.spv", "shaders/frag.spv", false);
	staticPipeline = compilePipelineHandle("shaders/static_vert.spv", "shaders/static_frag.spv", false);

	// Ensure you add this variable to your renderer.h private section: VkPipeline instancedPipeline = VK_NULL_HANDLE;
	instancedPipeline = compilePipelineHandle("shaders/instanced_vert.spv", "shaders/static_frag.spv", true);
}
void VulkanRenderer::RecreateGraphicsPipeline() {
	if (terrainPipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(logicalDevice, terrainPipeline, nullptr);
		terrainPipeline = VK_NULL_HANDLE;
	}
	if (staticPipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(logicalDevice, staticPipeline, nullptr);
		staticPipeline = VK_NULL_HANDLE;
	}
	if (instancedPipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(logicalDevice, instancedPipeline, nullptr);
		instancedPipeline = VK_NULL_HANDLE;
	}
	if (pipelineLayout != VK_NULL_HANDLE) {
		vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);
		pipelineLayout = VK_NULL_HANDLE;
	}
	CreateGraphicsPipeline();
}

void VulkanRenderer::CreateDescriptorSetLayout() {
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding lightLayoutBinding{};
	lightLayoutBinding.binding = 1;
	lightLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	lightLayoutBinding.descriptorCount = 1;
	lightLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	lightLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding samplerLayoutBinding{};
	samplerLayoutBinding.binding = 2;
	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerLayoutBinding.descriptorCount = 500;
	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	samplerLayoutBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding normalSamplerBinding{};
	normalSamplerBinding.binding = 3;
	normalSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	normalSamplerBinding.descriptorCount = 500;
	normalSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	normalSamplerBinding.pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutBinding cubemapBinding{};
	cubemapBinding.binding = 4;
	cubemapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	cubemapBinding.descriptorCount = 1;
	cubemapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	cubemapBinding.pImmutableSamplers = nullptr;

	std::array<VkDescriptorSetLayoutBinding, 5> bindings = {
		uboLayoutBinding, lightLayoutBinding, samplerLayoutBinding, normalSamplerBinding, cubemapBinding
	};

	// index 0 -> binding 0 (UBO): no flags
	// index 1 -> binding 1 (light SSBO): no flags (fixed-size, not variable/update-after-bind)
	// index 2 -> binding 2 (sampler array): variable count + update-after-bind, since it's LAST
	VkDescriptorBindingFlags bindingFlags[5] = {
	0,
	0,
	VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
	VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
	0
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo layoutBindingFlags{};
	layoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	layoutBindingFlags.bindingCount = 5;
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
	VkDescriptorPoolSize poolSizes[4]{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = 1;

	poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[1].descriptorCount = 1;

	poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[2].descriptorCount = 1000;

	poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[3].descriptorCount = 1001;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
	poolInfo.poolSizeCount = 4;
	poolInfo.pPoolSizes = poolSizes;
	poolInfo.maxSets = 1;

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

	VkDescriptorBufferInfo lightBufferInfo{};
	lightBufferInfo.buffer = lightBuffer;
	lightBufferInfo.offset = 0;
	lightBufferInfo.range = sizeof(Light) * MAX_LIGHTS;

	VkWriteDescriptorSet lightWrite{};
	lightWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	lightWrite.dstSet = descriptorSet;
	lightWrite.dstBinding = 1;
	lightWrite.dstArrayElement = 0;
	lightWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	lightWrite.descriptorCount = 1;
	lightWrite.pBufferInfo = &lightBufferInfo;

	std::vector<VkWriteDescriptorSet> writes = { descriptorWrite, lightWrite };

	// Binding 4 (skybox cubemap) is written later by
	// SkyboxRenderer::UpdateDescriptor, once the cubemap texture has
	// actually been loaded - it doesn't exist yet at this point in Init.
	vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void VulkanRenderer::CreateUniformBuffer() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	// Create the buffer
	CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		uniformBuffer, uniformBufferMemory);

	if (vkMapMemory(logicalDevice, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped) != VK_SUCCESS) {
		throw std::runtime_error("Failed to persistently map Uniform Buffer memory.");
	}
}
void VulkanRenderer::UpdateUniformBuffer(const CameraData& cam) {
	UniformBufferObject ubo{};

	cameraPosition = cam.pos;

	ubo.view = glm::lookAt(cam.pos, cam.pos + cam.front, cam.up);
	ubo.proj = glm::perspective(glm::radians(45.0f),
		swapChainExtent.width / (float)swapChainExtent.height,
		0.1f,
		g_Settings.renderDistance);
	ubo.proj[1][1] *= -1;

	ubo.ambient = 0.25f;                // brighter ambient
	ubo.specularPower = 8.0f;           // softer highlights
	ubo.lightCount = static_cast<uint32_t>(currentLights.size());

	ubo.cameraPos = cam.pos;
	
	ubo.fogStart = g_Settings.fogStart;
	ubo.fogEnd = g_Settings.fogEnd;
	ubo.fadeParams = glm::vec4(
		g_Settings.staticFadeStart,   // .x (Read by static.frag)
		g_Settings.staticFadeEnd,     // .y (Read by static.frag)
		g_Settings.grassFadeStart,  // .z (Read by grass.frag)
		g_Settings.grassFadeEnd     // .w (Read by grass.frag)
	);

	ubo.screenSize = glm::vec2((float)swapChainExtent.width, (float)swapChainExtent.height);

	ubo.inverseViewProj = glm::inverse(ubo.proj * ubo.view);
	ubo.inverseProj = glm::inverse(ubo.proj);
	ubo.inverseView = glm::inverse(ubo.view);

	UpdateFrustumPlanes(ubo.proj * ubo.view);

	memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
}

void VulkanRenderer::CreateLightBuffer() {
	VkDeviceSize bufferSize = sizeof(Light) * MAX_LIGHTS;
	CreateBuffer(bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		lightBuffer, lightBufferMemory);

	if (vkMapMemory(logicalDevice, lightBufferMemory, 0, bufferSize, 0, &lightBufferMapped) != VK_SUCCESS) {
		throw std::runtime_error("Failed to persistently map Light Buffer memory.");
	}
}
void VulkanRenderer::SetLights(const std::vector<Light>& lights) {
	currentLights = lights;

	if (currentLights.size() > MAX_LIGHTS) {
		currentLights.resize(MAX_LIGHTS); // or log a warning
	}

	if (!currentLights.empty()) {
		memcpy(lightBufferMapped, currentLights.data(), sizeof(Light) * currentLights.size());
	}
}

void VulkanRenderer::CreateImGuiDescriptorPool() {
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

	init_info.PipelineInfoMain.RenderPass = compositionRenderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);
}
void VulkanRenderer::DrawGUI() {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	uint32_t triangleCount = sceneTotalIndices / 3;
	uint32_t texturesLoaded = static_cast<uint32_t>(g_AssetManager.GetTextureRegistry().size());

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
	ImGui::Text("Vertices:  %u", sceneTotalVertices);
	ImGui::Text("Indices:   %u", sceneTotalIndices);

	ImGui::Separator();

	// PIPELINE SECTION
	ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "PIPELINE");
	ImGui::Text("Draw Calls: %-8u", drawCallCount);
	ImGui::Text("Culled:     %-8u", culledCount);
	ImGui::Text("Textures:   %-8u", texturesLoaded);

	ImGui::End();

	ImGui::Render();
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

void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels,
	VkSampleCountFlagBits numSamples, VkFormat format,
	VkImageTiling tiling, VkImageUsageFlags usage,
	VkMemoryPropertyFlags properties, VkImage& image,
	VkDeviceMemory& imageMemory) {
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.samples = numSamples;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &image) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create image!");
	}

	VkMemoryRequirements memRequirements;
	vkGetImageMemoryRequirements(logicalDevice, image, &memRequirements);

	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

	if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate image memory!");
	}

	vkBindImageMemory(logicalDevice, image, imageMemory, 0);
}

VkImageView VulkanRenderer::CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) {
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create texture image view!");
	}

	return imageView;
}

void VulkanRenderer::CreateDepthResources() {
	VkFormat depthFormat = FindDepthFormat();
	// NOW USES INTERNAL RESOLUTION
	CreateImage(GetInternalWidth(), GetInternalHeight(), 1, m_currentMsaaSamples, depthFormat,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
	depthImageView = CreateImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}
void VulkanRenderer::CreateColorResources() {
	if (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
		VkFormat colorFormat = swapChainImageFormat;
		// NOW USES INTERNAL RESOLUTION
		CreateImage(GetInternalWidth(), GetInternalHeight(), 1, m_currentMsaaSamples, colorFormat,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory);
		colorImageView = CreateImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}
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

void VulkanRenderer::AddTerrainChunk(int64_t key, int cx, int cz, int lod,
	const std::vector<ModelVertex>& vertices,
	const std::vector<uint32_t>& indices)
{
	m_terrainRenderer.AddTerrainChunk(key, cx, cz, lod, vertices, indices);
}
void VulkanRenderer::RemoveTerrainChunk(int64_t key) {
	m_terrainRenderer.RemoveTerrainChunk(key);
}
void VulkanRenderer::SetTerrainChunkSize(float size) {
	m_terrainRenderer.SetChunkSize(size);
}
void VulkanRenderer::ClearHeightCache() {
	m_terrainRenderer.ClearHeightCache();
}

void VulkanRenderer::AddWaterBodyForChunk(int64_t chunkKey, const WaterMesh& mesh,
	const std::string& normalMapPath,
	float tiling, float waveStrength) {
	m_waterRenderer.AddWaterBodyForChunk(chunkKey, mesh, normalMapPath, tiling, waveStrength);
}
void VulkanRenderer::AddWaterBody(const WaterMesh& mesh, const std::string& normalMapPath,
	float tiling, float waveStrength) {
	m_waterRenderer.AddWaterBody(mesh, normalMapPath, tiling, waveStrength);
}
void VulkanRenderer::RemoveWaterBody(int64_t chunkKey) {
	m_waterRenderer.RemoveWaterBody(chunkKey);
}

void VulkanRenderer::AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances) {
	m_grassRenderer.AddGrass(key, grassInstances);
}

void VulkanRenderer::ApplySettings() {
	bool needSwapchainRecreate = false;

	if (g_Settings.vsync != currentSettings.vsync) {
		currentSettings.vsync = g_Settings.vsync;
		needSwapchainRecreate = true;
	}

	if (g_Settings.fullscreen != currentSettings.fullscreen) {
		currentSettings.fullscreen = g_Settings.fullscreen;
		needSwapchainRecreate = true;
	}

	if (g_Settings.msaaSamples != currentSettings.msaaSamples) {
		// Clamp and convert
		VkSampleCountFlagBits newSamples = IntToSampleCount(g_Settings.msaaSamples);
		VkSampleCountFlagBits maxSupported = GetMaxUsableSampleCount();
		if (static_cast<int>(newSamples) > static_cast<int>(maxSupported))
			newSamples = maxSupported;

		if (newSamples != m_currentMsaaSamples) {
			m_currentMsaaSamples = newSamples;
			currentSettings.msaaSamples = g_Settings.msaaSamples;
			needSwapchainRecreate = true;
		}
	}

	if (!g_Settings.fullscreen) {
		int currentWidth, currentHeight;
		glfwGetWindowSize(window, &currentWidth, &currentHeight);
		if (currentWidth != g_Settings.windowWidth || currentHeight != g_Settings.windowHeight) {
			glfwSetWindowSize(window, g_Settings.windowWidth, g_Settings.windowHeight);
			needSwapchainRecreate = true;
		}
	}

	if (needSwapchainRecreate) {
		RecreateSwapChain();
	}

	currentSettings.anisotropicFiltering = g_Settings.anisotropicFiltering;
	currentSettings.maxAnisotropy = g_Settings.maxAnisotropy;
	currentSettings.renderDistance = g_Settings.renderDistance;
	currentSettings.showStats = g_Settings.showStats;
}
void VulkanRenderer::ToggleFullscreen() {
	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* mode = glfwGetVideoMode(monitor);

	if (g_Settings.fullscreen) {
		// Going fullscreen: store current window position and size
		glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
		glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
		glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
		// Update settings to match actual monitor size
		g_Settings.windowWidth = mode->width;
		g_Settings.windowHeight = mode->height;
	}
	else {
		// Going windowed: restore previous size and position
		glfwSetWindowMonitor(window, nullptr, windowedPosX, windowedPosY, windowedWidth, windowedHeight, 0);
		g_Settings.windowWidth = windowedWidth;
		g_Settings.windowHeight = windowedHeight;
	}
}
void VulkanRenderer::SetFogParams(float start, float end) {
	m_fogStart = start;
	m_fogEnd = end;
}

void VulkanRenderer::DrawFrame() {
	ApplySettings();

	DrawGUI();

	drawCallCount = 0;
	sceneTotalVertices = 0;
	sceneTotalIndices = 0;
	culledCount = 0;

	m_globalFrameCounter++;

	m_uploader.Tick(m_globalFrameCounter);

	m_pendingDeletionsGlobal.Flush(m_globalFrameCounter, [&](BufferDeletion& del) {
		// buffers[i]/memories[i] are always pushed as matched pairs (see
		// BufferDeletion push sites) — DestroyBuffer already does the
		// null-check-and-null-out, no need to duplicate that here.
		for (size_t i = 0; i < del.buffers.size(); ++i) {
			DestroyBuffer(del.buffers[i], del.memories[i]);
		}
		});

	m_terrainRenderer.Tick(m_globalFrameCounter);
	m_grassRenderer.Tick(m_globalFrameCounter);

	vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

	uint32_t imageIndex;
	VkResult acquireResult = vkAcquireNextImageKHR(logicalDevice, swapChain, UINT64_MAX,
		imageAvailableSemaphores[currentFrame],
		VK_NULL_HANDLE, &imageIndex);

	if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR) {
		RecreateSwapChain();
		return; // skip this frame
	}
	else if (acquireResult != VK_SUCCESS) {
		throw std::runtime_error("Failed to acquire swapchain image.");
	}

	if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
		vkWaitForFences(logicalDevice, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
	}

	imagesInFlight[imageIndex] = inFlightFences[currentFrame];

	vkResetFences(logicalDevice, 1, &inFlightFences[currentFrame]);

	vkResetCommandBuffer(commandBuffers[imageIndex], 0);
	RecordCommandBuffer(commandBuffers[imageIndex], imageIndex);

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

	if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
		throw std::runtime_error("Failed to submit draw command buffer.");
	}

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;
	VkSwapchainKHR swapChains[] = { swapChain };
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	VkResult presentResult = vkQueuePresentKHR(presentQueue, &presentInfo);

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
		RecreateSwapChain();
		return; // skip advancing frame
	}
	else if (presentResult != VK_SUCCESS) {
		throw std::runtime_error("Failed to present swapchain image.");
	}

	// Advance to next frame
	currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::Cleanup() {
	m_isShuttingDown = true;

	// Wait for the GPU to finish all outstanding operations before tearing resources down
	if (logicalDevice != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(logicalDevice);
	}

	// 1. Clean up ImGui Vulkan resources
	if (imguiDescriptorPool != VK_NULL_HANDLE) {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(logicalDevice, imguiDescriptorPool, nullptr);
		imguiDescriptorPool = VK_NULL_HANDLE;
	}

	// 2. Clean up Resources
	if (colorImageView != VK_NULL_HANDLE) vkDestroyImageView(logicalDevice, colorImageView, nullptr);
	if (colorImage != VK_NULL_HANDLE) vkDestroyImage(logicalDevice, colorImage, nullptr);
	if (colorImageMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, colorImageMemory, nullptr);

	if (depthImageView != VK_NULL_HANDLE) vkDestroyImageView(logicalDevice, depthImageView, nullptr);
	if (depthImage != VK_NULL_HANDLE) vkDestroyImage(logicalDevice, depthImage, nullptr);
	if (depthImageMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, depthImageMemory, nullptr);

	for (auto framebuffer : swapChainFramebuffers) {
		if (framebuffer != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
		}
	}
	swapChainFramebuffers.clear();

	if (logicalDevice != VK_NULL_HANDLE) {
		if (compositionPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, compositionPipeline, nullptr);
		if (compositionPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, compositionPipelineLayout, nullptr);
		if (compositionRenderPass != VK_NULL_HANDLE) vkDestroyRenderPass(logicalDevice, compositionRenderPass, nullptr);
		if (compositionDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(logicalDevice, compositionDescriptorPool, nullptr);
		if (compositionDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(logicalDevice, compositionDescriptorSetLayout, nullptr);

		if (offscreenFramebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(logicalDevice, offscreenFramebuffer, nullptr);
		if (offscreenResolveImageView != VK_NULL_HANDLE) vkDestroyImageView(logicalDevice, offscreenResolveImageView, nullptr);
		if (offscreenResolveImage != VK_NULL_HANDLE) vkDestroyImage(logicalDevice, offscreenResolveImage, nullptr);
		if (offscreenResolveImageMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, offscreenResolveImageMemory, nullptr);
		if (offscreenSampler != VK_NULL_HANDLE) vkDestroySampler(logicalDevice, offscreenSampler, nullptr);

		compositionPipeline = VK_NULL_HANDLE;
		compositionRenderPass = VK_NULL_HANDLE;
		offscreenFramebuffer = VK_NULL_HANDLE;
	}

	for (auto imageView : swapChainImageViews) {
		if (imageView != VK_NULL_HANDLE) {
			vkDestroyImageView(logicalDevice, imageView, nullptr);
		}
	}
	swapChainImageViews.clear();

	// 3. Clean up swapchain
	if (swapChain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(logicalDevice, swapChain, nullptr);
		swapChain = VK_NULL_HANDLE;
	}

	// 4. Sub-system Cleanups
	if (logicalDevice != VK_NULL_HANDLE) {
		// Main Mesh Graphics Pipeline
		if (terrainPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, terrainPipeline, nullptr);
		if (instancedPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, instancedPipeline, nullptr);
		if (staticPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, staticPipeline, nullptr);
		if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);

		instancedPipeline = VK_NULL_HANDLE;
		terrainPipeline = VK_NULL_HANDLE;
		staticPipeline = VK_NULL_HANDLE;
		pipelineLayout = VK_NULL_HANDLE;

		m_staticMeshRenderer.Cleanup();

		// Terrain Pipeline
		m_terrainRenderer.Cleanup();

		// Skybox Pipeline
		m_skybox.Cleanup(logicalDevice);

		// Water Pipeline
		m_waterRenderer.Cleanup(logicalDevice);

		// Grass Pipeline
		m_grassRenderer.Cleanup();

		// Uploader
		m_uploader.Shutdown();

		if (renderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
			renderPass = VK_NULL_HANDLE;
		}
	}

	// 6. Clean up Descriptor Sets, Pools, Layouts & Textures
	if (logicalDevice != VK_NULL_HANDLE) {
		if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
		if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);

		// Let the AssetManager clean up all the other 37 leaked textures (sand, grass, rock)
		if (assetManager != nullptr) {
			assetManager->Cleanup(logicalDevice);
		}
	}

	// 7. Clean up Global Uniform, Light, Mesh, and Terrain buffers
	if (logicalDevice != VK_NULL_HANDLE) {
		// Uniform Buffer
		if (uniformBufferMapped != nullptr) vkUnmapMemory(logicalDevice, uniformBufferMemory);
		DestroyBuffer(uniformBuffer, uniformBufferMemory);

		// Light Buffer
		if (lightBufferMapped != nullptr) vkUnmapMemory(logicalDevice, lightBufferMemory);
		DestroyBuffer(lightBuffer, lightBufferMemory);
	}

	// 8. Clean up Sync Objects (Semaphores & Fences)
	if (logicalDevice != VK_NULL_HANDLE) {
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			if (renderFinishedSemaphores.size() > i && renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
				vkDestroySemaphore(logicalDevice, renderFinishedSemaphores[i], nullptr);
			}
			if (imageAvailableSemaphores.size() > i && imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
				vkDestroySemaphore(logicalDevice, imageAvailableSemaphores[i], nullptr);
			}
			if (inFlightFences.size() > i && inFlightFences[i] != VK_NULL_HANDLE) {
				vkDestroyFence(logicalDevice, inFlightFences[i], nullptr);
			}
		}
	}

	// 9. Clean up Command pools
	if (logicalDevice != VK_NULL_HANDLE) {
		if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
	}

	// 10. Destroy the core Vulkan Handles
	if (logicalDevice != VK_NULL_HANDLE) {
		vkDestroyDevice(logicalDevice, nullptr);
		logicalDevice = VK_NULL_HANDLE;
	}

	if (instance != VK_NULL_HANDLE) {
		// Destroy debug messenger if validation layers were enabled
		if (enableValidationLayers && debugMessenger != VK_NULL_HANDLE) {
			auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
			if (func != nullptr) {
				func(instance, debugMessenger, nullptr);
			}
		}

		if (surface != VK_NULL_HANDLE) {
			vkDestroySurfaceKHR(instance, surface, nullptr);
			surface = VK_NULL_HANDLE;
		}

		vkDestroyInstance(instance, nullptr);
		instance = VK_NULL_HANDLE;
	}

	// 11. Destroy Window context
	if (window != nullptr) {
		glfwDestroyWindow(window);
		window = nullptr;
	}
	glfwTerminate();
}