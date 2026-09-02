#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <fstream>
#include <glm/glm.hpp>

#include "scene_types.h"
#include "texture.h"
#include "mesh.h"
#include "settings.h"

class VulkanRenderer;

struct MeshAsset {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<std::string> materialTextures;
    std::vector<std::string> normalMapTextures;
    std::vector<std::string> ormTextures;
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

    std::unordered_map<std::string, uint32_t> m_ormTextureToId;
    std::vector<Texture> m_ormTextureRegistry;

    Texture m_defaultOrmTexture;
    Texture m_defaultNormalTexture;
    Texture m_defaultTexture;

public:
    void Cleanup(VkDevice device);
    void SetRenderer(VulkanRenderer* renderer) { m_renderer = renderer; }

    void LoadMesh(const std::string& path);
    void LoadGLTF(const std::string& path);

    void RegisterMesh(
        const std::string& name,
        std::vector<ModelVertex>&& vertices,   
        std::vector<uint32_t>&& indices,                     
        std::vector<SubMesh> subMeshes = {},            
        std::vector<std::string> materialTextures = {}
    );
    void UnregisterMesh(const std::string& name) { m_meshes.erase(name); }
    MeshAsset* GetMesh(const std::string& path);

    void SetDescriptorSet(VkDescriptorSet set) { m_descriptorSet = set; }

    uint32_t LoadTextureFromMemory(const std::string& virtualName, const uint8_t* buffer, size_t bufferSize, int texType);

public:
    uint32_t LoadTextureFromFile(const std::string& filePath);
    uint32_t LoadNormalTextureFromFile(const std::string& filePath);
    uint32_t LoadOrmTextureFromFile(const std::string& filePath);

    void CreateTextureImage(const std::string& path, Texture& tex, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    void TransitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels = 1);

    // ---------

    Texture* GetTexture(const std::string& path);
    uint32_t GetTextureId(const std::string& path);
    void LoadTexture(const std::string& path);
    const std::vector<Texture>& GetTextureRegistry() const { return m_textureRegistry; }
    const std::unordered_map<std::string, uint32_t>& GetTextureMap() const { return m_textureToId; }
    void CreateDefaultTexture();

    // ---------

    void LoadOrmTexture(const std::string& path);
    Texture* GetOrmTexture(const std::string& path);
    uint32_t GetOrmTextureId(const std::string& path);
    const std::vector<Texture>& GetOrmTextureRegistry() const { return m_ormTextureRegistry; }
    const std::unordered_map<std::string, uint32_t>& GetOrmTextureMap() const { return m_ormTextureToId; }
    void CreateDefaultOrmTexture();

    // ---------

    Texture* GetNormalTexture(const std::string& path);
    uint32_t GetNormalTextureId(const std::string& path);
    void LoadNormalTexture(const std::string& path);
    Texture& GetDefaultTexture() { return m_defaultTexture; }
    const std::vector<Texture>& GetNormalTextureRegistry() const { return m_normalTextureRegistry; }
    const std::unordered_map<std::string, uint32_t>& GetNormalTextureMap() const { return m_normalTextureToId; }
    void CreateDefaultNormalTexture();

    // ---------

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