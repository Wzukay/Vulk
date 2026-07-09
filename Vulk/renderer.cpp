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
	CreateLightBuffer();     

	CreateDescriptorPool();
	CreateDescriptorSet();  

	CreateGlobalBuffers();
	CreateImGui();
	CreateTerrainBuffers();

	g_AssetManager.SetRenderer(this);
	g_AssetManager.SetDescriptorSet(descriptorSet);
	g_AssetManager.CreateDefaultTexture();
	g_AssetManager.CreateDefaultNormalTexture();

	CreateGraphicsPipeline(g_Settings.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL);

	CreateSkyboxTexture();
	UpdateSkyboxDescriptor();
	CreateSkyboxPipeline();

	CreateWaterPipeline();

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

	// 3. Recreate swapchain and its dependent resources
	CreateSwapChain();      // re-creates swapChainImages, swapChainImageFormat, swapChainExtent
	CreateImageViews();
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
	init_info.PipelineInfoMain.RenderPass = renderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);
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
		throw std::runtime_error("Failed to create command pool");
	}

	VkCommandPoolCreateInfo uploadPoolInfo{};
	uploadPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	uploadPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	uploadPoolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
	if (vkCreateCommandPool(logicalDevice, &uploadPoolInfo, nullptr, &uploadCommandPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create upload command pool");
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
	// ORDER MATTERS IN THIS = DRAW ORDER

	if (currentScene == nullptr) {
		std::cout << "[DEBUG] Scene is null" << std::endl;
	}

	drawCallCount = 0;
	sceneTotalVertices = 0;
	sceneTotalIndices = 0;
	culledCount = 0;

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

	if (skyboxPipeline != VK_NULL_HANDLE && currentScene != nullptr) {
		DrawSkybox(commandBuffer);
	}

	if (graphicsPipeline != VK_NULL_HANDLE && currentScene != nullptr) {
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

		if (currentScene != nullptr) {

			visibleStaticDrawList.clear();

			for (const DrawEntry& original : staticDrawList)
			{
				const auto& obj = currentSceneObjects[original.objectIndex];
				const auto& sub = currentSceneSubMeshes[original.subMeshIndex];

				float maxScale = original.cachedMaxScale;

				glm::vec3 worldCenter =
					glm::vec3(obj.modelMatrix *
						glm::vec4(sub.boundingCenterLocal, 1.0f));

				float worldRadius =
					sub.boundingRadiusLocal * maxScale;

				if (!IsSphereInFrustum(worldCenter, worldRadius))
				{
					culledCount++;
					continue;
				}

				DrawEntry entry = original;
				entry.worldCenter = worldCenter;
				entry.worldRadius = worldRadius;
				entry.distSq =
					glm::length2(worldCenter - cameraPosition);

				visibleStaticDrawList.push_back(entry);
			}

			std::sort(visibleStaticDrawList.begin(), visibleStaticDrawList.end(),
				[](const DrawEntry& a, const DrawEntry& b) {
					return a.distSq < b.distSq;
				});

			DrawStaticMeshes(commandBuffer);

			DrawTerrain(commandBuffer);

			DrawWater(commandBuffer);
		}
		else {
			if (globalVertexBuffer == VK_NULL_HANDLE) {
				std::cerr << "[VULKAN RUNTIME] Global vertex buffer is not initialized yet. Skipping scene geometry draw calls.\n";
			}
			else if (globalIndexBuffer == VK_NULL_HANDLE) {
				std::cerr << "[VULKAN RUNTIME] Global index buffer is not initialized yet. Skipping scene geometry draw calls.\n";
			}
		}
	}
	else {
		static bool warned = false;
		if (!warned) {
			std::cerr << "[VULKAN RUNTIME] Pipeline or scene not ready. Skipping draw.\n";
			warned = true;
		}
	}

	if (ImGui::GetDrawData() != nullptr) {
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
		drawCallCount++;
	}

	vkCmdEndRenderPass(commandBuffer);

	m_lastDrawCallCount = drawCallCount;
	m_lastCulledCount = culledCount;
	m_lastTotalVertices = sceneTotalVertices;
	m_lastTotalIndices = sceneTotalIndices;

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to record layout command instructions.");
	}
}

void VulkanRenderer::DrawTerrain(VkCommandBuffer commandBuffer) {
	if (terrainChunks.empty()) return;

	if (globalTerrainVertexBuffer == VK_NULL_HANDLE || globalTerrainIndexBuffer == VK_NULL_HANDLE) {
		std::cerr << "Global terrain buffers are null!\n";
		return;
	}

	VkBuffer vertexBuffers[] = { globalTerrainVertexBuffer };
	VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, globalTerrainIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

	const float occlusionRefreshDist = m_terrainChunkSize * 0.25f;
	bool refreshOcclusion = m_firstOcclusionUpdate ||
		glm::length2(cameraPosition - m_lastOcclusionCameraPos) > (occlusionRefreshDist * occlusionRefreshDist);

	if (refreshOcclusion) {
		m_lastOcclusionCameraPos = cameraPosition;
		m_firstOcclusionUpdate = false;
	}

	for (auto& [key, chunk] : terrainChunks) {
		if (!chunk.ready) continue;

		// Frustum culling
		if (!IsSphereInFrustum(chunk.center, chunk.radius)) {
			culledCount++;
			continue;
		}

		// Occlusion culling 
		if (refreshOcclusion) {
			chunk.cachedOccluded = IsChunkOccluded(chunk.center, chunk.radius);
		}
		if (chunk.cachedOccluded) {
			culledCount++;
			continue;
		}

		// Update stats
		sceneTotalVertices += chunk.vertexCount;
		sceneTotalIndices += chunk.indexCount;

		// ── LOD morphing ──
		float dist = glm::length(chunk.center - cameraPosition);
		float blend = 0.0f;

		if (chunk.lod == 0) {
			// Morph from LOD0 to LOD1 over the range 0 → terrainChunkSize * 2.0f
			float morphEnd = m_terrainChunkSize * 2.0f;
			float t = glm::clamp(dist / morphEnd, 0.0f, 1.0f);
			blend = t * t * (3.0f - 2.0f * t);   // smoothstep
		}
		// For LOD1 and LOD2, blend stays 0 (they already use coarse positions)

		// Build push constants
		PushConstants constants{};
		constants.modelMatrix = glm::mat4(1.0f);
		constants.textureId = 0;
		constants.normalTextureId = 0;
		constants.objectId = 0;
		constants.lodBlend = blend;

		vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
			0, sizeof(PushConstants), &constants);

		// Draw
		vkCmdDrawIndexed(commandBuffer,
			chunk.indexCount,
			1,
			chunk.indexOffset,
			chunk.vertexOffset,
			0);

		drawCallCount++;
	}
}
void VulkanRenderer::DrawStaticMeshes(VkCommandBuffer commandBuffer)
{
	VkBuffer vertexBuffers[] = { globalVertexBuffer };
	VkDeviceSize offsets[] = { 0 };

	vkCmdBindVertexBuffers(
		commandBuffer,
		0,
		1,
		vertexBuffers,
		offsets);

	vkCmdBindIndexBuffer(
		commandBuffer,
		globalIndexBuffer,
		0,
		VK_INDEX_TYPE_UINT32);

	for (const auto& entry : visibleStaticDrawList)
	{
		const auto& obj = currentSceneObjects[entry.objectIndex];
		const auto& sub = currentSceneSubMeshes[entry.subMeshIndex];

		PushConstants constants{};
		constants.modelMatrix = obj.modelMatrix;
		constants.objectId = obj.objectId;
		constants.textureId = sub.textureId;
		constants.normalTextureId = sub.normalTextureId;

		vkCmdPushConstants(
			commandBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof(PushConstants),
			&constants);

		vkCmdDrawIndexed(
			commandBuffer,
			sub.indexCount,
			1,
			sub.firstIndex,
			sub.vertexOffset,
			0);

		drawCallCount++;
	}
}

void VulkanRenderer::UpdateScene(const Scene& scene) {
	const auto& instances = scene.GetInstances();

	// Detect whether the actual set of meshes in the scene changed.
	bool geometryChanged = (instances.size() != m_lastInstanceMeshNames.size());
	if (!geometryChanged) {
		for (size_t i = 0; i < instances.size(); ++i) {
			if (instances[i].meshName != m_lastInstanceMeshNames[i]) {
				geometryChanged = true;
				break;
			}
		}
	}

	if (geometryChanged) {
		// ---- Expensive path: only runs when instances are added/removed/swapped ----
		std::vector<ModelVertex> allVerts;
		std::vector<uint32_t> allIndices;
		std::vector<SubMesh> allSubMeshes;
		std::vector<SceneObject> objects;

		staticDrawList.clear();
		m_lastInstanceMeshNames.clear();
		m_objectDrawEntryIndices.clear();
		m_objectDrawEntryIndices.resize(instances.size());

		uint32_t vertexOffset = 0;
		uint32_t indexOffset = 0;

		for (size_t instIdx = 0; instIdx < instances.size(); ++instIdx) {
			const auto& inst = instances[instIdx];
			m_lastInstanceMeshNames.push_back(inst.meshName);

			MeshAsset* mesh = g_AssetManager.GetMesh(inst.meshName);
			if (!mesh) {
				std::cerr << "[UpdateScene] Mesh not found: " << inst.meshName << "\n";
				continue;
			}

			allVerts.insert(allVerts.end(), mesh->vertices.begin(), mesh->vertices.end());
			allIndices.insert(allIndices.end(), mesh->indices.begin(), mesh->indices.end());

			SceneObject obj;
			obj.modelMatrix = inst.transform;
			obj.objectId = inst.objectId;
			obj.type = inst.type;
			obj.firstSubMesh = static_cast<uint32_t>(allSubMeshes.size());
			obj.subMeshCount = static_cast<uint32_t>(mesh->subMeshes.size());

			for (size_t subIdx = 0; subIdx < mesh->subMeshes.size(); ++subIdx) {
				const auto& sub = mesh->subMeshes[subIdx];
				SubMesh newSub = sub;
				newSub.vertexOffset += vertexOffset;
				newSub.firstIndex += indexOffset;

				const std::string& texPath = mesh->materialTextures[subIdx];
				newSub.textureId = g_AssetManager.GetTextureId(texPath);

				const std::string& normalPath = mesh->normalMapTextures[subIdx];
				newSub.normalTextureId = g_AssetManager.GetNormalTextureId(normalPath);

				allSubMeshes.push_back(newSub);
			}

			objects.push_back(obj);

			float scaleX = glm::length(glm::vec3(obj.modelMatrix[0]));
			float scaleY = glm::length(glm::vec3(obj.modelMatrix[1]));
			float scaleZ = glm::length(glm::vec3(obj.modelMatrix[2]));
			float maxScale = std::max({ scaleX, scaleY, scaleZ });

			uint32_t objIdx = static_cast<uint32_t>(objects.size()) - 1;
			for (uint32_t sub = obj.firstSubMesh; sub < obj.firstSubMesh + obj.subMeshCount; sub++) {
				DrawEntry entry{};
				entry.objectIndex = objIdx;
				entry.subMeshIndex = sub;
				entry.cachedMaxScale = maxScale;

				m_objectDrawEntryIndices[instIdx].push_back(static_cast<uint32_t>(staticDrawList.size()));
				staticDrawList.push_back(entry);
			}

			vertexOffset += static_cast<uint32_t>(mesh->vertices.size());
			indexOffset += static_cast<uint32_t>(mesh->indices.size());
		}

		UploadStaticSceneData(allVerts, allIndices);

		currentSceneObjects = std::move(objects);
		currentSceneSubMeshes = std::move(allSubMeshes);
	}
	else {
		// ---- Cheap path: runs every frame, just refreshes transforms, no GPU upload ----
		for (size_t i = 0; i < instances.size() && i < currentSceneObjects.size(); ++i) {
			const glm::mat4& xform = instances[i].transform;
			currentSceneObjects[i].modelMatrix = xform;

			float scaleX = glm::length(glm::vec3(xform[0]));
			float scaleY = glm::length(glm::vec3(xform[1]));
			float scaleZ = glm::length(glm::vec3(xform[2]));
			float maxScale = std::max({ scaleX, scaleY, scaleZ });

			for (uint32_t drawIdx : m_objectDrawEntryIndices[i]) {
				staticDrawList[drawIdx].cachedMaxScale = maxScale;
			}
		}
	}

	if (g_AssetManager.IsTextureDirty()) {
		UpdateTextureDescriptors(scene);
		g_AssetManager.ClearTextureDirty();
	}

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
	const auto& registry = g_AssetManager.GetTextureRegistry();
	const auto& textureMap = g_AssetManager.GetTextureMap();

	std::vector<VkWriteDescriptorSet> writes;
	std::deque<VkDescriptorImageInfo> imageInfos;

	for (const auto& pair : textureMap) {
		const std::string& path = pair.first;
		if (path == "default") continue;
		uint32_t id = pair.second;
		const Texture* tex = g_AssetManager.GetTexture(pair.first); // returns pointer
		if (!tex) continue;

		VkDescriptorImageInfo info{};
		info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		info.imageView = tex->imageView;
		info.sampler = tex->sampler;
		imageInfos.push_back(info);

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = descriptorSet;
		write.dstBinding = 2;
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

void VulkanRenderer::CreateGraphicsPipeline(VkPolygonMode polygonMode) {
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
	auto bindingDescription = ModelVertex::getBindingDescription();
	auto attributeDescriptions = ModelVertex::getAttributeDescriptions();

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
	rasterizer.polygonMode = polygonMode; // Solid geometry fill mode
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

	std::vector<char> cacheData;
	std::ifstream cacheFile("pipeline.cache", std::ios::ate | std::ios::binary);
	if (cacheFile.is_open()) {
		size_t size = cacheFile.tellg();
		cacheFile.seekg(0);
		cacheData.resize(size);
		cacheFile.read(cacheData.data(), size);
		cacheFile.close();
	}

	VkPipelineCacheCreateInfo cacheInfo{};
	cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	cacheInfo.initialDataSize = cacheData.size();
	cacheInfo.pInitialData = cacheData.empty() ? nullptr : cacheData.data();

	if (vkCreatePipelineCache(logicalDevice, &cacheInfo, nullptr, &pipelineCache) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create pipeline cache.");
	}

	auto createPipeline = [&](VkBool32 blendEnable, VkBool32 depthWriteEnable) -> VkPipeline {
		// Color blend attachment
		VkPipelineColorBlendAttachmentState colorBlendAttachment{};
		colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = blendEnable;
		if (blendEnable) {
			colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
			colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
			colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
			colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
		}

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		// Override depth write
		VkPipelineDepthStencilStateCreateInfo ds = depthStencil;
		ds.depthWriteEnable = depthWriteEnable;

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
		pipelineInfo.pDepthStencilState = &ds;
		pipelineInfo.pDynamicState = &dynamicState;
		pipelineInfo.layout = pipelineLayout;
		pipelineInfo.renderPass = renderPass;
		pipelineInfo.subpass = 0;

		VkPipeline pipeline;
		// Create pipeline using the cache
		if (vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create graphics pipeline.");
		}
		return pipeline;
		};

	graphicsPipeline = createPipeline(VK_FALSE, VK_TRUE);          // Opaque
	//graphicsPipelineTransparent = createPipeline(VK_TRUE, VK_FALSE); // Transparent

	size_t dataSize;
	if (vkGetPipelineCacheData(logicalDevice, pipelineCache, &dataSize, nullptr) == VK_SUCCESS && dataSize > 0) {
		std::vector<char> newCacheData(dataSize);
		if (vkGetPipelineCacheData(logicalDevice, pipelineCache, &dataSize, newCacheData.data()) == VK_SUCCESS) {
			std::ofstream outFile("pipeline.cache", std::ios::binary);
			if (outFile) {
				outFile.write(newCacheData.data(), dataSize);
				outFile.close();
			}
		}
	}

	vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
	vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
}
void VulkanRenderer::CreateSkyboxPipeline() {
	auto vertCode = ReadFile("shaders/skybox_vert.spv");
	auto fragCode = ReadFile("shaders/skybox_frag.spv");
	skyboxVertModule = CreateShaderModule(vertCode);
	skyboxFragModule = CreateShaderModule(fragCode);

	VkPipelineShaderStageCreateInfo vertStage = {};
	vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStage.module = skyboxVertModule;
	vertStage.pName = "main";

	VkPipelineShaderStageCreateInfo fragStage = {};
	fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragStage.module = skyboxFragModule;
	fragStage.pName = "main";

	VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

	// No vertex input
	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 0;
	vertexInput.pVertexBindingDescriptions = nullptr;
	vertexInput.vertexAttributeDescriptionCount = 0;
	vertexInput.pVertexAttributeDescriptions = nullptr;

	// Input assembly: triangle list
	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	// Viewport state (dynamic)
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// Rasterizer: back-face culling off, depth bias off, polygon mode fill
	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE; // we want to see sky from inside
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	// Multisample
	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.sampleShadingEnable = VK_FALSE;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Depth/stencil: depth test on, depth write off, compare op <=
	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencil.stencilTestEnable = VK_FALSE;

	// Color blending: disable
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;

	// Dynamic states: viewport and scissor
	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	// Pipeline layout: we need the UBO binding for skybox (binding 0)
	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &descriptorSetLayout; // reuse the same layout as main
	layoutInfo.pushConstantRangeCount = 0;
	layoutInfo.pPushConstantRanges = nullptr;

	if (vkCreatePipelineLayout(logicalDevice, &layoutInfo, nullptr, &skyboxPipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create skybox pipeline layout");
	}

	// Graphics pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = skyboxPipelineLayout;
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, &skyboxPipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create skybox pipeline");
	}
}
void VulkanRenderer::CreateWaterPipeline() {
	auto vertCode = ReadFile("shaders/water_vert.spv");
	auto fragCode = ReadFile("shaders/water_frag.spv");
	waterVertModule = CreateShaderModule(vertCode);
	waterFragModule = CreateShaderModule(fragCode);

	VkPipelineShaderStageCreateInfo vertStage = {};
	vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStage.module = waterVertModule;
	vertStage.pName = "main";

	VkPipelineShaderStageCreateInfo fragStage = {};
	fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragStage.module = waterFragModule;
	fragStage.pName = "main";

	VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

	// Vertex input: WaterVertex { vec3 pos; vec2 uv; }
	VkVertexInputBindingDescription binding{};
	binding.binding = 0;
	binding.stride = sizeof(WaterVertex);
	binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	std::array<VkVertexInputAttributeDescription, 2> attributes{};
	attributes[0].location = 0;
	attributes[0].binding = 0;
	attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	attributes[0].offset = offsetof(WaterVertex, pos);

	attributes[1].location = 1;
	attributes[1].binding = 0;
	attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
	attributes[1].offset = offsetof(WaterVertex, uv);

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &binding;
	vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
	vertexInput.pVertexAttributeDescriptions = attributes.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// No culling: water can be seen from below (underwater) as well as above.
	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.sampleShadingEnable = VK_FALSE;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Depth test on (so terrain/objects occlude water), depth write OFF (translucent, like skybox).
	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.stencilTestEnable = VK_FALSE;

	// Alpha blending on, same factors as your other translucent pass.
	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;

	VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(WaterPushConstants);

	VkPipelineLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &descriptorSetLayout; // reuse the same bindless layout as everything else
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(logicalDevice, &layoutInfo, nullptr, &waterPipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create water pipeline layout");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = stages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = waterPipelineLayout;
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineInfo, nullptr, &waterPipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create water pipeline");
	}
}
void VulkanRenderer::RecreateGraphicsPipeline() {
	if (graphicsPipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
		graphicsPipeline = VK_NULL_HANDLE;
	}
	VkPolygonMode mode = g_Settings.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
	CreateGraphicsPipeline(mode);
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

	// Only add cubemap write if texture is valid
	if (m_skyboxTexture.imageView != VK_NULL_HANDLE && m_skyboxTexture.sampler != VK_NULL_HANDLE) {
		VkDescriptorImageInfo cubemapInfo{};
		cubemapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		cubemapInfo.imageView = m_skyboxTexture.imageView;
		cubemapInfo.sampler = m_skyboxTexture.sampler;

		VkWriteDescriptorSet cubemapWrite{};
		cubemapWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		cubemapWrite.dstSet = descriptorSet;
		cubemapWrite.dstBinding = 4;
		cubemapWrite.dstArrayElement = 0;
		cubemapWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		cubemapWrite.descriptorCount = 1;
		cubemapWrite.pImageInfo = &cubemapInfo;

		writes.push_back(cubemapWrite);
	}
	else {
		std::cout << "[Renderer] Warning: Skybox texture not set, skipping cubemap descriptor write.\n";
	}

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

	ubo.fogStart = m_fogStart;
	ubo.fogEnd = m_fogEnd;

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

void VulkanRenderer::CreateTerrainBuffers() {
	const VkDeviceSize maxTerrainVertices = 10'000'000;  // adjust to your needs
	const VkDeviceSize maxTerrainIndices = 10'000'000;

	VkDeviceSize vertexBufferSize = sizeof(ModelVertex) * maxTerrainVertices;
	VkDeviceSize indexBufferSize = sizeof(uint32_t) * maxTerrainIndices;

	VkBufferCreateInfo stagingInfo{};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = STAGING_BUFFER_SIZE;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(logicalDevice, &stagingInfo, nullptr, &m_globalStagingBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create unified staging buffer!");
	}

	// 2. Query structural size requirements
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements(logicalDevice, m_globalStagingBuffer, &memReqs);

	// 3. Find correct memory type for Host-Visible & Host-Coherent RAM
	VkMemoryAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memReqs.size;
	allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &m_globalStagingMemory) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate unified staging memory!");
	}

	// 4. Bind driver memory to our buffer handle
	vkBindBufferMemory(logicalDevice, m_globalStagingBuffer, m_globalStagingMemory, 0);

	// 5. 🚀 THE CRITICAL OPTIMIZATION: Map the memory handle ONCE for the lifetime of the application
	if (vkMapMemory(logicalDevice, m_globalStagingMemory, 0, STAGING_BUFFER_SIZE, 0, &m_stagingBufferMapped) != VK_SUCCESS) {
		throw std::runtime_error("Failed to persistently map global staging buffer!");
	}

	// Clear the ring index offset
	m_stagingRingOffset = 0;

	CreateBuffer(vertexBufferSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		globalTerrainVertexBuffer, globalTerrainVertexMemory);

	CreateBuffer(indexBufferSize,
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
		globalTerrainIndexBuffer, globalTerrainIndexMemory);

	std::cout << "[Memory Manager] Pre-allocated global terrain buffers.\n";
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

	init_info.PipelineInfoMain.RenderPass = renderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);
}
void VulkanRenderer::DrawGUI() {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	uint32_t triangleCount = m_lastTotalIndices / 3;
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
	ImGui::Text("Vertices:  %u", m_lastTotalVertices);
	ImGui::Text("Indices:   %u", m_lastTotalIndices);

	ImGui::Separator();

	// PIPELINE SECTION
	ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "PIPELINE");
	ImGui::Text("Draw Calls: %-8u", m_lastDrawCallCount);
	ImGui::Text("Culled:     %-8u", m_lastCulledCount);
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

void VulkanRenderer::UploadStaticSceneData(const std::vector<ModelVertex>& verts, const std::vector<uint32_t>& idxs) {
	// Check size
	if (verts.empty() || idxs.empty()) {
		return;
	}

	if (verts.size() > MAX_GLOBAL_VERTICES || idxs.size() > MAX_GLOBAL_INDICES) {
		throw std::runtime_error("Scene data exceeds pre-allocated buffer size!");
	}

	VkDeviceSize vertexSize = sizeof(ModelVertex) * verts.size();
	VkDeviceSize indexSize = sizeof(uint32_t) * idxs.size();

	// Staging buffers
	VkBuffer stagingVert = VK_NULL_HANDLE;
	VkDeviceMemory stagingVertMem = VK_NULL_HANDLE;
	VkBuffer stagingIndex = VK_NULL_HANDLE;
	VkDeviceMemory stagingIndexMem = VK_NULL_HANDLE;

	try {
		CreateBuffer(vertexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingVert, stagingVertMem);
		void* data;
		vkMapMemory(logicalDevice, stagingVertMem, 0, vertexSize, 0, &data);
		memcpy(data, verts.data(), vertexSize);
		vkUnmapMemory(logicalDevice, stagingVertMem);

		VkBuffer stagingInd; VkDeviceMemory stagingIndMem;
		CreateBuffer(indexSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingInd, stagingIndMem);
		vkMapMemory(logicalDevice, stagingIndMem, 0, indexSize, 0, &data);
		memcpy(data, idxs.data(), indexSize);
		vkUnmapMemory(logicalDevice, stagingIndMem);

		VkCommandBuffer cmd = BeginSingleTimeCommands();
		VkBufferCopy vertCopy{ 0, 0, vertexSize };
		vkCmdCopyBuffer(cmd, stagingVert, globalVertexBuffer, 1, &vertCopy);
		VkBufferCopy indCopy{ 0, 0, indexSize };
		vkCmdCopyBuffer(cmd, stagingInd, globalIndexBuffer, 1, &indCopy);
		EndSingleTimeCommands(cmd);

		vkDestroyBuffer(logicalDevice, stagingVert, nullptr);
		vkFreeMemory(logicalDevice, stagingVertMem, nullptr);
		vkDestroyBuffer(logicalDevice, stagingInd, nullptr);
		vkFreeMemory(logicalDevice, stagingIndMem, nullptr);
	}
	catch (...) {
		if (stagingVert != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, stagingVert, nullptr);
		if (stagingVertMem != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, stagingVertMem, nullptr);
		if (stagingIndex != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, stagingIndex, nullptr);
		if (stagingIndexMem != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, stagingIndexMem, nullptr);
		throw; // rethrow the exception
	}
}
void VulkanRenderer::AddTerrainChunk(
	int64_t key,
	int cx,
	int cz,
	int lod,
	const std::vector<ModelVertex>& vertices,
	const std::vector<uint32_t>& indices)
{
	if (m_isShuttingDown) {
		std::cout << "[WARN] AddTerrainChunk called during shutdown, ignoring.\n";
		return;
	}

	TerrainChunkGPU chunk{};

	chunk.key = key;

	glm::vec3 minBound(FLT_MAX), maxBound(-FLT_MAX);
	for (const auto& v : vertices) {
		minBound = glm::min(minBound, v.pos);
		maxBound = glm::max(maxBound, v.pos);
	}
	glm::vec3 center = (minBound + maxBound) * 0.5f;
	float radius = 0.0f;
	for (const auto& v : vertices) {
		radius = glm::max(radius, glm::length(v.pos - center));
	}
	radius += 10.0f;  // margin for safety

	chunk.center = center;
	chunk.radius = radius;
	chunk.lod = lod;

	UploadTerrainChunkAsync(chunk, vertices, indices);
}
void VulkanRenderer::UploadTerrainChunkAsync(
	TerrainChunkGPU& chunk,
	const std::vector<ModelVertex>& verts,
	const std::vector<uint32_t>& indices)
{
	VkDeviceSize vertexSize = sizeof(ModelVertex) * verts.size();
	VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

	if (vertexSize == 0 || indexSize == 0) return;

	// Allocate space in the global discrete GPU buffers (Your un-altered memory strategy)
	uint32_t vertexOffset, indexOffset;
	{
		std::lock_guard<std::mutex> lock(terrainAllocMutex);

		auto vIt = std::find_if(freeVertexSpans.begin(), freeVertexSpans.end(),
			[&](const FreeSpan& s) { return s.count >= verts.size(); });
		if (vIt != freeVertexSpans.end()) {
			vertexOffset = vIt->offset;
			freeVertexSpans.erase(vIt);
		}
		else {
			vertexOffset = nextTerrainVertexOffset.fetch_add(static_cast<uint32_t>(verts.size()));
		}

		auto iIt = std::find_if(freeIndexSpans.begin(), freeIndexSpans.end(),
			[&](const FreeSpan& s) { return s.count >= indices.size(); });
		if (iIt != freeIndexSpans.end()) {
			indexOffset = iIt->offset;
			freeIndexSpans.erase(iIt);
		}
		else {
			indexOffset = nextTerrainIndexOffset.fetch_add(static_cast<uint32_t>(indices.size()));
		}
	}

	chunk.vertexOffset = vertexOffset;
	chunk.indexOffset = indexOffset;
	chunk.vertexCount = static_cast<uint32_t>(verts.size());
	chunk.indexCount = static_cast<uint32_t>(indices.size());

	// 🚀 OPTIMIZED RING PASS: Instead of allocations, roll the cursor inside the mapped 32MB pointer
	uint32_t localStagingVertexOffset = 0;
	uint32_t localStagingIndexOffset = 0;

	{
		std::lock_guard<std::mutex> lock(m_stagingBufferMutex);

		// If the arriving sizes exceed remaining pool capacity, wrap back around to zero
		if (m_stagingRingOffset + vertexSize + indexSize >= STAGING_BUFFER_SIZE) {
			m_stagingRingOffset = 0;
		}

		// Write vertices straight to RAM via offset
		localStagingVertexOffset = m_stagingRingOffset;
		std::memcpy(static_cast<uint8_t*>(m_stagingBufferMapped) + localStagingVertexOffset, verts.data(), vertexSize);
		m_stagingRingOffset += static_cast<uint32_t>(vertexSize);

		// Write indices straight to RAM via offset
		localStagingIndexOffset = m_stagingRingOffset;
		std::memcpy(static_cast<uint8_t*>(m_stagingBufferMapped) + localStagingIndexOffset, indices.data(), indexSize);
		m_stagingRingOffset += static_cast<uint32_t>(indexSize);
	}

	// --- Allocate a command buffer out of your existing pool ---
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = uploadCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	VkCommandBuffer cmdBuf;
	if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &cmdBuf) != VK_SUCCESS)
		throw std::runtime_error("Failed to allocate upload command buffer");

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmdBuf, &beginInfo);

	// 🚀 UNIFIED COPIES: Pull from m_globalStagingBuffer via unique staging offset keys
	VkBufferCopy vertCopy{ localStagingVertexOffset, vertexOffset * sizeof(ModelVertex), vertexSize };
	vkCmdCopyBuffer(cmdBuf, m_globalStagingBuffer, globalTerrainVertexBuffer, 1, &vertCopy);

	VkBufferCopy indexCopy{ localStagingIndexOffset, indexOffset * sizeof(uint32_t), indexSize };
	vkCmdCopyBuffer(cmdBuf, m_globalStagingBuffer, globalTerrainIndexBuffer, 1, &indexCopy);

	vkEndCommandBuffer(cmdBuf);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	VkFence fence;
	vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmdBuf;
	if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS)
		throw std::runtime_error("Failed to submit upload command buffer");

	// --- Store pending upload for tracking ---
	chunk.ready = false;
	PendingUpload upload;
	upload.fence = fence;
	upload.commandBuffer = cmdBuf;

	// 🚀 ZERO ALLOCATION: Set these to VK_NULL_HANDLE so your cleanup loop skips destroying them!
	upload.stagingVertexBuffer = VK_NULL_HANDLE;
	upload.stagingVertexMemory = VK_NULL_HANDLE;
	upload.stagingIndexBuffer = VK_NULL_HANDLE;
	upload.stagingIndexMemory = VK_NULL_HANDLE;

	upload.chunkKey = chunk.key;
	upload.chunk = chunk;
	pendingUploads.push_back(upload);
}
void VulkanRenderer::RemoveTerrainChunk(int64_t key) {
	auto it = terrainChunks.find(key);
	if (it == terrainChunks.end()) return;

	TerrainChunkGPU& chunk = it->second;

	// Return the offsets to the free lists
	{
		std::lock_guard<std::mutex> lock(terrainAllocMutex);
		freeVertexSpans.push_back({ chunk.vertexOffset, chunk.vertexCount });
		freeIndexSpans.push_back({ chunk.indexOffset, chunk.indexCount });
	}

	// Remove from the map
	terrainChunks.erase(it);
}
bool VulkanRenderer::IsChunkOccluded(const glm::vec3& chunkCenter, float chunkRadius) {
	const glm::vec3& camPos = cameraPosition;

	// If the camera is inside the chunk, don't occlude
	float distToCenter = glm::length(chunkCenter - camPos);
	if (distToCenter < chunkRadius) return false;

	// Direction from camera to chunk center
	glm::vec3 dir = glm::normalize(chunkCenter - camPos);

	// Number of samples along the ray (more = more accurate, slower)
	const int numSamples = 20;   // was 10
	float step = distToCenter / (float)numSamples;

	// Start slightly offset from the camera (avoid self‑occlusion)
	float offset = 2.0f;
	glm::vec3 samplePos = camPos + dir * offset;

	// Height margin to avoid tiny bumps culling chunks (tune this)
	const float heightMargin = 10.0f;

	glm::vec3 up(0.0f, 1.0f, 0.0f);
	glm::vec3 right = glm::normalize(glm::cross(dir, up));
	glm::vec3 sampleOffsets[] = {
		glm::vec3(0.0f, 0.0f, 0.0f),      // center
		right * chunkRadius * 0.5f,        // right
		-right * chunkRadius * 0.5f,       // left
		up * chunkRadius * 0.5f,           // above
		-up * chunkRadius * 0.5f           // below
	};

	for (int i = 0; i < numSamples; ++i) {
		// Check the main ray
		float terrainHeight = Chunk::GetHeight(samplePos.x, samplePos.z);
		if (samplePos.y < terrainHeight + heightMargin) {
			// Also check the offsets – if any offset point is above terrain, chunk is visible
			for (const auto& offsetVec : sampleOffsets) {
				glm::vec3 offsetPos = samplePos + offsetVec;
				float offsetTerrain = Chunk::GetHeight(offsetPos.x, offsetPos.z);
				if (offsetPos.y >= offsetTerrain + heightMargin) {
					return false; // visible
				}
			}
			return true; // all points are below terrain → occluded
		}
		samplePos += dir * step;
	}

	return false; // Not occluded
}

size_t VulkanRenderer::CreateWaterBodyGPU(const WaterMesh& mesh,
	const std::string& normalMapPath,
	float tiling, float waveStrength) {
	WaterBodyGPU body{};
	body.indexCount = static_cast<uint32_t>(mesh.indices.size());
	body.normalTextureId = g_AssetManager.GetNormalTextureId(normalMapPath);
	body.tiling = tiling;
	body.waveStrength = waveStrength;

	VkDeviceSize vertexSize = sizeof(WaterVertex) * mesh.vertices.size();
	VkDeviceSize indexSize = sizeof(uint32_t) * mesh.indices.size();

	CreateBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		body.vertexBuffer, body.vertexMemory);

	void* data;
	vkMapMemory(logicalDevice, body.vertexMemory, 0, vertexSize, 0, &data);
	memcpy(data, mesh.vertices.data(), vertexSize);
	vkUnmapMemory(logicalDevice, body.vertexMemory);

	CreateBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		body.indexBuffer, body.indexMemory);

	vkMapMemory(logicalDevice, body.indexMemory, 0, indexSize, 0, &data);
	memcpy(data, mesh.indices.data(), indexSize);
	vkUnmapMemory(logicalDevice, body.indexMemory);

	m_waterBodies.push_back(body);
	return m_waterBodies.size() - 1;
}
void VulkanRenderer::AddWaterBody(const WaterMesh& mesh,
	const std::string& normalMapPath,
	float tiling, float waveStrength) {
	if (mesh.vertices.empty() || mesh.indices.empty()) return;
	CreateWaterBodyGPU(mesh, normalMapPath, tiling, waveStrength);
}
void VulkanRenderer::AddWaterBodyForChunk(int64_t chunkKey,
	const WaterMesh& mesh,
	const std::string& normalMapPath,
	float tiling, float waveStrength) {
	if (mesh.vertices.empty() || mesh.indices.empty()) return;

	// Remove any existing water for this chunk first
	auto it = m_waterBodyLookup.find(chunkKey);
	if (it != m_waterBodyLookup.end()) {
		RemoveWaterBody(chunkKey);
	}

	size_t idx = CreateWaterBodyGPU(mesh, normalMapPath, tiling, waveStrength);
	m_waterBodyLookup[chunkKey] = idx;
}
void VulkanRenderer::RemoveWaterBody(int64_t chunkKey) {
	auto it = m_waterBodyLookup.find(chunkKey);
	if (it == m_waterBodyLookup.end()) {
		return;
	}

	size_t targetIndex = it->second;
	if (targetIndex >= m_waterBodies.size()) {
		m_waterBodyLookup.erase(it);
		return;
	}

	WaterBodyGPU& body = m_waterBodies[targetIndex];

	PendingDeletion del;
	del.buffers.push_back(body.vertexBuffer);
	del.memories.push_back(body.vertexMemory);
	del.buffers.push_back(body.indexBuffer);
	del.memories.push_back(body.indexMemory);
	// Ensure the buffers survive at least MAX_FRAMES_IN_FLIGHT frames
	del.safeFrame = m_globalFrameCounter + MAX_FRAMES_IN_FLIGHT + 1;
	m_pendingDeletionsGlobal.push_back(std::move(del));

	// ---- Swap-and-pop pattern to remove from vector ----
	if (targetIndex != m_waterBodies.size() - 1) {
		m_waterBodies[targetIndex] = m_waterBodies.back();
		// Update lookup for the moved entry
		for (auto& pair : m_waterBodyLookup) {
			if (pair.second == m_waterBodies.size() - 1) {
				pair.second = targetIndex;
				break;
			}
		}
	}

	m_waterBodies.pop_back();
	m_waterBodyLookup.erase(it);
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

	// Only check window size if not fullscreen (fullscreen size is handled by monitor)
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

	if (g_Settings.wireframe != currentSettings.wireframe) {
		RecreateGraphicsPipeline();
		currentSettings.wireframe = g_Settings.wireframe;
	}

	currentSettings.anisotropicFiltering = g_Settings.anisotropicFiltering;
	currentSettings.maxAnisotropy = g_Settings.maxAnisotropy;
	currentSettings.renderDistance = g_Settings.renderDistance;
	currentSettings.frustumCulling = g_Settings.frustumCulling;
	currentSettings.showTerrain = g_Settings.showTerrain;
	currentSettings.showModels = g_Settings.showModels;
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

void VulkanRenderer::DrawFrame() {
	if (globalVertexBuffer == VK_NULL_HANDLE) {
		std::cout << "CRITICAL: vertexBuffer is still null when drawing!" << std::endl;
	}

	ApplySettings();

	m_globalFrameCounter++;

	for (auto it = m_pendingDeletionsGlobal.begin(); it != m_pendingDeletionsGlobal.end(); ) {
		if (m_globalFrameCounter >= it->safeFrame) {
			for (auto buf : it->buffers) {
				if (buf != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, buf, nullptr);
			}
			for (auto mem : it->memories) {
				if (mem != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, mem, nullptr);
			}
			it = m_pendingDeletionsGlobal.erase(it);
		}
		else {
			++it;
		}
	}


	DrawGUI();

	vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

	for (auto it = m_staleWaterQueue.begin(); it != m_staleWaterQueue.end(); ) {
		if (it->safeFrameIndex == currentFrame) {
			if (it->vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, it->vertexBuffer, nullptr);
			if (it->vertexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, it->vertexMemory, nullptr);
			if (it->indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, it->indexBuffer, nullptr);
			if (it->indexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, it->indexMemory, nullptr);
			it = m_staleWaterQueue.erase(it);
		}
		else {
			++it;
		}
	}

	// --- Process completed asynchronous uploads (NEW) ---
	for (auto it = pendingUploads.begin(); it != pendingUploads.end(); ) {
		VkResult status = vkGetFenceStatus(logicalDevice, it->fence);
		if (status == VK_SUCCESS) {
			auto existingIt = terrainChunks.find(it->chunkKey);
			if (existingIt != terrainChunks.end()) {
				std::lock_guard<std::mutex> lock(terrainAllocMutex);
				freeVertexSpans.push_back({ existingIt->second.vertexOffset, existingIt->second.vertexCount });
				freeIndexSpans.push_back({ existingIt->second.indexOffset, existingIt->second.indexCount });
			}

			it->chunk.ready = true;
			terrainChunks[it->chunkKey] = it->chunk;

			// Upload finished – destroy staging buffers and free command buffer
			vkDestroyBuffer(logicalDevice, it->stagingVertexBuffer, nullptr);
			vkFreeMemory(logicalDevice, it->stagingVertexMemory, nullptr);
			vkDestroyBuffer(logicalDevice, it->stagingIndexBuffer, nullptr);
			vkFreeMemory(logicalDevice, it->stagingIndexMemory, nullptr);
			vkDestroyFence(logicalDevice, it->fence, nullptr);
			vkFreeCommandBuffers(logicalDevice, uploadCommandPool, 1, &it->commandBuffer);
			it = pendingUploads.erase(it);
		}
		else if (status == VK_NOT_READY) {
			++it;   // Not finished yet, keep it
		}
		else {
			throw std::runtime_error("vkGetFenceStatus failed for upload");
		}
	}

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
void VulkanRenderer::DrawSkybox(VkCommandBuffer commandBuffer) {
	if (m_skyboxTexture.imageView == VK_NULL_HANDLE) return;
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}
void VulkanRenderer::DrawWater(VkCommandBuffer commandBuffer) {
	if (m_waterBodies.empty()) return;

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, waterPipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

	float elapsed = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - m_waterStartTime).count();

	for (const auto& body : m_waterBodies) {
		VkBuffer vertexBuffers[] = { body.vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, body.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		WaterPushConstants constants{};
		constants.modelMatrix = glm::mat4(1.0f);
		constants.time = elapsed;
		constants.normalTextureId = body.normalTextureId;
		constants.tiling = body.tiling;
		constants.waveStrength = body.waveStrength;

		vkCmdPushConstants(commandBuffer, waterPipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(WaterPushConstants), &constants);

		vkCmdDrawIndexed(commandBuffer, body.indexCount, 1, 0, 0, 0);
		drawCallCount++;
	}
}

void VulkanRenderer::CreateSkyboxTexture() {
	std::string base = "assets/skybox/";
	m_skyboxTexture = g_AssetManager.LoadCubemapFromFaces(
		base + "right.tga",
		base + "left.tga",
		base + "top.tga",
		base + "bottom.tga",
		base + "back.tga",
		base + "front.tga"
	);

	if (m_skyboxTexture.imageView == VK_NULL_HANDLE) {
		std::cerr << "[Skybox] Failed to load skybox faces.\n";
	}
}
void VulkanRenderer::UpdateSkyboxDescriptor() {
	if (descriptorSet == VK_NULL_HANDLE) return;

	VkDescriptorImageInfo info{};
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	Texture& defaultTex = g_AssetManager.GetDefaultTexture();

	if (m_skyboxTexture.imageView != VK_NULL_HANDLE && m_skyboxTexture.sampler != VK_NULL_HANDLE) {
		info.imageView = m_skyboxTexture.imageView;
		info.sampler = m_skyboxTexture.sampler;
	}
	else {
		info.imageView = defaultTex.imageView;
		info.sampler = defaultTex.sampler;
	}

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = descriptorSet;
	write.dstBinding = 4;
	write.dstArrayElement = 0;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.descriptorCount = 1;
	write.pImageInfo = &info;

	vkUpdateDescriptorSets(logicalDevice, 1, &write, 0, nullptr);
}

void VulkanRenderer::SetFogParams(float start, float end) {
	m_fogStart = start;
	m_fogEnd = end;
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

	// 2. Clean up Swapchain and dependent resources
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

	for (auto framebuffer : swapChainFramebuffers) {
		if (framebuffer != VK_NULL_HANDLE) {
			vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
		}
	}
	swapChainFramebuffers.clear();

	for (auto imageView : swapChainImageViews) {
		if (imageView != VK_NULL_HANDLE) {
			vkDestroyImageView(logicalDevice, imageView, nullptr);
		}
	}
	swapChainImageViews.clear();

	if (swapChain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(logicalDevice, swapChain, nullptr);
		swapChain = VK_NULL_HANDLE;
	}

	// 3. Process remaining pending uploads/deletions immediately
	if (logicalDevice != VK_NULL_HANDLE) {
		for (auto& upload : pendingUploads) {
			if (upload.fence != VK_NULL_HANDLE) vkDestroyFence(logicalDevice, upload.fence, nullptr);
			if (upload.stagingVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, upload.stagingVertexBuffer, nullptr);
			if (upload.stagingVertexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, upload.stagingVertexMemory, nullptr);
			if (upload.stagingIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, upload.stagingIndexBuffer, nullptr);
			if (upload.stagingIndexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, upload.stagingIndexMemory, nullptr);
		}
		pendingUploads.clear();
	}

	// 4. Clean up pipelines, layouts, and individual shader modules
	if (logicalDevice != VK_NULL_HANDLE) {
		// Main Mesh Graphics Pipeline
		if (graphicsPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
		if (pipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);

		// Skybox Pipeline
		if (skyboxPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, skyboxPipeline, nullptr);
		if (skyboxPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, skyboxPipelineLayout, nullptr);
		if (skyboxVertModule != VK_NULL_HANDLE) vkDestroyShaderModule(logicalDevice, skyboxVertModule, nullptr);
		if (skyboxFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(logicalDevice, skyboxFragModule, nullptr);

		// Water Pipeline
		if (waterPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, waterPipeline, nullptr);
		if (waterPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, waterPipelineLayout, nullptr);
		if (waterVertModule != VK_NULL_HANDLE) vkDestroyShaderModule(logicalDevice, waterVertModule, nullptr);
		if (waterFragModule != VK_NULL_HANDLE) vkDestroyShaderModule(logicalDevice, waterFragModule, nullptr);

		// Pipeline cache
		if (pipelineCache != VK_NULL_HANDLE) vkDestroyPipelineCache(logicalDevice, pipelineCache, nullptr);

		if (renderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
			renderPass = VK_NULL_HANDLE;
		}
	}

	// 5. Clean up individual Water bodies that haven't been queued for deletion
	if (logicalDevice != VK_NULL_HANDLE) {
		for (auto& water : m_waterBodies) {
			if (water.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, water.vertexBuffer, nullptr);
			if (water.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, water.vertexMemory, nullptr);
			if (water.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, water.indexBuffer, nullptr);
			if (water.indexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, water.indexMemory, nullptr);
		}
		m_waterBodies.clear();

		// Wipe out any stale water buffers that were left waiting in the queue
		for (auto& stale : m_staleWaterQueue) {
			if (stale.vertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, stale.vertexBuffer, nullptr);
			if (stale.vertexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, stale.vertexMemory, nullptr);
			if (stale.indexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, stale.indexBuffer, nullptr);
			if (stale.indexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, stale.indexMemory, nullptr);
		}
		m_staleWaterQueue.clear();
	}

	// 6. Clean up Descriptor Sets, Pools, Layouts & Textures
	if (logicalDevice != VK_NULL_HANDLE) {
		if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
		if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);

		// Clean up the ONLY texture the renderer owns (Skybox)
		if (m_skyboxTexture.sampler != VK_NULL_HANDLE) vkDestroySampler(logicalDevice, m_skyboxTexture.sampler, nullptr);
		if (m_skyboxTexture.imageView != VK_NULL_HANDLE) vkDestroyImageView(logicalDevice, m_skyboxTexture.imageView, nullptr);
		if (m_skyboxTexture.image != VK_NULL_HANDLE) vkDestroyImage(logicalDevice, m_skyboxTexture.image, nullptr);
		// Note: Assuming your Texture struct calls this 'imageMemory', which was used in your original asset loading.
		if (m_skyboxTexture.imageMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, m_skyboxTexture.imageMemory, nullptr);

		// Let the AssetManager clean up all the other 37 leaked textures (sand, grass, rock)
		if (assetManager != nullptr) {
			assetManager->Cleanup(logicalDevice);
		}
	}

	// 7. Clean up Global Uniform, Light, Mesh, and Terrain buffers
	if (logicalDevice != VK_NULL_HANDLE) {
		// Uniform Buffer
		if (uniformBufferMapped != nullptr) vkUnmapMemory(logicalDevice, uniformBufferMemory);
		if (uniformBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, uniformBuffer, nullptr);
		if (uniformBufferMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, uniformBufferMemory, nullptr);

		// Light Buffer
		if (lightBufferMapped != nullptr) vkUnmapMemory(logicalDevice, lightBufferMemory);
		if (lightBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, lightBuffer, nullptr);
		if (lightBufferMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, lightBufferMemory, nullptr);

		// Global Static Meshes Buffer
		if (globalVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, globalVertexBuffer, nullptr);
		if (globalVertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, globalVertexBufferMemory, nullptr);
		if (globalIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, globalIndexBuffer, nullptr);
		if (globalIndexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, globalIndexBufferMemory, nullptr);

		// Global Terrain Buffer
		if (globalTerrainVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, globalTerrainVertexBuffer, nullptr);
		if (globalTerrainVertexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, globalTerrainVertexMemory, nullptr);
		if (globalTerrainIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, globalTerrainIndexBuffer, nullptr);
		if (globalTerrainIndexMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, globalTerrainIndexMemory, nullptr);

		// Dynamic extra allocated terrain buffers
		for (auto buf : m_allocatedTerrainBuffers) vkDestroyBuffer(logicalDevice, buf, nullptr);
		for (auto mem : m_allocatedTerrainMemory) vkFreeMemory(logicalDevice, mem, nullptr);

		// Persistent Staging Ring Buffer
		if (m_stagingBufferMapped != nullptr) vkUnmapMemory(logicalDevice, m_globalStagingMemory);
		if (m_globalStagingBuffer != VK_NULL_HANDLE) vkDestroyBuffer(logicalDevice, m_globalStagingBuffer, nullptr);
		if (m_globalStagingMemory != VK_NULL_HANDLE) vkFreeMemory(logicalDevice, m_globalStagingMemory, nullptr);
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
		if (uploadCommandPool != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice, uploadCommandPool, nullptr);
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