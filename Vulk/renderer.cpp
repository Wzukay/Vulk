#include "renderer.h"
#include "input.h"
#include "light.h"
#include "biome.h"

#include <iostream>
#include <cstring>
#include <stdexcept>
#include <set>
#include <unordered_map>
#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <glm/gtx/norm.hpp>

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	return VK_ERROR_EXTENSION_NOT_PRESENT;
}
static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
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
	m_pendingDeletionsGlobal.Push(std::move(del), m_globalFrameCounter + m_framesInFlight);
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

void VulkanRenderer::Initialize(
	int width,
	int height,
	const std::string& title)
{
	m_framesInFlight = std::clamp(static_cast<uint32_t>(g_Settings.framesInFlight), 2u, MAX_SUPPORTED_FRAMES_IN_FLIGHT);

	g_Settings.framesInFlight = static_cast<int>(m_framesInFlight);

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

	if (g_Settings.antiAliasingMode == 1) { // MSAA Enabled
		m_currentMsaaSamples = IntToSampleCount(g_Settings.msaaSamples);
		VkSampleCountFlagBits maxSupported = GetMaxUsableSampleCount();
		if (static_cast<int>(m_currentMsaaSamples) > static_cast<int>(maxSupported)) {
			m_currentMsaaSamples = maxSupported;
			g_Settings.msaaSamples = static_cast<int>(m_currentMsaaSamples);
		}
	}
	else { // None or FXAA
		m_currentMsaaSamples = VK_SAMPLE_COUNT_1_BIT;
	}

	CreateSwapChain();
	CreateImageViews();
	CreateOffscreenResolve();
	CreateWaterTarget();
	CreateDepthResources();
	CreateColorResources();

	CreateCommandPool();

	CreateSSAOResources();
	CreateSSAOPipeline();
	CreateSSAOBlurResources();
	CreateSSAOBlurPipeline();

	CreateCompositionPass();
	CreateFrameBuffers();
	CreateCommandBuffers();
	CreateSyncObjects();

	CreateHZBResources();

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
	g_AssetManager.CreateDefaultOrmTexture();
	InitBiomes();

	CreateGraphicsPipeline();
	CreateCompositionPipeline();
	CreateHZBPipeline();

	VkFormat depthFormat = FindDepthFormat();

	m_staticMeshRenderer.Init(logicalDevice, this);

	m_skybox.Init(logicalDevice, swapChainImageFormat, depthFormat, descriptorSetLayout, m_currentMsaaSamples);
	m_skybox.LoadTexture();
	m_skybox.UpdateDescriptor(logicalDevice, descriptorSet);

	m_terrainRenderer.Init(logicalDevice, this, &m_uploader, 5'000'000, 10'000'000);
	m_terrainRenderer.UpdateHZBDescriptor(hzbTarget.view, hzbTarget.sampler);
	m_staticMeshRenderer.UpdateHZBDescriptor(hzbTarget.view, hzbTarget.sampler);

	m_waterRenderer.Init(logicalDevice, this, &m_uploader, swapChainImageFormat, depthFormat, descriptorSetLayout, m_currentMsaaSamples);
	m_waterRenderer.SetSceneDepth(depthTarget.view, depthTarget.sampler);

	m_grassRenderer.Init(logicalDevice, this, &m_uploader, swapChainImageFormat, depthFormat, descriptorSetLayout, m_currentMsaaSamples);

	m_boidRenderer.Init(logicalDevice, this, &m_uploader, swapChainImageFormat, depthFormat, descriptorSetLayout, m_currentMsaaSamples);

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
	std::set<uint32_t> uniqueQueueFamilies = {
		indices.graphicsFamily.value(),
		indices.presentFamily.value()
	};

	float queuePriority = 1.0f;

	for (uint32_t queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType =
			VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.multiDrawIndirect = VK_TRUE;
	deviceFeatures.drawIndirectFirstInstance = VK_TRUE;

	VkPhysicalDeviceVulkan13Features features13{};
	features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	features13.dynamicRendering = VK_TRUE;
	features13.shaderDemoteToHelperInvocation = VK_TRUE;

	VkPhysicalDeviceVulkan11Features features11{};
	features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
	features11.shaderDrawParameters = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12{};
	features12.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.samplerFilterMinmax = VK_TRUE;
	features12.runtimeDescriptorArray = VK_TRUE;
	features12.descriptorBindingPartiallyBound = VK_TRUE;
	features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	features12.pNext = &features13;

	features11.pNext = &features12;

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount =
		static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.pEnabledFeatures = &deviceFeatures;
	createInfo.pNext = &features11;
	createInfo.enabledExtensionCount =
		static_cast<uint32_t>(deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = deviceExtensions.data();

	if (vkCreateDevice(
		physicalDevice,
		&createInfo,
		nullptr,
		&logicalDevice) != VK_SUCCESS) {
		throw std::runtime_error(
			"Critical Failure: Unable to build raw Vulkan device.");
	}

	vkGetDeviceQueue(
		logicalDevice,
		indices.graphicsFamily.value(),
		0,
		&graphicsQueue);

	vkGetDeviceQueue(
		logicalDevice,
		indices.presentFamily.value(),
		0,
		&presentQueue);
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
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	// If minimized or closing, pause until restored or return if shutting down
	while (width == 0 || height == 0) {
		if (glfwWindowShouldClose(window) || m_isShuttingDown) return;
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	vkDeviceWaitIdle(logicalDevice);

	// --- 1. Destroy Framebuffers & Image Views ---
	for (auto framebuffer : swapChainFramebuffers) {
		vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
	}
	swapChainFramebuffers.clear();

	for (auto imageView : swapChainImageViews) {
		vkDestroyImageView(logicalDevice, imageView, nullptr);
	}
	swapChainImageViews.clear();

	// --- 2. Destroy Core Render Targets ---
	depthTarget.Destroy(logicalDevice);
	colorTarget.Destroy(logicalDevice);

	if (swapChain != VK_NULL_HANDLE) { vkDestroySwapchainKHR(logicalDevice, swapChain, nullptr); swapChain = VK_NULL_HANDLE; }

	// --- 3. Destroy SSAO Targets (main pass + blur pass) ---
	if (ssaoUBOMapped != nullptr) { vkUnmapMemory(logicalDevice, ssaoUBOMemory); ssaoUBOMapped = nullptr; }
	DestroyBuffer(ssaoUBO, ssaoUBOMemory);

	if (ssaoPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(logicalDevice, ssaoPipeline, nullptr); ssaoPipeline = VK_NULL_HANDLE; }
	if (ssaoPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(logicalDevice, ssaoPipelineLayout, nullptr); ssaoPipelineLayout = VK_NULL_HANDLE; }
	if (ssaoDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(logicalDevice, ssaoDescriptorSetLayout, nullptr); ssaoDescriptorSetLayout = VK_NULL_HANDLE; }
	if (ssaoDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(logicalDevice, ssaoDescriptorPool, nullptr); ssaoDescriptorPool = VK_NULL_HANDLE; }
	ssaoTarget.Destroy(logicalDevice);
	ssaoNoiseTarget.Destroy(logicalDevice);

	ssaoPingPongTarget.Destroy(logicalDevice);
	ssaoBlurTarget.Destroy(logicalDevice);

	if (ssaoBlurPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(logicalDevice, ssaoBlurPipeline, nullptr); ssaoBlurPipeline = VK_NULL_HANDLE; }
	if (ssaoBlurPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(logicalDevice, ssaoBlurPipelineLayout, nullptr); ssaoBlurPipelineLayout = VK_NULL_HANDLE; }
	if (ssaoBlurDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(logicalDevice, ssaoBlurDescriptorSetLayout, nullptr); ssaoBlurDescriptorSetLayout = VK_NULL_HANDLE; }
	if (ssaoBlurDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(logicalDevice, ssaoBlurDescriptorPool, nullptr); ssaoBlurDescriptorPool = VK_NULL_HANDLE; }

	// --- 4. Destroy HZB Targets ---
	if (hzbPipeline != VK_NULL_HANDLE) {
		vkDestroyPipeline(logicalDevice, hzbPipeline, nullptr);
		vkDestroyPipelineLayout(logicalDevice, hzbPipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(logicalDevice, hzbDescriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(logicalDevice, hzbDescriptorPool, nullptr);
		hzbPipeline = VK_NULL_HANDLE;
	}
	if (hzbTarget.image != VK_NULL_HANDLE) {
		for (auto view : hzbMipViews) vkDestroyImageView(logicalDevice, view, nullptr);
		hzbMipViews.clear();
		hzbTarget.Destroy(logicalDevice);
	}

	// --- 5. Destroy Composition Phase & Render Pass ---
	if (compositionPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(logicalDevice, compositionPipeline, nullptr); compositionPipeline = VK_NULL_HANDLE; }
	if (compositionPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(logicalDevice, compositionPipelineLayout, nullptr); compositionPipelineLayout = VK_NULL_HANDLE; }
	if (compositionDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(logicalDevice, compositionDescriptorPool, nullptr); compositionDescriptorPool = VK_NULL_HANDLE; }
	if (compositionDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(logicalDevice, compositionDescriptorSetLayout, nullptr); compositionDescriptorSetLayout = VK_NULL_HANDLE; }

	offscreenTarget.Destroy(logicalDevice);
	waterTarget.Destroy(logicalDevice);

	// THE CRITICAL FIX: Destroy the Render Pass before recreating it!
	if (compositionRenderPass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(logicalDevice, compositionRenderPass, nullptr);
		compositionRenderPass = VK_NULL_HANDLE;
	}

	// --- 6. Recreate Everything ---
	CreateSwapChain();
	CreateImageViews();
	CreateOffscreenResolve();
	CreateWaterTarget();
	CreateColorResources();
	CreateDepthResources();

	CreateHZBResources();
	CreateHZBPipeline();

	m_terrainRenderer.UpdateHZBDescriptor(hzbTarget.view, hzbTarget.sampler);
	m_staticMeshRenderer.UpdateHZBDescriptor(hzbTarget.view, hzbTarget.sampler); 
	m_waterRenderer.SetSceneDepth(depthTarget.view, depthTarget.sampler);

	CreateSSAOResources();
	CreateSSAOPipeline();
	CreateSSAOBlurResources();
	CreateSSAOBlurPipeline();

	CreateCompositionPass();
	CreateFrameBuffers();

	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		for (uint32_t j = 0; j < NUM_RENDER_THREADS; j++) {
			vkFreeCommandBuffers(logicalDevice, threadCommandPools[i][j], 1, &threadCommandBuffers[i][j]);
		}
	}

	vkFreeCommandBuffers(logicalDevice, commandPool, static_cast<uint32_t>(commandBuffers.size()), commandBuffers.data());
	CreateCommandBuffers();

	imagesInFlight.assign(swapChainImages.size(), VK_NULL_HANDLE);

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
	init_info.UseDynamicRendering = false;
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
void VulkanRenderer::CreateCommandPool() {
	QueueFamilyIndices queueFamilyIndices = FindQueueFamilies(physicalDevice);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

	if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create command pool");
	}

	threadCommandPools.resize(m_framesInFlight);
	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		threadCommandPools[i].resize(NUM_RENDER_THREADS);
		for (uint32_t j = 0; j < NUM_RENDER_THREADS; j++) {
			if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &threadCommandPools[i][j]) != VK_SUCCESS) {
				throw std::runtime_error("Failed to create secondary command pool");
			}
		}
	}
}
void VulkanRenderer::CreateCommandBuffers() {
	commandBuffers.resize(swapChainImages.size());

	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

	if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
		throw std::runtime_error("Failed to distribute command routing tracks.");
	}

	// --- NEW: Secondary Command Buffers ---
	threadCommandBuffers.resize(m_framesInFlight);
	for (uint32_t i = 0; i < m_framesInFlight; i++) {
		threadCommandBuffers[i].resize(NUM_RENDER_THREADS);
		for (uint32_t j = 0; j < NUM_RENDER_THREADS; j++) {
			VkCommandBufferAllocateInfo secAllocInfo{};
			secAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			secAllocInfo.commandPool = threadCommandPools[i][j];
			secAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY; // Critical!
			secAllocInfo.commandBufferCount = 1;

			if (vkAllocateCommandBuffers(logicalDevice, &secAllocInfo, &threadCommandBuffers[i][j]) != VK_SUCCESS) {
				throw std::runtime_error("Failed to allocate secondary command buffer");
			}
		}
	}
}
void VulkanRenderer::CreateSyncObjects() {
	imageAvailableSemaphores.resize(m_framesInFlight);
	renderFinishedSemaphores.resize(m_framesInFlight);
	inFlightFences.resize(m_framesInFlight);

	imagesInFlight.assign(swapChainImages.size(), VK_NULL_HANDLE);

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // Crucial: start opened

	for (size_t i = 0; i < m_framesInFlight; i++) {
		if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
			vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
			vkCreateFence(logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {

			throw std::runtime_error("Failed to initialize multi-frame synchronization primitives.");
		}
	}
}
void VulkanRenderer::CreateOffscreenResolve() {
	CreateImage(swapChainExtent.width, swapChainExtent.height, 1, VK_SAMPLE_COUNT_1_BIT, swapChainImageFormat,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, offscreenTarget.image, offscreenTarget.memory);

	offscreenTarget.view = CreateImageView(offscreenTarget.image, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &offscreenTarget.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create offscreen sampler.");
	}
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

	VkDescriptorSetLayoutBinding ormSamplerBinding{};
	ormSamplerBinding.binding = 5;
	ormSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	ormSamplerBinding.descriptorCount = 500;
	ormSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	ormSamplerBinding.pImmutableSamplers = nullptr;

	std::array<VkDescriptorSetLayoutBinding, 6> bindings = {
		uboLayoutBinding, lightLayoutBinding, samplerLayoutBinding, normalSamplerBinding, cubemapBinding, ormSamplerBinding
	};

	VkDescriptorBindingFlags bindingFlags[6] = { 0, 0,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, 0,
		VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
	};

	VkDescriptorSetLayoutBindingFlagsCreateInfo layoutBindingFlags{};
	layoutBindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
	layoutBindingFlags.bindingCount = 6;
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

	vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
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

void VulkanRenderer::CreateUniformBuffer() {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

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
		2.0f,
		g_Settings.renderDistance);
	ubo.proj[1][1] *= -1;

	ubo.ambient = 0.25f;                // brighter ambient
	ubo.specularPower = 8.0f;           // softer highlights
	ubo.lightCount = static_cast<uint32_t>(currentLights.size());

	ubo.cameraPos = cam.pos;
	
	ubo.fogStart = g_Settings.GetFogStart();
	ubo.fogEnd = g_Settings.GetFogEnd();
	ubo.fadeParams = glm::vec4(
		g_Settings.GetStaticFadeStart(),
		g_Settings.GetStaticFadeEnd(),
		g_Settings.GetGrassFadeStart(),
		g_Settings.GetGrassFadeEnd()
	);

	ubo.screenSize = glm::vec2((float)swapChainExtent.width, (float)swapChainExtent.height);

	ubo.inverseViewProj = glm::inverse(ubo.proj * ubo.view);
	ubo.inverseProj = glm::inverse(ubo.proj);
	ubo.inverseView = glm::inverse(ubo.view);

	m_currentViewProj = ubo.proj * ubo.view;

	UpdateFrustumPlanes(ubo.proj * ubo.view);

	if (!currentLights.empty()) {
		ubo.sunDirection = currentLights[0].positionOrDir;
		ubo.sunColor = currentLights[0].color;

		float sunY = ubo.sunDirection.y;

		float ambientBlend = glm::smoothstep(-0.1f, 0.3f, sunY);

		// Dropped the night-time ambient floor to 0.01
		ubo.ambient = glm::mix(0.01f, 0.22f, ambientBlend);
	}
	else {
		// SAFE FALLBACK FOR FRAME 1
		ubo.sunDirection = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		ubo.sunColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
		ubo.ambient = 0.22f;
	}

	memcpy(uniformBufferMapped, &ubo, sizeof(ubo));

	if (g_Settings.enableSSAO && ssaoUBOMapped != nullptr) {
		ssaoUBOMapped->projection = ubo.proj;
		ssaoUBOMapped->inverseProjection = ubo.inverseProj;
	}
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

void VulkanRenderer::CreateImGui() {
	CreateImGuiDescriptorPool();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui_ImplGlfw_InitForVulkan(window, true);

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

	// --- FIX: TRADITIONAL SETUP ---
	init_info.UseDynamicRendering = false;
	init_info.PipelineInfoMain.RenderPass = compositionRenderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

	ImGui_ImplVulkan_Init(&init_info);
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
void VulkanRenderer::CreateCompositionPass() {
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = swapChainImageFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // Native swapchain layout

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
void VulkanRenderer::CreateFrameBuffers() {
	swapChainFramebuffers.resize(swapChainImageViews.size());
	for (size_t i = 0; i < swapChainImageViews.size(); i++) {
		VkImageView attachment[] = { swapChainImageViews[i] };
		VkFramebufferCreateInfo fbInfo{};
		fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		fbInfo.renderPass = compositionRenderPass;
		fbInfo.attachmentCount = 1;
		fbInfo.pAttachments = attachment;
		fbInfo.width = swapChainExtent.width;
		fbInfo.height = swapChainExtent.height;
		fbInfo.layers = 1;

		if (vkCreateFramebuffer(logicalDevice, &fbInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS)
			throw std::runtime_error("Failed to create swapchain framebuffer");
	}
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

void VulkanRenderer::CreateImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format,
	VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
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
	CreateImage(swapChainExtent.width, swapChainExtent.height, 1, m_currentMsaaSamples, depthFormat,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthTarget.image, depthTarget.memory);
	depthTarget.view = CreateImageView(depthTarget.image, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

	if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &depthTarget.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create depth sampler.");
	}
}
void VulkanRenderer::CreateColorResources() {
	if (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
		VkFormat colorFormat = swapChainImageFormat;
		// NOW USES INTERNAL RESOLUTION
		CreateImage(swapChainExtent.width, swapChainExtent.height, 1, m_currentMsaaSamples, colorFormat,
			VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorTarget.image, colorTarget.memory);
		colorTarget.view = CreateImageView(colorTarget.image, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	}
}
void VulkanRenderer::CreateSSAOResources() {
	uint32_t width = std::max(1u, swapChainExtent.width / 2);
	uint32_t height = std::max(1u, swapChainExtent.height / 2);

	CreateImage(width, height, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8_UNORM,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ssaoTarget.image, ssaoTarget.memory);

	ssaoTarget.view = CreateImageView(ssaoTarget.image, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &ssaoTarget.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create SSAO sampler.");
	}

	CreateBuffer(sizeof(SSAOUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		ssaoUBO, ssaoUBOMemory);
	vkMapMemory(logicalDevice, ssaoUBOMemory, 0, sizeof(SSAOUBO), 0, (void**)&ssaoUBOMapped);

	std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
	std::default_random_engine generator;
	SSAOUBO uboData{};

	for (int i = 0; i < 24; ++i) {
		glm::vec3 sample(randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator));
		sample = glm::normalize(sample) * randomFloats(generator);
		float scale = (float)i / 24.0f;
		sample *= glm::mix(0.1f, 1.0f, scale * scale);
		uboData.samples[i] = glm::vec4(sample, 0.0f);
	}

	memcpy(ssaoUBOMapped, &uboData, sizeof(SSAOUBO));

	std::vector<glm::vec4> ssaoNoise(16);
	for (int i = 0; i < 16; i++) {
		ssaoNoise[i] = glm::vec4(
			randomFloats(generator) * 2.0f - 1.0f,
			randomFloats(generator) * 2.0f - 1.0f,
			0.0f, 0.0f);
	}

	// 2. Create Staging Buffer
	VkDeviceSize noiseSize = ssaoNoise.size() * sizeof(glm::vec4);
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	CreateBuffer(noiseSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(logicalDevice, stagingBufferMemory, 0, noiseSize, 0, &data);
	memcpy(data, ssaoNoise.data(), (size_t)noiseSize);
	vkUnmapMemory(logicalDevice, stagingBufferMemory);

	// 3. Create 4x4 Noise Image
	CreateImage(4, 4, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ssaoNoiseTarget.image, ssaoNoiseTarget.memory);
	ssaoNoiseTarget.view = CreateImageView(ssaoNoiseTarget.image, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	VkSamplerCreateInfo noiseSamplerInfo{};
	noiseSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	noiseSamplerInfo.magFilter = VK_FILTER_NEAREST; // Must be nearest for raw noise
	noiseSamplerInfo.minFilter = VK_FILTER_NEAREST;
	noiseSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	noiseSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // TILE THE TEXTURE
	noiseSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	noiseSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	vkCreateSampler(logicalDevice, &noiseSamplerInfo, nullptr, &ssaoNoiseTarget.sampler);

	// 4. Copy Buffer to Image
	VkCommandBuffer cmd = BeginSingleTimeCommands();
	TransitionImageLayout(cmd, ssaoNoiseTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	VkBufferImageCopy region{};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { 4, 4, 1 };
	vkCmdCopyBufferToImage(cmd, stagingBuffer, ssaoNoiseTarget.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	TransitionImageLayout(cmd, ssaoNoiseTarget.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	EndSingleTimeCommands(cmd);

	DestroyBuffer(stagingBuffer, stagingBufferMemory);
}
void VulkanRenderer::CreateSSAOBlurResources() {
	uint32_t width = std::max(1u, swapChainExtent.width / 2);
	uint32_t height = std::max(1u, swapChainExtent.height / 2);

	// 1. Ping-Pong intermediate image (Horizontal blur output)
	CreateImage(width, height, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8_UNORM,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ssaoPingPongTarget.image, ssaoPingPongTarget.memory);
	ssaoPingPongTarget.view = CreateImageView(ssaoPingPongTarget.image, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	// 2. Final blur image (Vertical blur output)
	CreateImage(width, height, 1, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R8_UNORM,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ssaoBlurTarget.image, ssaoBlurTarget.memory);
	ssaoBlurTarget.view = CreateImageView(ssaoBlurTarget.image, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}
void VulkanRenderer::CreateHZBResources() {
	uint32_t width = swapChainExtent.width;
	uint32_t height = swapChainExtent.height;
	hzbDimensions = glm::vec2((float)width, (float)height);

	// Calculate how many mip levels we need for the screen size
	hzbMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

	// Create the image with STORAGE usage so compute shaders can write to it
	CreateImage(width, height, hzbMipLevels, VK_SAMPLE_COUNT_1_BIT, VK_FORMAT_R32_SFLOAT,
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, hzbTarget.image, hzbTarget.memory);

	// Create the global image view containing all mips
	hzbTarget.view = CreateImageView(hzbTarget.image, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, hzbMipLevels);

	// Create individual image views for EACH mip level so we can bind them independently in compute
	hzbMipViews.resize(hzbMipLevels);
	for (uint32_t i = 0; i < hzbMipLevels; i++) {
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = hzbTarget.image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = VK_FORMAT_R32_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = i;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };

		if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &hzbMipViews[i]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create HZB mip image view!");
		}
	}

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = static_cast<float>(hzbMipLevels);

	// Vulkan 1.3 allows hardware min/max filtering
	VkSamplerReductionModeCreateInfo reductionInfo{};
	reductionInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_REDUCTION_MODE_CREATE_INFO;
	reductionInfo.reductionMode = VK_SAMPLER_REDUCTION_MODE_MAX;
	samplerInfo.pNext = &reductionInfo;

	if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &hzbTarget.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create HZB sampler.");
	}

	VkCommandBuffer cmd = BeginSingleTimeCommands();

	VkImageMemoryBarrier initBarrier{};
	initBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	initBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	initBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	initBarrier.image = hzbTarget.image;
	initBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	initBarrier.subresourceRange.baseMipLevel = 0;
	initBarrier.subresourceRange.levelCount = hzbMipLevels;
	initBarrier.subresourceRange.baseArrayLayer = 0;
	initBarrier.subresourceRange.layerCount = 1;
	initBarrier.srcAccessMask = 0;
	initBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &initBarrier);

	// Clear the HZB to 1.0f (the farthest possible depth) so nothing gets accidentally culled on Frame 1
	VkClearColorValue clearColor = { {1.0f, 1.0f, 1.0f, 1.0f} };
	vkCmdClearColorImage(cmd, hzbTarget.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &initBarrier.subresourceRange);

	// Transition the whole pyramid to READ_ONLY for the culling shader
	initBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	initBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	initBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	initBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &initBarrier);

	EndSingleTimeCommands(cmd);
}
void VulkanRenderer::GenerateHZB(VkCommandBuffer commandBuffer) {
	VkImageMemoryBarrier initBarrier{};
	initBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	initBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	initBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	initBarrier.image = hzbTarget.image;
	initBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	initBarrier.subresourceRange.baseMipLevel = 0;
	initBarrier.subresourceRange.levelCount = hzbMipLevels; // Apply to the whole pyramid!
	initBarrier.subresourceRange.baseArrayLayer = 0;
	initBarrier.subresourceRange.layerCount = 1;
	initBarrier.srcAccessMask = 0;
	initBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &initBarrier);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hzbPipeline);

	uint32_t currentWidth = static_cast<uint32_t>(GetInternalWidth());
	uint32_t currentHeight = static_cast<uint32_t>(GetInternalHeight());

	for (uint32_t i = 0; i < hzbMipLevels; i++) {
		// If not the first mip, transition the *previous* mip to READ_ONLY so we can sample from it
		if (i > 0) {
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = hzbTarget.image;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(commandBuffer,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &barrier);
		}

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hzbPipelineLayout, 0, 1, &hzbDescriptorSets[i], 0, nullptr);

		glm::vec2 outSize(currentWidth, currentHeight);
		vkCmdPushConstants(commandBuffer, hzbPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec2), &outSize);

		// 16x16 local workgroup size (defined in the GLSL shader)
		uint32_t groupCountX = (currentWidth + 15) / 16;
		uint32_t groupCountY = (currentHeight + 15) / 16;
		vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);

		currentWidth = std::max(1u, currentWidth / 2);
		currentHeight = std::max(1u, currentHeight / 2);
	}

	// Transition the final mip to READ_ONLY so the whole pyramid is ready for the terrain culling shader
	VkImageMemoryBarrier finalBarrier{};
	finalBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	finalBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	finalBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	finalBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	finalBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	finalBarrier.image = hzbTarget.image;
	finalBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	finalBarrier.subresourceRange.baseMipLevel = hzbMipLevels - 1;
	finalBarrier.subresourceRange.levelCount = 1;
	finalBarrier.subresourceRange.baseArrayLayer = 0;
	finalBarrier.subresourceRange.layerCount = 1;
	finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &finalBarrier);
}

void VulkanRenderer::CreateGraphicsPipeline() {
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags =
		VK_SHADER_STAGE_VERTEX_BIT |
		VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(PushConstants);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType =
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(
		logicalDevice,
		&pipelineLayoutInfo,
		nullptr,
		&pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error(
			"Failed to build shared pipeline uniform layout object.");
	}

	auto bindingDescription =
		ModelVertex::getBindingDescription();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology =
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkViewport viewport{
		0.0f,
		0.0f,
		static_cast<float>(swapChainExtent.width),
		static_cast<float>(swapChainExtent.height),
		0.0f,
		1.0f
	};

	VkRect2D scissor{ {0, 0}, swapChainExtent };

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.pViewports = &viewport;
	viewportState.scissorCount = 1;
	viewportState.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = m_currentMsaaSamples;
	multisampling.alphaToCoverageEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType =
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	auto compilePipelineHandle =
		[&](
			const std::string& vertPath,
			const std::string& fragPath,
			PipelineVertexType vertexType) -> VkPipeline
		{
			auto vertCode = ReadFile(vertPath);
			auto fragCode = ReadFile(fragPath);

			VkShaderModule vertModule = CreateShaderModule(logicalDevice, vertCode);

			VkShaderModule fragModule = CreateShaderModule(logicalDevice, fragCode);

			VkPipelineShaderStageCreateInfo stages[] = {
				{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main" },
				{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main" }
			};

			std::vector<VkVertexInputBindingDescription> bindings = {
				ModelVertex::getBindingDescription()
			};

			std::vector<VkVertexInputAttributeDescription> attributes;

			switch (vertexType) {
			case PipelineVertexType::Terrain: {
				auto terrainAttrs = ModelVertex::getTerrainAttributeDescriptions();
				attributes.assign(terrainAttrs.begin(), terrainAttrs.end());
				break;
			}
			case PipelineVertexType::Static: {
				auto staticAttrs = ModelVertex::getStaticAttributeDescriptions();
				attributes.assign(staticAttrs.begin(), staticAttrs.end());
				break;
			}
			case PipelineVertexType::Instanced: {
				auto staticAttrs = ModelVertex::getStaticAttributeDescriptions();
				attributes.assign(staticAttrs.begin(), staticAttrs.end());

				bindings.push_back(InstanceData::getBindingDescription());
				auto instanceAttributes = InstanceData::getAttributeDescriptions();
				attributes.insert(attributes.end(), instanceAttributes.begin(), instanceAttributes.end());
				break;
			}
			}

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
			vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
			vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
			vertexInputInfo.pVertexBindingDescriptions = bindings.data();
			vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
			vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

			VkFormat colorFormat = swapChainImageFormat;
			VkFormat depthFormat = FindDepthFormat();

			VkPipelineRenderingCreateInfo renderingInfo{};
			renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
			renderingInfo.colorAttachmentCount = 1;
			renderingInfo.pColorAttachmentFormats = &colorFormat;
			renderingInfo.depthAttachmentFormat = depthFormat;

			VkGraphicsPipelineCreateInfo pipelineInfo{};
			pipelineInfo.sType =
				VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipelineInfo.stageCount = 2;
			pipelineInfo.pStages = stages;
			pipelineInfo.pVertexInputState = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &inputAssembly;
			pipelineInfo.pViewportState = &viewportState;
			pipelineInfo.pRasterizationState = &rasterizer;
			pipelineInfo.pMultisampleState = &multisampling;
			pipelineInfo.pColorBlendState = &colorBlending;
			pipelineInfo.pDepthStencilState = &depthStencil;
			pipelineInfo.pDynamicState = &dynamicState;
			pipelineInfo.layout = pipelineLayout;
			pipelineInfo.pNext = &renderingInfo;
			pipelineInfo.renderPass = VK_NULL_HANDLE;

			VkPipeline pipeline = VK_NULL_HANDLE;

			if (vkCreateGraphicsPipelines(
				logicalDevice,
				VK_NULL_HANDLE,
				1,
				&pipelineInfo,
				nullptr,
				&pipeline) != VK_SUCCESS) {
				throw std::runtime_error(
					"Failed to compile graphics pipeline: " +
					vertPath + " / " + fragPath);
			}

			vkDestroyShaderModule(logicalDevice, fragModule, nullptr);
			vkDestroyShaderModule(logicalDevice, vertModule, nullptr);

			return pipeline;
		};

	terrainPipeline = compilePipelineHandle(
        "shaders/terrain_vert.spv",
        "shaders/terrain_frag.spv",
        PipelineVertexType::Terrain);

    staticPipeline = compilePipelineHandle(
        "shaders/static_vert.spv",
        "shaders/static_frag.spv",
        PipelineVertexType::Static);

    instancedPipeline = compilePipelineHandle(
        "shaders/instanced_vert.spv",
        "shaders/instanced_frag.spv",
        PipelineVertexType::Instanced);
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
void VulkanRenderer::CreateCompositionPipeline() {
	VkDescriptorSetLayoutBinding samplerBinding{};
	samplerBinding.binding = 0;
	samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerBinding.descriptorCount = 1;
	samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding ssaoBinding{};
	ssaoBinding.binding = 1;
	ssaoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	ssaoBinding.descriptorCount = 1;
	ssaoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding waterBinding{};
	waterBinding.binding = 2;
	waterBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	waterBinding.descriptorCount = 1;
	waterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutBinding bindings[] = { samplerBinding, ssaoBinding, waterBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 3;
	layoutInfo.pBindings = bindings;
	vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &compositionDescriptorSetLayout);

	// 2. Pool size must be 2 to hold both textures
	VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 };
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;
	vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &compositionDescriptorPool);

	// 3. Allocate Set
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = compositionDescriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &compositionDescriptorSetLayout;
	vkAllocateDescriptorSets(logicalDevice, &allocInfo, &compositionDescriptorSet);

	// 4. Update Descriptor Sets for both images
	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageInfo.imageView = offscreenTarget.view;
	imageInfo.sampler = offscreenTarget.sampler;

	VkDescriptorImageInfo ssaoImageInfo{};
	ssaoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	ssaoImageInfo.imageView = ssaoBlurTarget.view;
	ssaoImageInfo.sampler = ssaoTarget.sampler;

	VkDescriptorImageInfo waterImageInfo{};
	waterImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	waterImageInfo.imageView = waterTarget.view;
	waterImageInfo.sampler = waterTarget.sampler;

	VkWriteDescriptorSet descriptorWrites[3]{};
	descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[0].dstSet = compositionDescriptorSet;
	descriptorWrites[0].dstBinding = 0;
	descriptorWrites[0].dstArrayElement = 0;
	descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[0].descriptorCount = 1;
	descriptorWrites[0].pImageInfo = &imageInfo;

	descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[1].dstSet = compositionDescriptorSet;
	descriptorWrites[1].dstBinding = 1;
	descriptorWrites[1].dstArrayElement = 0;
	descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[1].descriptorCount = 1;
	descriptorWrites[1].pImageInfo = &ssaoImageInfo;

	descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrites[2].dstSet = compositionDescriptorSet;
	descriptorWrites[2].dstBinding = 2;
	descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	descriptorWrites[2].descriptorCount = 1;
	descriptorWrites[2].pImageInfo = &waterImageInfo;

	vkUpdateDescriptorSets(logicalDevice, 3, descriptorWrites, 0, nullptr);

	// 5. Push Constants (FSR + SSAO Toggle)
	VkPushConstantRange fsrPushConstantRange{};
	fsrPushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	fsrPushConstantRange.offset = 0;
	fsrPushConstantRange.size = sizeof(FSRConstants);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &compositionDescriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &fsrPushConstantRange;
	vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &compositionPipelineLayout);

	// 6. Shaders & Pipeline state
	std::string fragPath = "shaders/quad_frag.spv";
	if (g_Settings.enableFSR) {
		fragPath = "shaders/fsr_frag.spv";
	}
	else if (g_Settings.antiAliasingMode == 2) {
		fragPath = "shaders/fxaa_frag.spv";
	}
	auto vertCode = ReadFile("shaders/quad_vert.spv");
	auto fragCode = ReadFile(fragPath);
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

	// Hybrid Rendering Linkage
	pInfo.pNext = nullptr;
	pInfo.renderPass = compositionRenderPass;

	if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pInfo, nullptr, &compositionPipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create composition pipeline.");
	}

	vkDestroyShaderModule(logicalDevice, fMod, nullptr);
	vkDestroyShaderModule(logicalDevice, vMod, nullptr);
}
void VulkanRenderer::CreateSSAOPipeline() {
	VkDescriptorSetLayoutBinding depthBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutBinding uboBinding{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutBinding noiseBinding{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutBinding bindings[] = { depthBinding, uboBinding, noiseBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 3, bindings };
	vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &ssaoDescriptorSetLayout);

	VkDescriptorPoolSize poolSizes[] = { {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1} };
	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 2, poolSizes };
	vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &ssaoDescriptorPool);

	VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, ssaoDescriptorPool, 1, &ssaoDescriptorSetLayout };
	vkAllocateDescriptorSets(logicalDevice, &allocInfo, &ssaoDescriptorSet);

	VkDescriptorImageInfo depthInfo{ depthTarget.sampler, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorBufferInfo uboInfo{ ssaoUBO, 0, sizeof(SSAOUBO) };
	VkDescriptorImageInfo noiseInfo{ ssaoNoiseTarget.sampler, ssaoNoiseTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }; 

	VkWriteDescriptorSet writes[3]{};
	writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo, nullptr, nullptr };
	writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoDescriptorSet, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo, nullptr };
	writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoDescriptorSet, 2, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &noiseInfo, nullptr, nullptr }; // NEW
	vkUpdateDescriptorSets(logicalDevice, 3, writes, 0, nullptr);

	VkPushConstantRange pcRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPushConstants) };
	VkPipelineLayoutCreateInfo pLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &ssaoDescriptorSetLayout, 1, &pcRange };
	vkCreatePipelineLayout(logicalDevice, &pLayoutInfo, nullptr, &ssaoPipelineLayout);

	auto vertCode = ReadFile("shaders/quad_vert.spv"); // Reuses the fullscreen quad vertex shader
	auto fragCode = ReadFile("shaders/ssao_frag.spv");
	VkShaderModule vMod = CreateShaderModule(logicalDevice, vertCode);
	VkShaderModule fMod = CreateShaderModule(logicalDevice, fragCode);

	VkPipelineShaderStageCreateInfo stages[] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main" },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main" }
	};

	VkPipelineVertexInputStateCreateInfo vertInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
	VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };
	VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE };
	rasterizer.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT };
	VkPipelineColorBlendAttachmentState blendAttachment{ VK_FALSE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0xF };
	VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &blendAttachment };
	VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynStates };
	VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

	VkFormat format = VK_FORMAT_R8_UNORM;
	VkPipelineRenderingCreateInfo renderInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1, &format, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED };

	VkGraphicsPipelineCreateInfo pInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &renderInfo, 0, 2, stages, &vertInput, &inputAssembly, nullptr, &viewportState, &rasterizer, &multisampling, &depthStencil, &colorBlending, &dynamicState, ssaoPipelineLayout, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, -1 };
	vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pInfo, nullptr, &ssaoPipeline);

	vkDestroyShaderModule(logicalDevice, fMod, nullptr);
	vkDestroyShaderModule(logicalDevice, vMod, nullptr);
}
void VulkanRenderer::CreateSSAOBlurPipeline() {
	// Bindings: Binding 0 = SSAO Texture input, Binding 1 = Depth texture input
	VkDescriptorSetLayoutBinding ssaoBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutBinding depthBinding{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
	VkDescriptorSetLayoutBinding bindings[] = { ssaoBinding, depthBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, bindings };
	vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &ssaoBlurDescriptorSetLayout);

	// Pool for 2 descriptor sets (Horizontal and Vertical passes)
	VkDescriptorPoolSize poolSizes[] = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 } // 2 sets * 2 samplers each = 4
	};
	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, poolSizes };
	poolInfo.maxSets = 2; // We need 2 sets allocated from this pool
	vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &ssaoBlurDescriptorPool);

	// Allocate 2 Descriptor Sets
	std::vector<VkDescriptorSetLayout> layouts(2, ssaoBlurDescriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, ssaoBlurDescriptorPool, 2, layouts.data() };

	VkDescriptorSet blurSets[2];
	vkAllocateDescriptorSets(logicalDevice, &allocInfo, blurSets);
	ssaoBlurDescriptorSetHorizontal = blurSets[0];
	ssaoBlurDescriptorSetVertical = blurSets[1];

	// Horizontal Set: Reads raw ssaoImage + depthImage
	VkDescriptorImageInfo rawSSAOInfo{ ssaoTarget.sampler, ssaoTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	VkDescriptorImageInfo depthInfo{ depthTarget.sampler, depthTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	VkWriteDescriptorSet hWrites[2]{};
	hWrites[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoBlurDescriptorSetHorizontal, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &rawSSAOInfo, nullptr, nullptr };
	hWrites[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoBlurDescriptorSetHorizontal, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo, nullptr, nullptr };
	vkUpdateDescriptorSets(logicalDevice, 2, hWrites, 0, nullptr);

	// Vertical Set: Reads ssaoPingPongImage + depthImage
	VkDescriptorImageInfo pingPongInfo{ ssaoTarget.sampler, ssaoPingPongTarget.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

	VkWriteDescriptorSet vWrites[2]{};
	vWrites[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoBlurDescriptorSetVertical, 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &pingPongInfo, nullptr, nullptr };
	vWrites[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, ssaoBlurDescriptorSetVertical, 1, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &depthInfo, nullptr, nullptr };
	vkUpdateDescriptorSets(logicalDevice, 2, vWrites, 0, nullptr);

	// Pipeline Layout
	VkPushConstantRange pcRange{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOBlurPushConstants) };
	VkPipelineLayoutCreateInfo pLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &ssaoBlurDescriptorSetLayout, 1, &pcRange };
	vkCreatePipelineLayout(logicalDevice, &pLayoutInfo, nullptr, &ssaoBlurPipelineLayout);

	// Shader Setup
	auto vertCode = ReadFile("shaders/quad_vert.spv");
	auto fragCode = ReadFile("shaders/ssao_blur_frag.spv");
	VkShaderModule vMod = CreateShaderModule(logicalDevice, vertCode);
	VkShaderModule fMod = CreateShaderModule(logicalDevice, fragCode);

	VkPipelineShaderStageCreateInfo stages[] = {
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vMod, "main" },
		{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fMod, "main" }
	};

	VkPipelineVertexInputStateCreateInfo vertInput{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
	VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr };

	VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_CLOCKWISE };
	rasterizer.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT };

	VkPipelineColorBlendAttachmentState blendAttachment{ VK_FALSE, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ZERO, VK_BLEND_OP_ADD, 0xF };
	VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &blendAttachment };

	VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dynStates };
	VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

	VkFormat format = VK_FORMAT_R8_UNORM;
	VkPipelineRenderingCreateInfo renderInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, nullptr, 0, 1, &format, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED };

	VkGraphicsPipelineCreateInfo pInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &renderInfo, 0, 2, stages, &vertInput, &inputAssembly, nullptr, &viewportState, &rasterizer, &multisampling, &depthStencil, &colorBlending, &dynamicState, ssaoBlurPipelineLayout, VK_NULL_HANDLE, 0, VK_NULL_HANDLE, -1 };
	vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pInfo, nullptr, &ssaoBlurPipeline);

	vkDestroyShaderModule(logicalDevice, fMod, nullptr);
	vkDestroyShaderModule(logicalDevice, vMod, nullptr);
}
void VulkanRenderer::CreateHZBPipeline() {
	VkDescriptorSetLayoutBinding inputBinding{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	VkDescriptorSetLayoutBinding outputBinding{ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
	VkDescriptorSetLayoutBinding bindings[] = { inputBinding, outputBinding };

	VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, bindings };
	if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &hzbDescriptorSetLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create HZB descriptor set layout");
	}

	VkDescriptorPoolSize poolSizes[] = {
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, hzbMipLevels},
		{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, hzbMipLevels}
	};
	VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, hzbMipLevels, 2, poolSizes };
	vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &hzbDescriptorPool);

	std::vector<VkDescriptorSetLayout> layouts(hzbMipLevels, hzbDescriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, hzbDescriptorPool, hzbMipLevels, layouts.data() };
	hzbDescriptorSets.resize(hzbMipLevels);
	vkAllocateDescriptorSets(logicalDevice, &allocInfo, hzbDescriptorSets.data());

	for (uint32_t i = 0; i < hzbMipLevels; i++) {
		VkDescriptorImageInfo inputInfo{};
		inputInfo.sampler = hzbTarget.sampler;
		inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// Mip 0 reads the main scene depth. Subsequent mips read the previous mip.
		inputInfo.imageView = (i == 0) ? depthTarget.view : hzbMipViews[i - 1];

		VkDescriptorImageInfo outputInfo{};
		outputInfo.imageView = hzbMipViews[i];
		outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL; // Compute shaders write to GENERAL

		VkWriteDescriptorSet writes[2]{};
		writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, hzbDescriptorSets[i], 0, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &inputInfo, nullptr, nullptr };
		writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, hzbDescriptorSets[i], 1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &outputInfo, nullptr, nullptr };
		vkUpdateDescriptorSets(logicalDevice, 2, writes, 0, nullptr);
	}

	VkPushConstantRange pcRange{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec2) };
	VkPipelineLayoutCreateInfo pLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &hzbDescriptorSetLayout, 1, &pcRange };
	vkCreatePipelineLayout(logicalDevice, &pLayoutInfo, nullptr, &hzbPipelineLayout);

	auto compCode = ReadFile("shaders/hzb_reduce_comp.spv");
	VkShaderModule compModule = CreateShaderModule(logicalDevice, compCode);
	VkPipelineShaderStageCreateInfo compStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, compModule, "main" };

	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, compStage, hzbPipelineLayout, VK_NULL_HANDLE, 0 };
	vkCreateComputePipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &hzbPipeline);

	vkDestroyShaderModule(logicalDevice, compModule, nullptr);
}

void VulkanRenderer::UpdateTextureDescriptors(const Scene& scene) {
	if (descriptorSet == VK_NULL_HANDLE) return;

	std::vector<VkWriteDescriptorSet> writes;
	std::deque<VkDescriptorImageInfo> imageInfos; // Deque ensures pointers remain stable during reallocation

	// Helper lambda to bind registries securely
	auto bindRegistry = [&](const std::unordered_map<std::string, uint32_t>& map, const std::vector<Texture>& registry, uint32_t binding) {
		for (const auto& pair : map) {
			if (pair.first == "default" || pair.second >= registry.size()) continue;
			const Texture* tex = &registry[pair.second];
			if (!tex || tex->imageView == VK_NULL_HANDLE) continue;

			VkDescriptorImageInfo info{ tex->sampler, tex->imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			imageInfos.push_back(info);

			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, descriptorSet, binding, pair.second, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imageInfos.back(), nullptr, nullptr };
			writes.push_back(write);
		}
		};

	bindRegistry(g_AssetManager.GetTextureMap(), g_AssetManager.GetTextureRegistry(), 2);
	bindRegistry(g_AssetManager.GetNormalTextureMap(), g_AssetManager.GetNormalTextureRegistry(), 3);
	bindRegistry(g_AssetManager.GetOrmTextureMap(), g_AssetManager.GetOrmTextureRegistry(), 5);

	if (!writes.empty()) {
		vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}
}

void VulkanRenderer::TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageAspectFlags aspect) {
	if (image == VK_NULL_HANDLE) return;
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = aspect;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = srcAccess;
	barrier.dstAccessMask = dstAccess;
	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) throw std::runtime_error("Failed to start recording.");

	m_boidRenderer.TickCompute(commandBuffer, ImGui::GetIO().DeltaTime);
	m_grassRenderer.Cull(commandBuffer, static_cast<uint32_t>(currentFrame));

	if (currentScene != nullptr) {
		glm::vec2 dynamicHzbSize = glm::vec2((float)GetInternalWidth(), (float)GetInternalHeight());

		m_staticMeshRenderer.Cull(commandBuffer, cameraPosition, m_currentViewProj, dynamicHzbSize, static_cast<uint32_t>(currentFrame), culledCount, sceneTotalVertices, sceneTotalIndices);
	}

	if (currentScene != nullptr) {
		glm::vec2 dynamicHzbSize = glm::vec2((float)GetInternalWidth(), (float)GetInternalHeight());
		m_terrainRenderer.Cull(commandBuffer, cameraPosition, m_currentViewProj, dynamicHzbSize, (float)(hzbMipLevels - 1), static_cast<uint32_t>(currentFrame), culledCount, sceneTotalVertices, sceneTotalIndices);
		m_waterRenderer.Cull(commandBuffer, cameraPosition, m_currentViewProj, static_cast<uint32_t>(currentFrame));
	}

	if (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
		TransitionImageLayout(commandBuffer, colorTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	TransitionImageLayout(commandBuffer, offscreenTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	TransitionImageLayout(commandBuffer, depthTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_ASPECT_DEPTH_BIT);

	VkViewport viewport{ 0.0f, 0.0f, (float)GetInternalWidth(), (float)GetInternalHeight(), 0.0f, 1.0f };
	VkRect2D scissor{ {0, 0}, {GetInternalWidth(), GetInternalHeight()} };

	VkRenderingAttachmentInfo colorAttachment{};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	if (m_currentMsaaSamples != VK_SAMPLE_COUNT_1_BIT) {
		colorAttachment.imageView = colorTarget.view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttachment.resolveImageView = offscreenTarget.view;
		colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	else {
		colorAttachment.imageView = offscreenTarget.view;
		colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.clearValue.color = { 0.0f, 0.0f, 0.0f, 1.0f };

	VkRenderingAttachmentInfo depthAttachment{};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = depthTarget.view;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil = { 1.0f, 0 };

	VkRenderingInfo renderingInfo{};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.extent = { GetInternalWidth(), GetInternalHeight() };
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	// Only set secondary buffer flag if we actually have a scene to render
	if (currentScene != nullptr) {
		renderingInfo.flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT;
	}

	vkCmdBeginRendering(commandBuffer, &renderingInfo);

	if (currentScene != nullptr) {
		VkFormat colorFormat = swapChainImageFormat;
		VkFormat depthFormat = FindDepthFormat();

		VkCommandBufferInheritanceRenderingInfo inheritanceRenderingInfo{};
		inheritanceRenderingInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
		inheritanceRenderingInfo.colorAttachmentCount = 1;
		inheritanceRenderingInfo.pColorAttachmentFormats = &colorFormat;
		inheritanceRenderingInfo.depthAttachmentFormat = depthFormat;
		inheritanceRenderingInfo.rasterizationSamples = m_currentMsaaSamples;

		VkCommandBufferInheritanceInfo inheritanceInfo{};
		inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
		inheritanceInfo.pNext = &inheritanceRenderingInfo;

		VkCommandBufferBeginInfo secondaryBeginInfo{};
		secondaryBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		secondaryBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT | VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		secondaryBeginInfo.pInheritanceInfo = &inheritanceInfo;

		uint32_t dc0 = 0, cc0 = 0, tv0 = 0, ti0 = 0;
		uint32_t dc1 = 0;
		uint32_t dc2 = 0;
		uint32_t dc3 = 0;

		auto recordTask = [&](uint32_t threadIdx, auto drawFunc) {
			VkCommandBuffer scb = threadCommandBuffers[currentFrame][threadIdx];
			vkBeginCommandBuffer(scb, &secondaryBeginInfo);

			VkViewport vp{ 0.0f, 0.0f, (float)GetInternalWidth(), (float)GetInternalHeight(), 0.0f, 1.0f };
			VkRect2D sc{ {0, 0}, {GetInternalWidth(), GetInternalHeight()} };
			vkCmdSetViewport(scb, 0, 1, &vp);
			vkCmdSetScissor(scb, 0, 1, &sc);

			drawFunc(scb);

			vkEndCommandBuffer(scb);
			return scb;
			};

		// 1. Thread 0: Static Meshes & Skybox
		auto f0 = std::async(std::launch::async, [&]() {
			return recordTask(0, [&](VkCommandBuffer scb) {
				m_skybox.Draw(scb, descriptorSet);
				vkCmdBindDescriptorSets(scb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
				if (staticPipeline != VK_NULL_HANDLE && instancedPipeline != VK_NULL_HANDLE) {
					m_staticMeshRenderer.Draw(scb, pipelineLayout, descriptorSet, cameraPosition, frustumPlanes, static_cast<uint32_t>(currentFrame), staticPipeline, instancedPipeline, dc0, cc0, tv0, ti0);
				}
				});
			});

		// 2. Thread 1: Terrain
		auto f1 = std::async(std::launch::async, [&]() {
			return recordTask(1, [&](VkCommandBuffer scb) {
				if (terrainPipeline != VK_NULL_HANDLE) {
					vkCmdBindPipeline(scb, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline);
					vkCmdBindDescriptorSets(scb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
					m_terrainRenderer.Draw(scb, pipelineLayout, static_cast<uint32_t>(currentFrame), dc1);
				}
				});
			});

		// 3. Thread 2: Grass
		auto f2 = std::async(std::launch::async, [&]() {
			return recordTask(2, [&](VkCommandBuffer scb) {
				m_grassRenderer.Draw(scb, descriptorSet, static_cast<uint32_t>(currentFrame), dc2, tv0, ti0);
				});
			});

		// 4. Thread 3: Boids
		auto f3 = std::async(std::launch::async, [&]() {
			return recordTask(3, [&](VkCommandBuffer scb) {
				// Draw Boids
				vkCmdBindPipeline(scb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_boidRenderer.m_graphicsPipeline);
				m_boidRenderer.Draw(scb, descriptorSet, dc3);
				});
			});

		VkCommandBuffer scbs[] = { f1.get(), f0.get(), f2.get(), f3.get() };
		vkCmdExecuteCommands(commandBuffer, 4, scbs);

		drawCallCount += (dc0 + dc1 + dc2 + dc3);
		culledCount += cc0;
		sceneTotalVertices += tv0;
		sceneTotalIndices += ti0;
	}
	else {
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	}
	vkCmdEndRendering(commandBuffer);

	TransitionImageLayout(commandBuffer, depthTarget.image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

	TransitionImageLayout(commandBuffer, waterTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

	uint32_t waterWidth = GetInternalWidth();
	uint32_t waterHeight = GetInternalHeight();

	VkViewport waterViewport{ 0.0f, 0.0f, static_cast<float>(waterWidth), static_cast<float>(waterHeight), 0.0f, 1.0f };
	VkRect2D waterScissor{ { 0, 0 }, { waterWidth, waterHeight } };

	VkRenderingAttachmentInfo waterAttachment{};
	waterAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	waterAttachment.imageView = waterTarget.view;
	waterAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	waterAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	waterAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	waterAttachment.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };

	VkRenderingInfo waterRenderingInfo{};
	waterRenderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	waterRenderingInfo.renderArea.extent = { waterWidth, waterHeight };
	waterRenderingInfo.layerCount = 1;
	waterRenderingInfo.colorAttachmentCount = 1;
	waterRenderingInfo.pColorAttachments = &waterAttachment;

	vkCmdBeginRendering(commandBuffer, &waterRenderingInfo);
	vkCmdSetViewport(commandBuffer, 0, 1, &waterViewport);
	vkCmdSetScissor(commandBuffer, 0, 1, &waterScissor);

	m_waterRenderer.Draw(commandBuffer, descriptorSet, cameraPosition, static_cast<float>(glfwGetTime()), static_cast<uint32_t>(currentFrame));

	vkCmdEndRendering(commandBuffer);

	TransitionImageLayout(commandBuffer, waterTarget.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 
						VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
						VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
						VK_IMAGE_ASPECT_COLOR_BIT);

	glm::vec2 activeScale = glm::vec2(
		(float)GetInternalWidth() / (float)swapChainExtent.width,
		(float)GetInternalHeight() / (float)swapChainExtent.height
	);

	GenerateHZB(commandBuffer);

	if (g_Settings.enableSSAO) {
		uint32_t ssaoWidth = std::max(1u, GetInternalWidth() / 2);
		uint32_t ssaoHeight = std::max(1u, GetInternalHeight() / 2);

		VkViewport ssaoViewport{ 0.0f, 0.0f, (float)ssaoWidth, (float)ssaoHeight, 0.0f, 1.0f };
		VkRect2D ssaoScissor{ {0, 0}, {ssaoWidth, ssaoHeight} };

		TransitionImageLayout(commandBuffer, ssaoTarget.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		VkRenderingAttachmentInfo ssaoAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, ssaoTarget.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {1.0f, 1.0f, 1.0f, 1.0f} };
		VkRenderingInfo ssaoRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {{0, 0}, {ssaoWidth, ssaoHeight}}, 1, 0, 1, &ssaoAttachment, nullptr, nullptr };

		vkCmdBeginRendering(commandBuffer, &ssaoRenderInfo);
		vkCmdSetViewport(commandBuffer, 0, 1, &ssaoViewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &ssaoScissor);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoPipelineLayout, 0, 1, &ssaoDescriptorSet, 0, nullptr);

		SSAOPushConstants ssaoPC{};
		ssaoPC.screenSize = glm::vec2(ssaoWidth, ssaoHeight);
		ssaoPC.radius = 1.6f;
		ssaoPC.bias = 0.2f;
		ssaoPC.renderScale = activeScale;
		vkCmdPushConstants(commandBuffer, ssaoPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOPushConstants), &ssaoPC);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		vkCmdEndRendering(commandBuffer);

		TransitionImageLayout(commandBuffer, ssaoTarget.image,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		TransitionImageLayout(commandBuffer, ssaoPingPongTarget.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		VkRenderingAttachmentInfo blurHAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, ssaoPingPongTarget.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {1.0f, 1.0f, 1.0f, 1.0f} };
		VkRenderingInfo blurHRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {{0, 0}, {ssaoWidth, ssaoHeight}}, 1, 0, 1, &blurHAttachment, nullptr, nullptr };

		vkCmdBeginRendering(commandBuffer, &blurHRenderInfo);
		vkCmdSetViewport(commandBuffer, 0, 1, &ssaoViewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &ssaoScissor);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoBlurPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoBlurPipelineLayout, 0, 1, &ssaoBlurDescriptorSetHorizontal, 0, nullptr);

		SSAOBlurPushConstants blurHPC{ glm::vec2(ssaoWidth, ssaoHeight), glm::vec2(1.0f, 0.0f), 0.10f, 2.0f, activeScale };
		vkCmdPushConstants(commandBuffer, ssaoBlurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOBlurPushConstants), &blurHPC);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		vkCmdEndRendering(commandBuffer);

		TransitionImageLayout(commandBuffer, ssaoPingPongTarget.image,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		TransitionImageLayout(commandBuffer, ssaoBlurTarget.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		VkRenderingAttachmentInfo blurVAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, nullptr, ssaoBlurTarget.view, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_RESOLVE_MODE_NONE, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, {1.0f, 1.0f, 1.0f, 1.0f} };
		VkRenderingInfo blurVRenderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO, nullptr, 0, {{0, 0}, {ssaoWidth, ssaoHeight}}, 1, 0, 1, &blurVAttachment, nullptr, nullptr };

		vkCmdBeginRendering(commandBuffer, &blurVRenderInfo);
		vkCmdSetViewport(commandBuffer, 0, 1, &ssaoViewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &ssaoScissor);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoBlurPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ssaoBlurPipelineLayout, 0, 1, &ssaoBlurDescriptorSetVertical, 0, nullptr);

		SSAOBlurPushConstants blurVPC{ glm::vec2(ssaoWidth, ssaoHeight), glm::vec2(0.0f, 1.0f), 0.1f, 2.0f, activeScale };
		vkCmdPushConstants(commandBuffer, ssaoBlurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SSAOBlurPushConstants), &blurVPC);
		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
		vkCmdEndRendering(commandBuffer);

		TransitionImageLayout(commandBuffer, ssaoBlurTarget.image,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		TransitionImageLayout(commandBuffer, depthTarget.image,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
			VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_ASPECT_DEPTH_BIT);
	}
	else {
		TransitionImageLayout(commandBuffer, ssaoTarget.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

		TransitionImageLayout(commandBuffer, ssaoBlurTarget.image,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
	}

	TransitionImageLayout(commandBuffer, offscreenTarget.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);

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

	if (compositionPipeline != VK_NULL_HANDLE) {
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, compositionPipelineLayout, 0, 1, &compositionDescriptorSet, 0, nullptr);

		FSRConstants fc{};
		fc.sharpness = g_Settings.enableFSR ? 0.35f : 0.0f;
		fc.enableSSAO = g_Settings.enableSSAO ? 1 : 0;
		fc.renderScale = activeScale;
		vkCmdPushConstants(commandBuffer, compositionPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(FSRConstants), &fc);

		vkCmdDraw(commandBuffer, 3, 1, 0, 0);
	}

	if (ImGui::GetDrawData() != nullptr) {
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
		drawCallCount++;
	}

	vkCmdEndRenderPass(commandBuffer);

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) throw std::runtime_error("Failed to record layout command instructions.");
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
	m_staticMeshRenderer.SetChunkSize(size);
}
void VulkanRenderer::ClearHeightCache() {
	m_terrainRenderer.ClearHeightCache();
}

void VulkanRenderer::AddGrass(int64_t key, const std::vector<GrassInstance>& grassInstances) {
	m_grassRenderer.AddGrass(key, grassInstances);
}
void VulkanRenderer::RemoveGrass(int64_t key) {
	m_grassRenderer.RemoveChunk(key);
}

void VulkanRenderer::AddBoid(int64_t chunkKey, const std::vector<BoidInstance>& initialBoids, uint32_t textureId) {
	m_boidRenderer.AddSwarm(chunkKey, initialBoids, textureId);
}
void VulkanRenderer::RemoveBoid(int64_t chunkKey) {
	m_boidRenderer.RemoveSwarm(chunkKey);
}

void VulkanRenderer::AddWaterChunk(int64_t key, const std::vector<ModelVertex>& vertices, const std::vector<uint32_t>& indices) {
	m_waterRenderer.AddWaterChunk(key, vertices, indices);
}
void VulkanRenderer::RemoveWaterChunk(int64_t key) {
	m_waterRenderer.RemoveWaterChunk(key);
}
void VulkanRenderer::CreateWaterTarget() {
	uint32_t width = GetInternalWidth();
	uint32_t height = GetInternalHeight();

	CreateImage(width, height, 1, VK_SAMPLE_COUNT_1_BIT, swapChainImageFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, waterTarget.image, waterTarget.memory);
	waterTarget.view = CreateImageView(waterTarget.image, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

	if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &waterTarget.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create water target sampler.");
	}
}

void VulkanRenderer::ApplySettings() {
	bool needSwapchainRecreate = false;

	if (g_Settings.enableFSR != currentSettings.enableFSR) {
		currentSettings.enableFSR = g_Settings.enableFSR;
		needSwapchainRecreate = true;
	}

	if (g_Settings.vsync != currentSettings.vsync) {
		currentSettings.vsync = g_Settings.vsync;
		needSwapchainRecreate = true;
	}

	if (g_Settings.fullscreen != currentSettings.fullscreen) {
		currentSettings.fullscreen = g_Settings.fullscreen;
		needSwapchainRecreate = true;
	}

	if (g_Settings.antiAliasingMode != currentSettings.antiAliasingMode) {
		currentSettings.antiAliasingMode = g_Settings.antiAliasingMode;
		needSwapchainRecreate = true;
	}

	VkSampleCountFlagBits newSamples = VK_SAMPLE_COUNT_1_BIT;
	if (g_Settings.antiAliasingMode == 1) {
		newSamples = IntToSampleCount(g_Settings.msaaSamples);
		VkSampleCountFlagBits maxSupported = GetMaxUsableSampleCount();
		if (static_cast<int>(newSamples) > static_cast<int>(maxSupported)) {
			newSamples = maxSupported;
		}
	}

	if (newSamples != m_currentMsaaSamples) {
		m_currentMsaaSamples = newSamples;
		currentSettings.msaaSamples = g_Settings.msaaSamples;
		needSwapchainRecreate = true;
	}

	if (!g_Settings.fullscreen) {
		int currentWidth, currentHeight;
		glfwGetWindowSize(window, &currentWidth, &currentHeight);
		if (currentWidth != g_Settings.windowWidth || currentHeight != g_Settings.windowHeight) {
			glfwSetWindowSize(window, g_Settings.windowWidth, g_Settings.windowHeight);
			needSwapchainRecreate = true;
		}
	}

	if (std::abs(g_Settings.renderScale - currentSettings.renderScale) > 0.01f) {
		currentSettings.renderScale = g_Settings.renderScale;
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
void VulkanRenderer::UpdateDRS() {
	if (!g_Settings.enableDRS) return;

	static int cooldownFrames = 0;
	static int stableTargetFrames = 0;

	// Lockout period to allow the framerate to stabilize after a resolution change
	if (cooldownFrames > 0) {
		cooldownFrames--;
		return;
	}

	float currentFPS = ImGui::GetIO().Framerate;
	float target = static_cast<float>(g_Settings.targetFPS);

	// 1. DANGER ZONE: We are dropping frames. Scale down aggressively.
	if (currentFPS < target - 3.0f) {
		if (g_Settings.renderScale > 0.5f) {
			g_Settings.renderScale = std::max(0.5f, g_Settings.renderScale - 0.05f); // Drop by 5%
			cooldownFrames = 30; // Short cooldown when dropping to recover quickly
		}
		stableTargetFrames = 0; // Reset the up-scale tracker
	}
	// 2. STABILITY ZONE: We are comfortably hitting our target frame rate.
	else if (currentFPS >= target - 1.0f) {
		if (g_Settings.renderScale < 1.0f) {
			stableTargetFrames++;

			// If we've held the target FPS solidly for ~120 frames (approx 2 seconds), tentatively increase quality
			if (stableTargetFrames > 120) {
				g_Settings.renderScale = std::min(1.0f, g_Settings.renderScale + 0.05f); // Increase by 5%
				cooldownFrames = 60; // Longer cooldown after an up-scale so it doesn't instantly panic and drop back down
				stableTargetFrames = 0;
			}
		}
	}
	// 3. MIDDLE GROUND: Just slightly below target (e.g., 58 FPS on a 60 Target). 
	// Do not panic, but do not scale up either.
	else {
		stableTargetFrames = 0;
	}
}

void VulkanRenderer::DrawFrame() {
	UpdateDRS();

	ApplySettings();

	drawCallCount = 0;
	sceneTotalVertices = 0;
	sceneTotalIndices = 0;
	culledCount = 0;

	m_globalFrameCounter++;

	m_uploader.Tick(m_globalFrameCounter);

	vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

	m_pendingDeletionsGlobal.Flush(m_globalFrameCounter, [&](BufferDeletion& del) {
		for (size_t i = 0; i < del.buffers.size(); ++i) {
			DestroyBuffer(del.buffers[i], del.memories[i]);
		}
		});

	m_terrainRenderer.Tick(m_globalFrameCounter);
	m_grassRenderer.Tick(m_globalFrameCounter);
	m_waterRenderer.Tick(m_globalFrameCounter);

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

	if (imagesInFlight[imageIndex] != VK_NULL_HANDLE && imagesInFlight[imageIndex] != inFlightFences[currentFrame]) {
		vkWaitForFences( logicalDevice, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
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
	currentFrame = (currentFrame + 1) % m_framesInFlight;
}
void VulkanRenderer::BeginUI() {
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}
void VulkanRenderer::EndUI() {
	// We keep your awesome stats overlay here so it draws over every scene!
	uint32_t triangleCount = sceneTotalIndices / 3;
	uint32_t texturesLoaded = static_cast<uint32_t>(g_AssetManager.GetTextureRegistry().size());

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoInputs;

	ImGui::SetNextWindowPos(ImVec2(1280 - 240, 10), ImGuiCond_Always);
	ImGui::SetNextWindowBgAlpha(0.35f);

	ImGui::Begin("Stats", nullptr, windowFlags);

	ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "PERFORMANCE");
	ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
	ImGui::Text("Ms/Frame: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);

	ImGui::Separator();
	ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "PACING & SCALING");

	if (g_Settings.frameCap == 0) {
		ImGui::Text("Frame Cap: Uncapped");
	}
	else {
		ImGui::Text("Frame Cap: %d FPS", g_Settings.frameCap);
	}

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

	// Finalize the ImGui frame
	ImGui::Render();
}
void VulkanRenderer::UpdateScene(const Scene& scene) {
	// Scene caches the ECS traversal, avoids rebuilding the static draw lists and instance buckets when nothing changed
	if (scene.NeedsRendererUpdate()) {
		m_staticMeshRenderer.UpdateScene(scene);
	}

	if (g_AssetManager.IsTextureDirty()) {
		UpdateTextureDescriptors(scene);
		g_AssetManager.ClearTextureDirty();
	}

	if (scene.HasModifiedLights()) {
		SetLights(scene.GetLights());
		scene.ClearModifiedLightsFlag();
	}

	currentScene = &scene;
}

void VulkanRenderer::Cleanup() {
	m_isShuttingDown = true;

	if (logicalDevice != VK_NULL_HANDLE) {
		vkDeviceWaitIdle(logicalDevice);
	}

	m_uploader.Shutdown();
	m_terrainRenderer.Cleanup();
	m_waterRenderer.Cleanup();
	m_grassRenderer.Cleanup();
	m_staticMeshRenderer.Cleanup();
	m_skybox.Cleanup(logicalDevice);
	m_boidRenderer.Cleanup();

	m_pendingDeletionsGlobal.Flush(UINT64_MAX, [&](BufferDeletion& del) {
		for (size_t i = 0; i < del.buffers.size(); ++i) {
			DestroyBuffer(del.buffers[i], del.memories[i]);
		}
		});

	if (imguiDescriptorPool != VK_NULL_HANDLE) {
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		vkDestroyDescriptorPool(logicalDevice, imguiDescriptorPool, nullptr);
		imguiDescriptorPool = VK_NULL_HANDLE;
	}

	if (assetManager != nullptr) {
		assetManager->Cleanup(logicalDevice);
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		// Clean up HZB Pipeline
		if (hzbPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, hzbPipeline, nullptr);
		if (hzbPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, hzbPipelineLayout, nullptr);
		if (hzbDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(logicalDevice, hzbDescriptorSetLayout, nullptr);
		if (hzbDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(logicalDevice, hzbDescriptorPool, nullptr);

		// Clean up HZB Images
		if (hzbTarget.image != VK_NULL_HANDLE) {
			for (auto view : hzbMipViews) vkDestroyImageView(logicalDevice, view, nullptr);
			hzbMipViews.clear();
			hzbTarget.Destroy(logicalDevice);
		}
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		colorTarget.Destroy(logicalDevice);
		depthTarget.Destroy(logicalDevice);
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		if (ssaoUBOMapped != nullptr) { vkUnmapMemory(logicalDevice, ssaoUBOMemory); ssaoUBOMapped = nullptr; }
		DestroyBuffer(ssaoUBO, ssaoUBOMemory);

		if (ssaoPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(logicalDevice, ssaoPipeline, nullptr); ssaoPipeline = VK_NULL_HANDLE; }
		if (ssaoPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(logicalDevice, ssaoPipelineLayout, nullptr); ssaoPipelineLayout = VK_NULL_HANDLE; }
		if (ssaoDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(logicalDevice, ssaoDescriptorSetLayout, nullptr); ssaoDescriptorSetLayout = VK_NULL_HANDLE; }
		if (ssaoDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(logicalDevice, ssaoDescriptorPool, nullptr); ssaoDescriptorPool = VK_NULL_HANDLE; }
		ssaoTarget.Destroy(logicalDevice);
		ssaoNoiseTarget.Destroy(logicalDevice);

		ssaoPingPongTarget.Destroy(logicalDevice);
		ssaoBlurTarget.Destroy(logicalDevice);

		if (ssaoBlurPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(logicalDevice, ssaoBlurPipeline, nullptr); ssaoBlurPipeline = VK_NULL_HANDLE; }
		if (ssaoBlurPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(logicalDevice, ssaoBlurPipelineLayout, nullptr); ssaoBlurPipelineLayout = VK_NULL_HANDLE; }
		if (ssaoBlurDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(logicalDevice, ssaoBlurDescriptorSetLayout, nullptr); ssaoBlurDescriptorSetLayout = VK_NULL_HANDLE; }
		if (ssaoBlurDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(logicalDevice, ssaoBlurDescriptorPool, nullptr); ssaoBlurDescriptorPool = VK_NULL_HANDLE; }
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		if (compositionPipeline != VK_NULL_HANDLE) vkDestroyPipeline(logicalDevice, compositionPipeline, nullptr);
		if (compositionPipelineLayout != VK_NULL_HANDLE) vkDestroyPipelineLayout(logicalDevice, compositionPipelineLayout, nullptr);
		if (compositionDescriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(logicalDevice, compositionDescriptorPool, nullptr);
		if (compositionDescriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(logicalDevice, compositionDescriptorSetLayout, nullptr);

		offscreenTarget.Destroy(logicalDevice);
		waterTarget.Destroy(logicalDevice);

		compositionPipeline = VK_NULL_HANDLE;
	}

	if (compositionRenderPass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(logicalDevice, compositionRenderPass, nullptr);
		compositionRenderPass = VK_NULL_HANDLE;
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		for (auto framebuffer : swapChainFramebuffers) {
			if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
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
	}

	if (compositionRenderPass != VK_NULL_HANDLE) {
		vkDestroyRenderPass(logicalDevice, compositionRenderPass, nullptr);
		compositionRenderPass = VK_NULL_HANDLE;
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		for (auto framebuffer : swapChainFramebuffers) {
			if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
		}
		swapChainFramebuffers.clear();

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
	}

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
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		if (descriptorPool != VK_NULL_HANDLE) vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
		if (descriptorSetLayout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		// Uniform Buffer
		if (uniformBufferMapped != nullptr) vkUnmapMemory(logicalDevice, uniformBufferMemory);
		DestroyBuffer(uniformBuffer, uniformBufferMemory);

		// Light Buffer
		if (lightBufferMapped != nullptr) vkUnmapMemory(logicalDevice, lightBufferMemory);
		DestroyBuffer(lightBuffer, lightBufferMemory);
	}

	if (logicalDevice != VK_NULL_HANDLE) {
		for (size_t i = 0; i < m_framesInFlight; i++) {
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

	if (logicalDevice != VK_NULL_HANDLE) {
		for (uint32_t i = 0; i < m_framesInFlight; i++) {
			for (uint32_t j = 0; j < NUM_RENDER_THREADS; j++) {
				if (threadCommandPools.size() > i && threadCommandPools[i].size() > j && threadCommandPools[i][j] != VK_NULL_HANDLE) {
					vkDestroyCommandPool(logicalDevice, threadCommandPools[i][j], nullptr);
				}
			}
		}
		if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
	}

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

	if (window != nullptr) {
		glfwDestroyWindow(window);
		window = nullptr;
	}
	
	glfwTerminate();
}