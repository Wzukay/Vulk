#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "scene_types.h"
#include "texture.h"
#include "renderMesh.h"

class VulkanRenderer;

struct MeshAsset {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<std::string> materialTextures; // per submesh
};

class AssetManager {
private:
    VulkanRenderer* m_renderer = nullptr;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    std::unordered_map<std::string, MeshAsset> m_meshes;
    std::unordered_map<std::string, Texture> m_textures;
    std::unordered_map<std::string, uint32_t> m_textureToId;
    std::vector<Texture> m_textureRegistry;

    Texture m_defaultTexture;

public:
    void Cleanup(VkDevice device);
    void SetRenderer(VulkanRenderer* renderer) { m_renderer = renderer; }

public:
    void LoadMesh(const std::string& path);
    void RegisterMesh(const std::string& name, const std::vector<ModelVertex>& vertices,
        const std::vector<uint32_t>& indices,
        const std::vector<SubMesh>& subMeshes = {},
        const std::vector<std::string>& materialTextures = {});
    MeshAsset* GetMesh(const std::string& path);
    void ParseObjFileByMaterial(const std::string& filepath,
        std::vector<std::vector<ModelVertex>>& verticesPerMaterial,
        std::vector<std::vector<uint32_t>>& indicesPerMaterial,
        std::vector<std::string>& textureFilenames);
    void SetDescriptorSet(VkDescriptorSet set) { m_descriptorSet = set; }

public:
    void CreateTextureImage(const std::string& path, Texture& tex);
    void CreateTextureImageView(Texture& tex, VkFormat format);
    void CreateTextureSampler(Texture& tex);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
    void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
    Texture* GetTexture(const std::string& path);
    uint32_t GetTextureId(const std::string& path);
    void LoadTexture(const std::string& path);

    const std::vector<Texture>& GetTextureRegistry() const { return m_textureRegistry; }
    const std::unordered_map<std::string, uint32_t>& GetTextureMap() const { return m_textureToId; }

    Texture& GetDefaultTexture() { return m_defaultTexture; }
    void CreateDefaultTexture();

private:
    VkDevice GetDevice() const;
    VkPhysicalDevice GetPhysicalDevice() const;
    VkCommandPool GetCommandPool() const;
    VkQueue GetGraphicsQueue() const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
};

extern AssetManager g_AssetManager;