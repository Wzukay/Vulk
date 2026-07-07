#include "assetManager.h"
#include "renderer.h"
#include "stb_image.h"

#include <iostream>
#include <algorithm>
#include <cfloat>
#include <unordered_set>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

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
    CopyBufferToImage(stagingBuffer, m_defaultTexture.image, 1, 1);
    TransitionImageLayout(m_defaultTexture.image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    CreateTextureImageView(m_defaultTexture, VK_FORMAT_R8G8B8A8_SRGB);
    CreateTextureSampler(m_defaultTexture);

    // Register as first texture
    m_textureRegistry.clear();
    m_textureRegistry.push_back(m_defaultTexture);
    m_textureToId["default"] = 0;

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
void AssetManager::LoadTexture(const std::string& path) {
    if (m_descriptorSet == VK_NULL_HANDLE) {
        std::cerr << "[AssetManager] ERROR: m_descriptorSet is null in LoadTexture!\n";
        return;
    }
    if (path.empty() || path == "default") return;
    if (m_textures.find(path) != m_textures.end()) return;

    try {
        Texture tex;
        CreateTextureImage(path, tex);
        m_textures[path] = tex;
        m_textureRegistry.push_back(tex);
        uint32_t id = static_cast<uint32_t>(m_textureRegistry.size()) - 1;
        m_textureToId[path] = id;

        // Write to descriptor set
        VkDescriptorImageInfo descriptorInfo{};
        descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorInfo.imageView = tex.imageView;
        descriptorInfo.sampler = tex.sampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSet;
        descriptorWrite.dstBinding = 2;
        descriptorWrite.dstArrayElement = id;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &descriptorInfo;

        vkUpdateDescriptorSets(GetDevice(), 1, &descriptorWrite, 0, nullptr);
        std::cout << "[AssetManager] Loaded texture: " << path << " (ID: " << id << ")\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[AssetManager] Exception loading texture '" << path << "': " << e.what() << "\n";
        // Do not re-throw; we'll fall back to default texture later.
    }
}
Texture* AssetManager::GetTexture(const std::string& path) {
    // Return default texture for empty path or "default"
    if (path.empty() || path == "default") return &m_defaultTexture;
    auto it = m_textures.find(path);
    if (it != m_textures.end()) return &it->second;
    LoadTexture(path);
    return &m_textures[path];
}
uint32_t AssetManager::GetTextureId(const std::string& path) {
    // Return 0 for empty path or "default"
    if (path.empty() || path == "default") return 0;
    auto it = m_textureToId.find(path);
    if (it != m_textureToId.end()) return it->second;
    LoadTexture(path);
    return m_textureToId[path];
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
void AssetManager::CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) {
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
void AssetManager::GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(GetPhysicalDevice(), imageFormat, &formatProperties);
    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("Texture image format does not support linear blitting for mipmap generation.");
    }

    VkCommandBuffer commandBuffer = BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;
    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkImageBlit blit{};
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;
        vkCmdBlitImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    EndSingleTimeCommands(commandBuffer);
}
void AssetManager::CreateTextureImage(const std::string& path, Texture& texture, VkFormat format) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        std::string err = stbi_failure_reason();
        throw std::runtime_error("Failed to load texture image: " + path + " (STB error: " + err + ")");
    }

    texture.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(GetDevice(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(GetDevice(), stagingBufferMemory);
    stbi_image_free(pixels);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>(texWidth);
    imageInfo.extent.height = static_cast<uint32_t>(texHeight);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = texture.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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

    TransitionImageLayout(texture.image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels);
    CopyBufferToImage(stagingBuffer, texture.image, texWidth, texHeight);
    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    GenerateMipmaps(texture.image, format, texWidth, texHeight, texture.mipLevels);
    CreateTextureImageView(texture, format);
    CreateTextureSampler(texture);
}
void AssetManager::CreateTextureImageView(Texture& texture, VkFormat format) {
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
}
void AssetManager::CreateTextureSampler(Texture& texture) {
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
    samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
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
    CopyBufferToImage(stagingBuffer, m_defaultNormalTexture.image, 1, 1);
    TransitionImageLayout(m_defaultNormalTexture.image, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(GetDevice(), stagingBuffer, nullptr);
    vkFreeMemory(GetDevice(), stagingBufferMemory, nullptr);

    CreateTextureImageView(m_defaultNormalTexture, VK_FORMAT_R8G8B8A8_UNORM);
    CreateTextureSampler(m_defaultNormalTexture);

    // Register as first normal texture (index 0)
    m_normalTextureRegistry.clear();
    m_normalTextureRegistry.push_back(m_defaultNormalTexture);
    m_normalTextureToId["default"] = 0;

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
void AssetManager::LoadNormalTexture(const std::string& path) {
    if (m_descriptorSet == VK_NULL_HANDLE) {
        std::cerr << "[AssetManager] ERROR: m_descriptorSet is null in LoadNormalTexture!\n";
        return;
    }
    if (path.empty()) return;
    if (m_normalTextures.find(path) != m_normalTextures.end()) return;

    try {
        Texture tex;
        CreateTextureImage(path, tex, VK_FORMAT_R8G8B8A8_UNORM); // reuses existing loader; format concern noted below
        m_normalTextures[path] = tex;
        m_normalTextureRegistry.push_back(tex);
        uint32_t id = static_cast<uint32_t>(m_normalTextureRegistry.size()); // +1 offset, since slot 0 is default
        m_normalTextureToId[path] = id;

        VkDescriptorImageInfo descriptorInfo{};
        descriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descriptorInfo.imageView = tex.imageView;
        descriptorInfo.sampler = tex.sampler;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSet;
        descriptorWrite.dstBinding = 3;
        descriptorWrite.dstArrayElement = id;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &descriptorInfo;

        vkUpdateDescriptorSets(GetDevice(), 1, &descriptorWrite, 0, nullptr);
        std::cout << "[AssetManager] Loaded normal map: " << path << " (ID: " << id << ")\n";
    }
    catch (const std::exception& e) {
        std::cerr << "[AssetManager] Exception loading normal map '" << path << "': " << e.what() << "\n";
    }
}
Texture* AssetManager::GetNormalTexture(const std::string& path) {
    if (path.empty()) return &m_defaultNormalTexture;
    auto it = m_normalTextures.find(path);
    if (it != m_normalTextures.end()) return &it->second;
    LoadNormalTexture(path);
    return &m_normalTextures[path];
}
uint32_t AssetManager::GetNormalTextureId(const std::string& path) {
    if (path.empty()) return 0; // slot 0 reserved for default flat normal
    auto it = m_normalTextureToId.find(path);
    if (it != m_normalTextureToId.end()) return it->second;
    LoadNormalTexture(path);
    return m_normalTextureToId[path];
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
        glm::vec3 n = verts[i].normal;
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
    }
}
static std::string GuessNormalMapPath(const std::string& diffusePath) {
    if (diffusePath.empty()) return "";

    std::string candidate = diffusePath;
    size_t pos = candidate.rfind("_diff");
    if (pos != std::string::npos) {
        candidate.replace(pos, 5, "_ddn"); // "_diff.tga" -> "_ddn.tga"
        std::ifstream test(candidate);
        if (test.good()) return candidate;
    }
    return "";
}
void AssetManager::ParseObjFileByMaterial(const std::string& filepath,
    std::vector<std::vector<ModelVertex>>& verticesPerMaterial,
    std::vector<std::vector<uint32_t>>& indicesPerMaterial,
    std::vector<std::string>& textureFilenames,
    std::vector<std::string>& normalMapFilenames) {

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string err;

    std::string baseDir = "";
    size_t lastSlash = filepath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        baseDir = filepath.substr(0, lastSlash + 1);
    }

    bool result = tinyobj::LoadObj(&attrib, &shapes, &materials, &err,
        filepath.c_str(), baseDir.empty() ? nullptr : baseDir.c_str(), true);

    if (!result) {
        throw std::runtime_error("[Asset Error] Failed to parse .obj file structure: " + err);
    }

    size_t materialCount = materials.size();
    size_t noMaterialIndex = materialCount;

    verticesPerMaterial.resize(materialCount + 1);
    indicesPerMaterial.resize(materialCount + 1);
    textureFilenames.resize(materialCount + 1, "");
    normalMapFilenames.resize(materialCount + 1, "");

    for (size_t m = 0; m < materialCount; m++) {
        textureFilenames[m] = materials[m].diffuse_texname.empty()
            ? ""
            : baseDir + materials[m].diffuse_texname;

        normalMapFilenames[m] = materials[m].bump_texname.empty()  // NEW
            ? "" : baseDir + materials[m].bump_texname;

        if (normalMapFilenames[m].empty()) {
            normalMapFilenames[m] = GuessNormalMapPath(textureFilenames[m]);
        }
    }

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
                auto& verts = verticesPerMaterial[materialId];
                auto& idxs = indicesPerMaterial[materialId];

                if (uniqueVertices.count(vertex) == 0) {
                    uniqueVertices[vertex] = static_cast<uint32_t>(verts.size());
                    verts.push_back(vertex);
                }
                idxs.push_back(uniqueVertices[vertex]);
            }
        }
    }

    std::cout << "[Asset Manager] '" << filepath << "' split into " << materials.size()
        << " materials (+ possible untextured group)\n";
}
void AssetManager::LoadMesh(const std::string& path) {
    if (m_meshes.find(path) != m_meshes.end()) return;

    std::vector<std::vector<ModelVertex>> verticesPerMaterial;
    std::vector<std::vector<uint32_t>> indicesPerMaterial;
    std::vector<std::string> textureFilenames;
    std::vector<std::string> normalMapFilenames;

    ParseObjFileByMaterial(path, verticesPerMaterial, indicesPerMaterial, textureFilenames, normalMapFilenames);

    // Flip winding to match Vulkan's clockwise front face
    for (auto& idxs : indicesPerMaterial) {
        for (size_t i = 0; i + 2 < idxs.size(); i += 3) {
            std::swap(idxs[i + 1], idxs[i + 2]);
        }
    }

    for (size_t m = 0; m < verticesPerMaterial.size(); ++m) {
        if (indicesPerMaterial[m].empty()) continue;
        ComputeTangents(verticesPerMaterial[m], indicesPerMaterial[m]);
    }

    MeshAsset mesh;
    uint32_t vertexOffset = 0;
    uint32_t indexOffset = 0;

    for (size_t m = 0; m < verticesPerMaterial.size(); ++m) {
        const auto& verts = verticesPerMaterial[m];
        const auto& idxs = indicesPerMaterial[m];
        if (idxs.empty()) continue;

        // Compute bounding sphere
        glm::vec3 minBound(FLT_MAX), maxBound(-FLT_MAX);
        for (const auto& v : verts) {
            minBound = glm::min(minBound, v.pos);
            maxBound = glm::max(maxBound, v.pos);
        }
        glm::vec3 center = (minBound + maxBound) * 0.5f;
        float radius = glm::length(maxBound - center);

        SubMesh sub;
        sub.indexCount = static_cast<uint32_t>(idxs.size());
        sub.firstIndex = indexOffset;
        sub.vertexOffset = static_cast<int32_t>(vertexOffset);
        sub.boundingCenterLocal = center;
        sub.boundingRadiusLocal = radius;
        sub.textureId = 0; // will be set later when scene assigns materials

        // Append to mesh
        mesh.vertices.insert(mesh.vertices.end(), verts.begin(), verts.end());
        mesh.indices.insert(mesh.indices.end(), idxs.begin(), idxs.end());
        mesh.subMeshes.push_back(sub);

        mesh.materialTextures.push_back(textureFilenames[m]);
        mesh.normalMapTextures.push_back(normalMapFilenames[m]);

        vertexOffset += static_cast<uint32_t>(verts.size());
        indexOffset += static_cast<uint32_t>(idxs.size());
    }

    m_meshes[path] = std::move(mesh);
    std::cout << "[Asset Manager] Loaded mesh: " << path << " with " << m_meshes[path].subMeshes.size() << " submeshes\n";
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
    LoadMesh(path);  // load on dem and
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
    for (auto& tex : m_textureRegistry) {
        if (tex.sampler)     vkDestroySampler(device, tex.sampler, nullptr);
        if (tex.imageView)   vkDestroyImageView(device, tex.imageView, nullptr);
        if (tex.image)       vkDestroyImage(device, tex.image, nullptr);
        if (tex.imageMemory) vkFreeMemory(device, tex.imageMemory, nullptr);
    }
    m_textureRegistry.clear();
    m_textureToId.clear();

    for (auto& pair : m_textures) {
        Texture& tex = pair.second;
        if (tex.sampler)     vkDestroySampler(device, tex.sampler, nullptr);
        if (tex.imageView)   vkDestroyImageView(device, tex.imageView, nullptr);
        if (tex.image)       vkDestroyImage(device, tex.image, nullptr);
        if (tex.imageMemory) vkFreeMemory(device, tex.imageMemory, nullptr);
        // Zero them to avoid double-destruction if the same texture appears again
        tex.sampler = VK_NULL_HANDLE;
        tex.imageView = VK_NULL_HANDLE;
        tex.image = VK_NULL_HANDLE;
        tex.imageMemory = VK_NULL_HANDLE;
    }
    m_textures.clear();

    for (auto& tex : m_normalTextureRegistry) {
        if (tex.sampler)     vkDestroySampler(device, tex.sampler, nullptr);
        if (tex.imageView)   vkDestroyImageView(device, tex.imageView, nullptr);
        if (tex.image)       vkDestroyImage(device, tex.image, nullptr);
        if (tex.imageMemory) vkFreeMemory(device, tex.imageMemory, nullptr);
    }
    m_normalTextureRegistry.clear();
    m_normalTextureToId.clear();

    for (auto& pair : m_normalTextures) {
        Texture& tex = pair.second;
        if (tex.sampler)     vkDestroySampler(device, tex.sampler, nullptr);
        if (tex.imageView)   vkDestroyImageView(device, tex.imageView, nullptr);
        if (tex.image)       vkDestroyImage(device, tex.image, nullptr);
        if (tex.imageMemory) vkFreeMemory(device, tex.imageMemory, nullptr);
        tex.sampler = VK_NULL_HANDLE;
        tex.imageView = VK_NULL_HANDLE;
        tex.image = VK_NULL_HANDLE;
        tex.imageMemory = VK_NULL_HANDLE;
    }
    m_normalTextures.clear();

    m_meshes.clear();
}
