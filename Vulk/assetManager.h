#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <fstream>
#include <glm/glm.hpp>

#include "scene_types.h"
#include "texture.h"
#include "renderMesh.h"
#include "settings.h"

class VulkanRenderer;

struct MeshAsset {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<std::string> materialTextures; // per submesh
    std::vector<std::string> normalMapTextures;
};

struct DDSHeader {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwHeight;
    uint32_t dwWidth;
    uint32_t dwLinearSize;
    uint32_t dwDepth;
    uint32_t dwMipMapCount;
    uint32_t dwReserved1[11];
    struct {
        uint32_t dwSize;
        uint32_t dwFlags;
        uint32_t dwFourCC;
        uint32_t dwRGBBitCount;
        uint32_t dwRBitMask;
        uint32_t dwGBitMask;
        uint32_t dwBBitMask;
        uint32_t dwABitMask;
    } ddspf;
    uint32_t dwCaps;
    uint32_t dwCaps2;
    uint32_t dwCaps3;
    uint32_t dwCaps4;
    uint32_t dwReserved2;
};

class AssetManager {
private:
    VulkanRenderer* m_renderer = nullptr;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    std::unordered_map<std::string, MeshAsset> m_meshes;

    std::unordered_map<std::string, uint32_t> m_textureToId;
    std::vector<Texture> m_textureRegistry;
  
    std::unordered_map<std::string, uint32_t> m_normalTextureToId;    
    std::vector<Texture> m_normalTextureRegistry;                   

    Texture m_defaultNormalTexture;
    Texture m_defaultTexture;

public:
    void Cleanup(VkDevice device);
    void SetRenderer(VulkanRenderer* renderer) { m_renderer = renderer; }

    void LoadMesh(const std::string& path);
    void RegisterMesh(
        const std::string& name,
        std::vector<ModelVertex>&& vertices,   
        std::vector<uint32_t>&& indices,                     
        std::vector<SubMesh> subMeshes = {},            
        std::vector<std::string> materialTextures = {}
    );
    void UnregisterMesh(const std::string& name) { m_meshes.erase(name); }
    MeshAsset* GetMesh(const std::string& path);
    void ParseObjFileByMaterial(const std::string& filepath,
        std::vector<std::vector<ModelVertex>>& verticesPerMaterial,
        std::vector<std::vector<uint32_t>>& indicesPerMaterial,
        std::vector<std::string>& textureFilenames,
        std::vector<std::string>& normalMapFilenames);
    void SetDescriptorSet(VkDescriptorSet set) { m_descriptorSet = set; }

public:
    uint32_t LoadTextureFromFile(const std::string& filePath);
    uint32_t LoadNormalTextureFromFile(const std::string& filePath);
    void CreateTextureImage(const std::string& path, Texture& tex, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    void CreateTextureImageView(Texture& tex, VkFormat format);
    void CreateTextureSampler(Texture& tex);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1);
    void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

    Texture* GetTexture(const std::string& path);
    uint32_t GetTextureId(const std::string& path);
    
    Texture* GetNormalTexture(const std::string& path);     
    uint32_t GetNormalTextureId(const std::string& path);   

    void LoadTexture(const std::string& path);
    void LoadNormalTexture(const std::string& path);        

    void CreateDefaultTexture();
    void CreateDefaultNormalTexture();

    const std::vector<Texture>& GetTextureRegistry() const { return m_textureRegistry; }
    const std::unordered_map<std::string, uint32_t>& GetTextureMap() const { return m_textureToId; }

    Texture& GetDefaultTexture() { return m_defaultTexture; }

    bool IsTextureDirty() const { return m_textureDirty; }
    void ClearTextureDirty() { m_textureDirty = false; }

private:
    bool m_textureDirty = false;

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