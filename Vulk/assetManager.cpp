#define _CRT_SECURE_NO_WARNINGS

#include "asset_manager.h"
#include "renderer.h"

#include <iostream>
#include <algorithm>
#include <cfloat>
#include <unordered_set>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

AssetManager g_AssetManager;

void AssetManager::CreateDefaultTexture() {
    uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, whitePixel, static_cast<size_t>(imageSize));
    vkUnmapMemory(GetDevice(), stagingBufferMemory);

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

    if (vkCreateImage(GetDevice(), &imageInfo, nullptr, &m_defaultTexture.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create default texture image.");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(GetDevice(), m_defaultTexture.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(GetDevice(), &allocInfo, nullptr, &m_defaultTexture.imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate default texture memory.");
    }
    vkBindImageMemory(GetDevice(), m_defaultTexture.image, m_defaultTexture.imageMemory, 0);

    TransitionImageLayout(m_defaultTexture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    
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
    region.imageExtent = { 1, 1, 1 };
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_defaultTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(commandBuffer);

    TransitionImageLayout(m_defaultTexture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_defaultTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_defaultTexture.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(GetDevice(), &viewInfo, nullptr, &m_defaultTexture.imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture image view.");

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(GetPhysicalDevice(), &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = g_Settings.maxAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_defaultTexture.mipLevels);
    samplerInfo.mipLodBias = 0.0f;
    if (vkCreateSampler(GetDevice(), &samplerInfo, nullptr, &m_defaultTexture.sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture sampler.");

    VkDescriptorImageInfo descriptorInfo{};
    descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorInfo.imageView = m_defaultTexture.imageView;
    descriptorInfo.sampler = m_defaultTexture.sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;   // <-- must be set before calling this!
    descriptorWrite.dstBinding = 2;             // binding 1 is the texture array
    descriptorWrite.dstArrayElement = 0;        // index 0 = default texture
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descriptorInfo;

    vkUpdateDescriptorSets(GetDevice(), 1, &descriptorWrite, 0, nullptr);
}
void AssetManager::CreateDefaultNormalTexture() {
    uint8_t flatNormalPixel[4] = { 128, 128, 255, 255 }; // tangent-space "no perturbation"
    VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, flatNormalPixel, static_cast<size_t>(imageSize));
    vkUnmapMemory(GetDevice(), stagingBufferMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 1, 1, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // NOT sRGB — normal data is linear, not color
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(GetDevice(), &imageInfo, nullptr, &m_defaultNormalTexture.image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create default normal texture image.");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(GetDevice(), m_defaultNormalTexture.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(GetDevice(), &allocInfo, nullptr, &m_defaultNormalTexture.imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate default normal texture memory.");
    }
    vkBindImageMemory(GetDevice(), m_defaultNormalTexture.image, m_defaultNormalTexture.imageMemory, 0);

    TransitionImageLayout(m_defaultNormalTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
    region.imageExtent = { 1, 1, 1 };
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_defaultNormalTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(commandBuffer);

    TransitionImageLayout(m_defaultNormalTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_defaultNormalTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = m_defaultNormalTexture.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(GetDevice(), &viewInfo, nullptr, &m_defaultNormalTexture.imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture image view.");
    
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(GetPhysicalDevice(), &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = g_Settings.maxAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(m_defaultNormalTexture.mipLevels);
    samplerInfo.mipLodBias = 0.0f;
    if (vkCreateSampler(GetDevice(), &samplerInfo, nullptr, &m_defaultNormalTexture.sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture sampler.");

    VkDescriptorImageInfo descriptorInfo{};
    descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descriptorInfo.imageView = m_defaultNormalTexture.imageView;
    descriptorInfo.sampler = m_defaultNormalTexture.sampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = m_descriptorSet;
    descriptorWrite.dstBinding = 3;        // normal map array
    descriptorWrite.dstArrayElement = 0;   // index 0 = default flat normal
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descriptorInfo;

    vkUpdateDescriptorSets(GetDevice(), 1, &descriptorWrite, 0, nullptr);
}
void AssetManager::CreateDefaultOrmTexture() {
    // Default ORM: Red=255 (No occlusion), Green=204 (Roughness 0.8), Blue=0 (Non-metal)
    uint8_t defaultOrmPixel[4] = { 255, 204, 0, 255 };
    VkDeviceSize imageSize = 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, defaultOrmPixel, static_cast<size_t>(imageSize));
    vkUnmapMemory(GetDevice(), stagingBufferMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { 1, 1, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // ORM data is strictly linear
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateImage(GetDevice(), &imageInfo, nullptr, &m_defaultOrmTexture.image);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(GetDevice(), m_defaultOrmTexture.image, &memReq);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, memReq.size, FindMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    vkAllocateMemory(GetDevice(), &allocInfo, nullptr, &m_defaultOrmTexture.imageMemory);
    vkBindImageMemory(GetDevice(), m_defaultOrmTexture.image, m_defaultOrmTexture.imageMemory, 0);

    TransitionImageLayout(m_defaultOrmTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { 1, 1, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, m_defaultOrmTexture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);

    TransitionImageLayout(m_defaultOrmTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, m_defaultOrmTexture.image, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1} };
    vkCreateImageView(GetDevice(), &viewInfo, nullptr, &m_defaultOrmTexture.imageView);

    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr, 0, VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_REPEAT, 0.0f, VK_FALSE, 1.0f, VK_FALSE, VK_COMPARE_OP_ALWAYS, 0.0f, 1.0f, VK_BORDER_COLOR_INT_OPAQUE_BLACK, VK_FALSE };
    vkCreateSampler(GetDevice(), &samplerInfo, nullptr, &m_defaultOrmTexture.sampler);

    VkDescriptorImageInfo descInfo{ m_defaultOrmTexture.sampler, m_defaultOrmTexture.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, m_descriptorSet, 5, 0, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &descInfo, nullptr, nullptr }; // BOUND TO 5
    vkUpdateDescriptorSets(GetDevice(), 1, &write, 0, nullptr);
}

uint32_t AssetManager::LoadTextureFromMemory(const std::string& virtualName, const uint8_t* buffer, size_t bufferSize, int texType) {
    auto& map = (texType == 0) ? m_textureToId : ((texType == 1) ? m_normalTextureToId : m_ormTextureToId);
    auto& registry = (texType == 0) ? m_textureRegistry : ((texType == 1) ? m_normalTextureRegistry : m_ormTextureRegistry);

    if (map.find(virtualName) != map.end()) {
        return map[virtualName];
    }

    // 1. Decode the embedded PNG/JPG using STB
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load_from_memory(buffer, static_cast<int>(bufferSize), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

    if (!pixels) {
        std::cerr << "[AssetManager] Failed to decode embedded texture: " << virtualName << "\n";
        return 0; // Fallback to default
    }

    VkDeviceSize imageSize = texWidth * texHeight * 4; // 4 bytes per pixel (RGBA)

    // 2. Upload to Staging Buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(GetDevice(), stagingBufferMemory);

    stbi_image_free(pixels); // Free CPU RAM

    // 3. Create Vulkan Image
    Texture tex{};
    tex.mipLevels = 1; // Simplified: 1 mip level for runtime decodes
    VkFormat format = (texType == 0) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = tex.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(GetDevice(), &imageInfo, nullptr, &tex.image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create embedded texture image.");

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(GetDevice(), tex.image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(GetDevice(), &allocInfo, nullptr, &tex.imageMemory);
    vkBindImageMemory(GetDevice(), tex.image, tex.imageMemory, 0);

    // 4. Transfer and Layout Transitions
    TransitionImageLayout(tex.image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, tex.mipLevels);

    VkCommandBuffer cmd = BeginSingleTimeCommands();
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { (uint32_t)texWidth, (uint32_t)texHeight, 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);

    TransitionImageLayout(tex.image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, tex.mipLevels);

    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    // 5. Create View & Sampler
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = tex.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = tex.mipLevels;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(GetDevice(), &viewInfo, nullptr, &tex.imageView);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = g_Settings.maxAnisotropy;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = static_cast<float>(tex.mipLevels);
    vkCreateSampler(GetDevice(), &samplerInfo, nullptr, &tex.sampler);

    // 6. Register in engine
    uint32_t newId = static_cast<uint32_t>(registry.size());
    registry.push_back(std::move(tex));
    map[virtualName] = newId;
    m_textureDirty = true;

    std::cout << "[AssetManager] Decoded Embedded GLB Texture: " << virtualName << " -> ID " << newId << "\n";
    return newId;
}
uint32_t AssetManager::LoadTextureFromFile(const std::string& filePath) {
    auto it = m_textureToId.find(filePath);
    if (it != m_textureToId.end()) {
        return it->second;
    }

    // Pre-check file existence to catch the missing asset path cleanly
    std::ifstream checkFile(filePath, std::ios::binary);
    if (!checkFile.is_open()) {
        std::cerr << "\n------------------------------------------------------------------------\n"
            << "[Asset Pipeline Error] FAILED TO OPEN COMPRESSED TEXTURE FILE:\n"
            << " -> Target Path: " << filePath << "\n"
            << "[Asset Pipeline Warning] Automatically falling back to default white texture (ID 0).\n"
            << "------------------------------------------------------------------------\n\n";

        // Map this broken path directly to your clean fallback texture ID 0
        m_textureToId[filePath] = 0;
        return 0;
    }
    checkFile.close();

    Texture tex{};
    CreateTextureImage(filePath, tex, VK_FORMAT_BC7_SRGB_BLOCK);

    uint32_t newId = static_cast<uint32_t>(m_textureRegistry.size());
    m_textureRegistry.push_back(std::move(tex));
    m_textureToId[filePath] = newId;

    m_textureDirty = true;
    std::cout << "[AssetManager] Loaded albedo: " << filePath << " -> ID " << newId << "\n";

    return newId;
}
void AssetManager::LoadTexture(const std::string& path) {
    if (path.empty() || path == "default") return;
    if (m_textureToId.find(path) != m_textureToId.end()) return;
    // Load and add to registry automatically via LoadTextureFromFile
    LoadTextureFromFile(path);
}

Texture* AssetManager::GetTexture(const std::string& path) {
    if (path.empty() || path == "default") {
        return &m_defaultTexture;
    }

    auto it = m_textureToId.find(path);
    if (it != m_textureToId.end()) {
        // CRITICAL CRASH FIX: Explicitly verify the ID falls within the allocated vector bounds
        if (it->second < m_textureRegistry.size()) {
            return &m_textureRegistry[it->second];
        }
    }

    // Graceful fallback to standalone default texture address if lookups fail or map to 0
    return &m_defaultTexture;
}
uint32_t AssetManager::GetTextureId(const std::string& path) {
    // Return 0 for empty path or "default"
    if (path.empty() || path == "default") return 0;
    auto it = m_textureToId.find(path);
    if (it != m_textureToId.end()) return it->second;
    LoadTexture(path);
    return m_textureToId[path];
}

uint32_t AssetManager::LoadNormalTextureFromFile(const std::string& filePath) {
    auto it = m_normalTextureToId.find(filePath);
    if (it != m_normalTextureToId.end()) {
        return it->second;
    }

    // Pre-check file existence to catch the missing normal vector path cleanly
    std::ifstream checkFile(filePath, std::ios::binary);
    if (!checkFile.is_open()) {
        std::cerr << "\n------------------------------------------------------------------------\n"
            << "[Asset Pipeline Error] FAILED TO OPEN COMPRESSED NORMAL FILE:\n"
            << " -> Target Path: " << filePath << "\n"
            << "[Asset Pipeline Warning] Automatically falling back to flat default normal (ID 0).\n"
            << "------------------------------------------------------------------------\n\n";

        // Map this broken normal path directly to your flat baseline texture ID 0
        m_normalTextureToId[filePath] = 0;
        return 0;
    }
    checkFile.close();

    Texture tex{};
    CreateTextureImage(filePath, tex, VK_FORMAT_BC7_UNORM_BLOCK);

    uint32_t newId = static_cast<uint32_t>(m_normalTextureRegistry.size());
    m_normalTextureRegistry.push_back(std::move(tex));
    m_normalTextureToId[filePath] = newId;

    m_textureDirty = true;
    std::cout << "[AssetManager] Loaded normal: " << filePath << " -> ID " << newId << "\n";

    return newId;
}
void AssetManager::LoadNormalTexture(const std::string& path) {
    if (path.empty()) return;
    if (m_normalTextureToId.find(path) != m_normalTextureToId.end()) return;
    LoadNormalTextureFromFile(path);
}
Texture* AssetManager::GetNormalTexture(const std::string& path) {
    if (path.empty() || path == "default") {
        return &m_defaultNormalTexture;
    }

    auto it = m_normalTextureToId.find(path);
    if (it != m_normalTextureToId.end()) {
        // CRITICAL CRASH FIX: Explicitly verify the ID falls within the allocated vector bounds
        if (it->second < m_normalTextureRegistry.size()) {
            return &m_normalTextureRegistry[it->second];
        }
    }

    // Graceful fallback to flat standalone normal texture address if lookups fail or map to 0
    return &m_defaultNormalTexture;
}
uint32_t AssetManager::GetNormalTextureId(const std::string& path) {
    if (path.empty()) return 0; // slot 0 reserved for default flat normal
    auto it = m_normalTextureToId.find(path);
    if (it != m_normalTextureToId.end()) return it->second;
    LoadNormalTexture(path);
    return m_normalTextureToId[path];
}

uint32_t AssetManager::LoadOrmTextureFromFile(const std::string& filePath) {
    auto it = m_ormTextureToId.find(filePath);
    if (it != m_ormTextureToId.end()) return it->second;

    std::ifstream checkFile(filePath, std::ios::binary);
    if (!checkFile.is_open()) {
        m_ormTextureToId[filePath] = 0;
        return 0;
    }
    checkFile.close();

    Texture tex{};
    CreateTextureImage(filePath, tex, VK_FORMAT_BC7_UNORM_BLOCK);

    uint32_t newId = static_cast<uint32_t>(m_ormTextureRegistry.size());
    m_ormTextureRegistry.push_back(std::move(tex));
    m_ormTextureToId[filePath] = newId;

    m_textureDirty = true;
    return newId;
}
void AssetManager::LoadOrmTexture(const std::string& path) {
    if (path.empty()) return;
    if (m_ormTextureToId.find(path) != m_ormTextureToId.end()) return;
    LoadOrmTextureFromFile(path);
}
Texture* AssetManager::GetOrmTexture(const std::string& path) {
    if (path.empty() || path == "default") return &m_defaultOrmTexture;
    auto it = m_ormTextureToId.find(path);
    if (it != m_ormTextureToId.end() && it->second < m_ormTextureRegistry.size()) return &m_ormTextureRegistry[it->second];
    return &m_defaultOrmTexture;
}
uint32_t AssetManager::GetOrmTextureId(const std::string& path) {
    if (path.empty()) return 0;
    auto it = m_ormTextureToId.find(path);
    if (it != m_ormTextureToId.end()) return it->second;
    LoadOrmTexture(path);
    return m_ormTextureToId[path];
}

void AssetManager::CreateTextureImage(const std::string& path, Texture& texture, VkFormat format) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open compressed texture file: " + path);
    }

    uint32_t magic = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != 0x20534444) { // "DDS " in ASCII
        throw std::runtime_error("File is not a valid DDS file: " + path);
    }

    DDSHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    uint32_t texWidth = header.dwWidth;
    uint32_t texHeight = header.dwHeight;
    uint32_t mipLevels = (header.dwMipMapCount == 0) ? 1 : header.dwMipMapCount;

    // Handle DX10 header extension if present
    if ((header.ddspf.dwFlags & 0x4) && (header.ddspf.dwFourCC == 0x30315844)) { 
        // Skip DX10 extension header (20 bytes)
        file.seekg(20, std::ios::cur);
    }

    // Read the remainder of the compressed pixel data blocks
    std::vector<char> pixelData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    VkDeviceSize imageSize = pixelData.size();

    uint32_t safeMipLevels = 0;
    VkDeviceSize accumulatedSize = 0;
    uint32_t currentWidth = texWidth;
    uint32_t currentHeight = texHeight;

    for (uint32_t i = 0; i < mipLevels; i++) {
        uint32_t blockWidth = (currentWidth + 3) / 4;
        uint32_t blockHeight = (currentHeight + 3) / 4;
        VkDeviceSize mipSize = blockWidth * blockHeight * 16; // 16 bytes per block for BC7

        if (accumulatedSize + mipSize > imageSize) {
            break; // Stop parsing if the file doesn't actually contain this mip level
        }

        accumulatedSize += mipSize;
        safeMipLevels++;

        if (currentWidth > 1) currentWidth /= 2;
        if (currentHeight > 1) currentHeight /= 2;
    }

    if (safeMipLevels == 0) safeMipLevels = 1;
    texture.mipLevels = safeMipLevels;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixelData.data(), static_cast<size_t>(imageSize));
    vkUnmapMemory(GetDevice(), stagingBufferMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = texWidth;
    imageInfo.extent.height = texHeight;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = texture.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(GetDevice(), &imageInfo, nullptr, &texture.image) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture image.");

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(GetDevice(), texture.image, &memRequirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(GetDevice(), &allocInfo, nullptr, &texture.imageMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate texture memory.");
    vkBindImageMemory(GetDevice(), texture.image, texture.imageMemory, 0);

    // Transition image layout to receive all mip levels
    TransitionImageLayout(texture.image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels);

    // Record copy regions for every precomputed mipmap inside the DDS file
    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    std::vector<VkBufferImageCopy> bufferCopyRegions;
    VkDeviceSize bufferOffset = 0;

    currentWidth = texWidth;
    currentHeight = texHeight;

    for (uint32_t i = 0; i < texture.mipLevels; i++) {
        uint32_t blockWidth = (currentWidth + 3) / 4;
        uint32_t blockHeight = (currentHeight + 3) / 4;
        VkDeviceSize mipSize = blockWidth * blockHeight * 16;

        VkBufferImageCopy region{};
        region.bufferOffset = bufferOffset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = i;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { currentWidth, currentHeight, 1 };

        bufferCopyRegions.push_back(region);
        bufferOffset += mipSize;

        if (currentWidth > 1) currentWidth /= 2;
        if (currentHeight > 1) currentHeight /= 2;
    }

    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(bufferCopyRegions.size()),
        bufferCopyRegions.data()
    );
    EndSingleTimeCommands(commandBuffer);

    // Transition image layout to shader-readable format
    TransitionImageLayout(texture.image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, texture.mipLevels);

    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = texture.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(GetDevice(), &viewInfo, nullptr, &texture.imageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture image view.");

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(GetPhysicalDevice(), &properties);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = g_Settings.maxAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(texture.mipLevels);
    samplerInfo.mipLodBias = 0.0f;
    if (vkCreateSampler(GetDevice(), &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS)
        throw std::runtime_error("Failed to create texture sampler.");
}

void AssetManager::TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) {
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
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

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

static void ComputeTangents(std::vector<ModelVertex>& verts, const std::vector<uint32_t>& idxs) {
    std::vector<glm::vec3> tanAccum(verts.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitanAccum(verts.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < idxs.size(); i += 3) {
        uint32_t i0 = idxs[i], i1 = idxs[i + 1], i2 = idxs[i + 2];
        ModelVertex& v0 = verts[i0];
        ModelVertex& v1 = verts[i1];
        ModelVertex& v2 = verts[i2];

        glm::vec3 edge1 = v1.pos - v0.pos;
        glm::vec3 edge2 = v2.pos - v0.pos;
        glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
        glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

        float denom = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
        if (std::abs(denom) < 1e-8f) continue; // degenerate UVs, skip this triangle
        float f = 1.0f / denom;

        glm::vec3 tangent = f * (deltaUV2.y * edge1 - deltaUV1.y * edge2);
        glm::vec3 bitangent = f * (deltaUV1.x * edge2 - deltaUV2.x * edge1);

        tanAccum[i0] += tangent; tanAccum[i1] += tangent; tanAccum[i2] += tangent;
        bitanAccum[i0] += bitangent; bitanAccum[i1] += bitangent; bitanAccum[i2] += bitangent;
    }

    for (size_t i = 0; i < verts.size(); ++i) {
        glm::vec3 n = DecodeNormal(verts[i].normal);
        glm::vec3 t = tanAccum[i];

        // Gram-Schmidt orthogonalize against the normal
        t = t - n * glm::dot(n, t);
        float len = glm::length(t);
        if (len < 1e-8f) {
            // Degenerate/no UV data - pick an arbitrary perpendicular vector
            glm::vec3 fallback = std::abs(n.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            t = glm::normalize(glm::cross(n, fallback));
        }
        else {
            t /= len;
        }

        // Handedness: does bitangent point the "expected" way relative to n x t?
        float handedness = (glm::dot(glm::cross(n, t), bitanAccum[i]) < 0.0f) ? -1.0f : 1.0f;

        verts[i].tangent = glm::vec4(t, handedness);

        verts[i].coarseTangent = verts[i].tangent;
    }
}

void AssetManager::LoadMesh(const std::string& path) {
    // 1. Prevent duplicate loading
    if (m_meshes.find(path) != m_meshes.end()) return;

    // 2. Route by extension
    if (path.length() > 4) {
        std::string ext = path.substr(path.length() - 4);
        // Convert to lowercase to handle .GLB or .GLTF safely
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".glb" || ext == "gltf") {
            LoadGLTF(path);
            return;
        }
    }

    // 3. Hard failure for non-glTF files
    std::cerr << "\n[AssetManager Error] Unsupported model format!\n"
        << " -> Rejected file: " << path << "\n\n";

    // Register empty dummy mesh to prevent crash and stop infinite reload loops
    m_meshes[path] = MeshAsset{};
}
void AssetManager::LoadGLTF(const std::string& path) {
    if (m_meshes.find(path) != m_meshes.end()) return;

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);

    if (result != cgltf_result_success) {
        std::cerr << "[AssetManager] FAILED TO LOAD GLTF: " << path << "\n";
        m_meshes[path] = MeshAsset{}; // Register empty to prevent infinite reload loops
        return;
    }

    cgltf_load_buffers(&options, data, path.c_str());

    std::string baseDir = "";
    size_t lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        baseDir = path.substr(0, lastSlash + 1);
    }

    MeshAsset mesh;
    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;

    // Helper to extract node transforms (glTF allows meshes to be translated/scaled inside the file)
    auto GetNodeTransform = [](cgltf_node* node) -> glm::mat4 {
        glm::mat4 transform(1.0f);
        if (node->has_matrix) {
            memcpy(&transform[0][0], node->matrix, sizeof(float) * 16);
        }
        else {
            glm::vec3 t(0.0f), s(1.0f);
            glm::quat r(1.0f, 0.0f, 0.0f, 0.0f); // w, x, y, z
            if (node->has_translation) t = glm::vec3(node->translation[0], node->translation[1], node->translation[2]);
            if (node->has_rotation) r = glm::quat(node->rotation[3], node->rotation[0], node->rotation[1], node->rotation[2]);
            if (node->has_scale) s = glm::vec3(node->scale[0], node->scale[1], node->scale[2]);

            transform = glm::translate(glm::mat4(1.0f), t) * glm::mat4_cast(r) * glm::scale(glm::mat4(1.0f), s);
        }
        return transform;
        };

    // Parse all nodes
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        cgltf_node* node = &data->nodes[i];
        if (!node->mesh) continue;

        glm::mat4 nodeTransform = GetNodeTransform(node);
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(nodeTransform)));

        for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p) {
            cgltf_primitive* primitive = &node->mesh->primitives[p];

            // Only support triangles
            if (primitive->type != cgltf_primitive_type_triangles) continue;

            SubMesh sub;
            sub.vertexOffset = globalVertexOffset;
            sub.firstIndex = globalIndexOffset;

            std::vector<ModelVertex> localVerts;
            std::vector<uint32_t> localIndices;

            // 1. EXTRACT INDICES
            if (primitive->indices) {
                cgltf_accessor* accessor = primitive->indices;
                localIndices.resize(accessor->count);
                for (cgltf_size idx = 0; idx < accessor->count; ++idx) {
                    localIndices[idx] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, idx));
                }

                for (size_t i = 0; i + 2 < localIndices.size(); i += 3) {
                    std::swap(localIndices[i + 1], localIndices[i + 2]);
                }
            }

            // 2. FIND VERTEX COUNT
            cgltf_size vertexCount = 0;
            for (cgltf_size a = 0; a < primitive->attributes_count; ++a) {
                if (primitive->attributes[a].type == cgltf_attribute_type_position) {
                    vertexCount = primitive->attributes[a].data->count;
                    break;
                }
            }
            localVerts.resize(vertexCount);

            // 3. EXTRACT ATTRIBUTES
            for (cgltf_size a = 0; a < primitive->attributes_count; ++a) {
                cgltf_attribute* attrib = &primitive->attributes[a];
                cgltf_accessor* accessor = attrib->data;

                for (cgltf_size v = 0; v < vertexCount; ++v) {
                    float values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                    cgltf_accessor_read_float(accessor, v, values, 4);

                    if (attrib->type == cgltf_attribute_type_position) {
                        glm::vec4 worldPos = nodeTransform * glm::vec4(values[0], values[1], values[2], 1.0f);
                        localVerts[v].pos = glm::vec3(worldPos);

                        // Populate coarse data for fallback logic
                        localVerts[v].coarseY = localVerts[v].pos.y;
                    }
                    else if (attrib->type == cgltf_attribute_type_normal) {
                        glm::vec3 worldNorm = normalMatrix * glm::vec3(values[0], values[1], values[2]);
                        localVerts[v].normal = EncodeNormal(glm::normalize(worldNorm));
                        localVerts[v].coarseNormal = localVerts[v].normal;
                    }
                    else if (attrib->type == cgltf_attribute_type_texcoord) {
                        localVerts[v].texCoord = glm::vec2(values[0], values[1]);
                    }
                    else if (attrib->type == cgltf_attribute_type_tangent) {
                        glm::vec3 worldTan = normalMatrix * glm::vec3(values[0], values[1], values[2]);
                        localVerts[v].coarseTangent = localVerts[v].tangent;
                    }
                }
            }

            // If the GLB didn't provide tangents, we compute them using your existing helper
            if (localVerts.size() > 0 && localVerts[0].tangent == glm::vec4(0.0f)) {
                ComputeTangents(localVerts, localIndices);
            }

            // 4. CALCULATE BOUNDS
            glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
            for (const auto& v : localVerts) {
                minB = glm::min(minB, v.pos);
                maxB = glm::max(maxB, v.pos);
            }
            sub.boundingCenterLocal = (minB + maxB) * 0.5f;
            sub.boundingRadiusLocal = glm::length(maxB - sub.boundingCenterLocal);
            sub.indexCount = static_cast<uint32_t>(localIndices.size());

            // 5. EXTRACT MATERIALS
            std::string albedoPath = "";
            std::string normalPath = "";
            std::string ormPath = "";

            if (primitive->material) {
                std::string matName = primitive->material->name ? primitive->material->name : "unnamed_mat";

                auto extractTexture = [&](cgltf_texture_view* view, int texType) -> std::string {
                    if (!view->texture || !view->texture->image) return "";
                    cgltf_image* image = view->texture->image;

                    if (image->buffer_view) {
                        std::string virtualName = path + "_" + matName + (texType == 0 ? "_albedo" : (texType == 1 ? "_normal" : "_orm"));
                        uint8_t* bufferData = (uint8_t*)image->buffer_view->buffer->data + image->buffer_view->offset;
                        LoadTextureFromMemory(virtualName, bufferData, image->buffer_view->size, texType);
                        return virtualName;
                    }
                    else if (image->uri) {
                        std::string externalPath = baseDir + image->uri;
                        size_t extPos = externalPath.find_last_of('.');
                        if (extPos != std::string::npos) externalPath.replace(extPos, externalPath.length() - extPos, ".dds");
                        std::ifstream test(externalPath);
                        if (test.good()) return externalPath;
                    }
                    return "";
                    };

                if (primitive->material->has_pbr_metallic_roughness) {
                    albedoPath = extractTexture(&primitive->material->pbr_metallic_roughness.base_color_texture, 0);
                    ormPath = extractTexture(&primitive->material->pbr_metallic_roughness.metallic_roughness_texture, 2);
                }
                normalPath = extractTexture(&primitive->material->normal_texture, 1);
            }

            // 6. PUSH TO MESH
            mesh.vertices.insert(mesh.vertices.end(), localVerts.begin(), localVerts.end());
            mesh.indices.insert(mesh.indices.end(), localIndices.begin(), localIndices.end());
            mesh.subMeshes.push_back(sub);
            mesh.materialTextures.push_back(albedoPath);
            mesh.normalMapTextures.push_back(normalPath);
            mesh.ormTextures.push_back(ormPath);

            globalVertexOffset += static_cast<uint32_t>(localVerts.size());
            globalIndexOffset += static_cast<uint32_t>(localIndices.size());

            std::cout << "[GLTF Debug] Submesh " << p << " -> Albedo: " << (albedoPath.empty() ? "EMPTY (Defaults to Dirt)" : albedoPath) << "\n";
        }
    }

    cgltf_free(data);

    m_meshes[path] = std::move(mesh);
    std::cout << "[Asset Manager] Loaded GLTF: " << path << " with " << m_meshes[path].subMeshes.size() << " submeshes\n";
}

void AssetManager::RegisterMesh(
    const std::string& name,
    std::vector<ModelVertex>&& vertices,
    std::vector<uint32_t>&& indices,
    std::vector<SubMesh> subMeshes,
    std::vector<std::string> materialTextures) {

    // 1. Erase old asset map keys quickly
    if (m_meshes.find(name) != m_meshes.end()) {
        m_meshes.erase(name);
    }

    MeshAsset mesh;
    mesh.vertices = std::move(vertices);
    mesh.indices = std::move(indices);

    if (subMeshes.empty()) {
        SubMesh sub;
        sub.indexCount = static_cast<uint32_t>(mesh.indices.size());
        sub.firstIndex = 0;
        sub.vertexOffset = 0;

        glm::vec3 minBound(FLT_MAX);
        glm::vec3 maxBound(-FLT_MAX);
        for (const auto& v : mesh.vertices) {
            minBound = glm::min(minBound, v.pos);
            maxBound = glm::max(maxBound, v.pos);
        }

        minBound.y = glm::min(minBound.y, -50.0f);
        maxBound.y = glm::max(maxBound.y, 50.0f);

        sub.boundingCenterLocal = (minBound + maxBound) * 0.5f;
        sub.boundingRadiusLocal = glm::length(maxBound - sub.boundingCenterLocal);

        sub.textureId = 0;
        sub.normalTextureId = 0;

        mesh.subMeshes.push_back(sub);
        mesh.materialTextures = { "" };
        mesh.normalMapTextures = { "" };
    }
    else {
        mesh.subMeshes = subMeshes;
        mesh.materialTextures = materialTextures;
        if (mesh.materialTextures.size() != mesh.subMeshes.size()) {
            mesh.materialTextures.resize(mesh.subMeshes.size(), "");
        }
        mesh.normalMapTextures.resize(mesh.subMeshes.size(), "");
    }

    m_meshes[name] = std::move(mesh);

#ifdef _DEBUG
    std::cout << "[Asset Manager] Registered mesh: " << name << "\n";
#endif
}
MeshAsset* AssetManager::GetMesh(const std::string& path) {
    auto it = m_meshes.find(path);
    if (it != m_meshes.end()) return &it->second;

    // LoadMesh now automatically routes to the right parser
    LoadMesh(path);
    return &m_meshes[path];
}

uint32_t AssetManager::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(GetPhysicalDevice(), &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type for buffer.");
}
void AssetManager::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(GetDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer.");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(GetDevice(), buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(GetDevice(), &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate memory.");
    }
    vkBindBufferMemory(GetDevice(), buffer, bufferMemory, 0);
}

VkCommandBuffer AssetManager::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = GetCommandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(GetDevice(), &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}
void AssetManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(GetGraphicsQueue());

    vkFreeCommandBuffers(GetDevice(), GetCommandPool(), 1, &commandBuffer);
}

VkDevice AssetManager::GetDevice() const { return m_renderer->GetLogicalDevice(); }
VkPhysicalDevice AssetManager::GetPhysicalDevice() const { return m_renderer->GetPhysicalDevice(); }
VkCommandPool AssetManager::GetCommandPool() const { return m_renderer->GetCommandPool(); }
VkQueue AssetManager::GetGraphicsQueue() const { return m_renderer->GetGraphicsQueue(); }

void AssetManager::Cleanup(VkDevice device) {
    std::cout << "[AssetManager] Cleaning up asset resources...\n";

    m_defaultTexture.CleanUp(device);
    m_defaultNormalTexture.CleanUp(device);
    m_defaultOrmTexture.CleanUp(device);

    // 1. Destroy diffuse/albedo textures exactly once via the registry
    for (auto& tex : m_textureRegistry) {
        tex.CleanUp(device);
    }
    m_textureRegistry.clear();
    m_textureToId.clear();

    // 2. Destroy normal textures exactly once via the normal registry
    for (auto& tex : m_normalTextureRegistry) {
        tex.CleanUp(device);
    }
    m_normalTextureRegistry.clear();
    m_normalTextureToId.clear();

    for (auto& tex : m_ormTextureRegistry) {
        tex.CleanUp(device);
    }
    m_ormTextureRegistry.clear();
    m_ormTextureToId.clear();

    m_meshes.clear();
}
