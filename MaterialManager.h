#pragma once
#include <vector>
#include <string>

#include <vulkan/vulkan.h>

#include"ThirdParty/Common/stb_image.h"

#include "VkHelper.h"


namespace vkapp
{
    class VulkanContext;
    struct Vertex;
}

//struct Material
//{
//    VkImage        image = VK_NULL_HANDLE;
//    VkDeviceMemory memory = VK_NULL_HANDLE;
//    VkImageView    imageView = VK_NULL_HANDLE;
//    VkSampler      sampler = VK_NULL_HANDLE;
//    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
//};

//vkapp::Material;


class MaterialManager
{
public:
    bool Init(vkapp::VulkanContext* ctx, uint32_t maxMaterials);
    void Cleanup();

    uint32_t CreateMaterial(const std::string& texturePath);

    uint32_t CreateMaterialFromMemory(
        const unsigned char* pixels,
        int width,
        int height);

    uint32_t CreateDefaultMaterial();

    VkDescriptorSetLayout GetLayout() const { return m_materialSetLayout; }
    const Material& GetMaterial(uint32_t index) const { return m_materials[index]; }

    const inline int GetMaterialCount() { return m_materials.size(); };

private:
    vkapp::VulkanContext* m_context = nullptr;

    VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;

    std::vector<Material> m_materials;

    bool CreateDescriptorLayout();
    bool CreateDescriptorPool(uint32_t maxMaterials);
};

