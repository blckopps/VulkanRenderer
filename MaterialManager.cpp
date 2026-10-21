#include "MaterialManager.h"
#include "VulkanContext.h"

#include <array>
#include <cstring>
#include <stdexcept>
#include <string>

namespace
{
    bool UploadTextureRGBA8(VkDevice device, VkPhysicalDevice physicalDevice,
        VkCommandPool commandPool, VkQueue graphicsQueue,
        const unsigned char* pixels, int width, int height, VkFormat format,
        VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView)
    {
        if (!pixels || width <= 0 || height <= 0) return false;
        const VkDeviceSize size = VkDeviceSize(width) * VkDeviceSize(height) * 4;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        try
        {
            CreateBufferRaw(device, physicalDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                staging, stagingMemory);
            void* mapped = nullptr;
            VkResult result = vkMapMemory(device, stagingMemory, 0, size, 0, &mapped);
            if (result != VK_SUCCESS) throw std::runtime_error("Texture staging map failed.");
            std::memcpy(mapped, pixels, static_cast<size_t>(size));
            vkUnmapMemory(device, stagingMemory);

            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = format;
            imageInfo.extent = { uint32_t(width), uint32_t(height), 1 };
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            result = vkCreateImage(device, &imageInfo, nullptr, &outImage);
            if (result != VK_SUCCESS) throw std::runtime_error("Texture image creation failed.");

            VkMemoryRequirements requirements{};
            vkGetImageMemoryRequirements(device, outImage, &requirements);
            VkMemoryAllocateInfo memoryInfo{};
            memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memoryInfo.allocationSize = requirements.size;
            memoryInfo.memoryTypeIndex = FindMemoryType(physicalDevice,
                requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            result = vkAllocateMemory(device, &memoryInfo, nullptr, &outMemory);
            if (result != VK_SUCCESS) throw std::runtime_error("Texture image memory allocation failed.");
            result = vkBindImageMemory(device, outImage, outMemory, 0);
            if (result != VK_SUCCESS) throw std::runtime_error("Texture image memory binding failed.");

            const bool submitted = SubmitImmediate(device, commandPool, graphicsQueue,
                [&](VkCommandBuffer cmd)
                {
                    VkImageMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = outImage;
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.levelCount = 1;
                    barrier.subresourceRange.layerCount = 1;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

                    VkBufferImageCopy copy{};
                    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    copy.imageSubresource.layerCount = 1;
                    copy.imageExtent = { uint32_t(width), uint32_t(height), 1 };
                    vkCmdCopyBufferToImage(cmd, staging, outImage,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
                });
            if (!submitted) throw std::runtime_error("Texture upload command submission failed.");

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = outImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;
            result = vkCreateImageView(device, &viewInfo, nullptr, &outView);
            if (result != VK_SUCCESS) throw std::runtime_error("Texture image view creation failed.");

            vkDestroyBuffer(device, staging, nullptr);
            vkFreeMemory(device, stagingMemory, nullptr);
            return true;
        }
        catch (...)
        {
            if (staging) vkDestroyBuffer(device, staging, nullptr);
            if (stagingMemory) vkFreeMemory(device, stagingMemory, nullptr);
            if (outView) { vkDestroyImageView(device, outView, nullptr); outView = VK_NULL_HANDLE; }
            if (outImage) { vkDestroyImage(device, outImage, nullptr); outImage = VK_NULL_HANDLE; }
            if (outMemory) { vkFreeMemory(device, outMemory, nullptr); outMemory = VK_NULL_HANDLE; }
            throw;
        }
    }

    bool CreateSampler(VkDevice device, VkSampler& sampler)
    {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_LINEAR;
        info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        info.maxAnisotropy = 1.0f;
        info.minLod = 0.0f;
        info.maxLod = 0.0f;
        return vkCreateSampler(device, &info, nullptr, &sampler) == VK_SUCCESS;
    }

    void DestroyMaterialResources(VkDevice device, Material& m)
    {
        if (m.materialUBO) vkDestroyBuffer(device, m.materialUBO, nullptr);
        if (m.materialUBOMemory) vkFreeMemory(device, m.materialUBOMemory, nullptr);
        if (m.sampler) vkDestroySampler(device, m.sampler, nullptr);
        if (m.imageView) vkDestroyImageView(device, m.imageView, nullptr);
        if (m.image) vkDestroyImage(device, m.image, nullptr);
        if (m.memory) vkFreeMemory(device, m.memory, nullptr);
        if (m.metallicRoughnessSampler) vkDestroySampler(device, m.metallicRoughnessSampler, nullptr);
        if (m.metallicRoughnessImageView) vkDestroyImageView(device, m.metallicRoughnessImageView, nullptr);
        if (m.metallicRoughnessimage) vkDestroyImage(device, m.metallicRoughnessimage, nullptr);
        if (m.metallicRoughnessMemory) vkFreeMemory(device, m.metallicRoughnessMemory, nullptr);
        m = Material{};
    }
}

bool MaterialManager::Init(vkapp::VulkanContext* ctx, uint32_t maxMaterials)
{
    if (!ctx || maxMaterials == 0 || m_context || m_materialSetLayout || m_descriptorPool)
        return false;
    m_context = ctx;
    if (!CreateDescriptorLayout()) { m_context = nullptr; return false; }
    if (!CreateDescriptorPool(maxMaterials))
    {
        vkDestroyDescriptorSetLayout(m_context->Device(), m_materialSetLayout, nullptr);
        m_materialSetLayout = VK_NULL_HANDLE;
        m_context = nullptr;
        return false;
    }
    return true;
}

void MaterialManager::Cleanup()
{
    if (!m_context) return;
    VkDevice device = m_context->Device();
    vkDeviceWaitIdle(device);
    if (m_descriptorPool) { vkDestroyDescriptorPool(device, m_descriptorPool, nullptr); m_descriptorPool = VK_NULL_HANDLE; }
    for (Material& m : m_materials) DestroyMaterialResources(device, m);
    m_materials.clear();
    if (m_materialSetLayout) { vkDestroyDescriptorSetLayout(device, m_materialSetLayout, nullptr); m_materialSetLayout = VK_NULL_HANDLE; }
    m_context = nullptr;
}

bool MaterialManager::CreateDescriptorLayout()
{
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    return vkCreateDescriptorSetLayout(m_context->Device(), &info, nullptr,
        &m_materialSetLayout) == VK_SUCCESS;
}

bool MaterialManager::CreateDescriptorPool(uint32_t maxMaterials)
{
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[0].descriptorCount = maxMaterials * 2;
    sizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sizes[1].descriptorCount = maxMaterials;
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    info.pPoolSizes = sizes.data();
    info.maxSets = maxMaterials;
    return vkCreateDescriptorPool(m_context->Device(), &info, nullptr,
        &m_descriptorPool) == VK_SUCCESS;
}

uint32_t MaterialManager::CreateMaterial(const std::string& texturePath)
{
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) throw std::runtime_error("Failed to load texture: " + texturePath);
    try
    {
        uint32_t index = CreateMaterialFromMemory(pixels, width, height);
        stbi_image_free(pixels);
        return index;
    }
    catch (...) { stbi_image_free(pixels); throw; }
}

uint32_t MaterialManager::CreateMaterialFromMemory(const unsigned char* pixels, int width, int height)
{
    if (!m_context || !m_descriptorPool || !m_materialSetLayout)
        throw std::runtime_error("MaterialManager must be initialized before creating materials.");
    if (!pixels || width <= 0 || height <= 0)
        throw std::invalid_argument("Invalid RGBA pixels or texture dimensions.");

    Material m{};
    VkDevice device = m_context->Device();
    VkPhysicalDevice physical = m_context->PhysicalDevice();
    try
    {
        // Base color is color data: SRGB format.
        UploadTextureRGBA8(device, physical, m_context->CommandPool(), m_context->GraphicsQueue(),
            pixels, width, height, VK_FORMAT_R8G8B8A8_SRGB, m.image, m.memory, m.imageView);
        if (!CreateSampler(device, m.sampler)) throw std::runtime_error("Base-color sampler creation failed.");

        // Fallback glTF metallic/roughness texture. G=roughness, B=metallic; both 1.
        static const unsigned char defaultMR[4] = { 255, 255, 255, 255 };
        UploadTextureRGBA8(device, physical, m_context->CommandPool(), m_context->GraphicsQueue(),
            defaultMR, 1, 1, VK_FORMAT_R8G8B8A8_UNORM,
            m.metallicRoughnessimage, m.metallicRoughnessMemory, m.metallicRoughnessImageView);
        if (!CreateSampler(device, m.metallicRoughnessSampler))
            throw std::runtime_error("Metallic/roughness sampler creation failed.");

        // Matches MaterialUBO currently defined in VkHelper.h.
        m.baseColorFactor = glm::vec4(1.0f);
        m.metallicFactor = 1.0f;
        m.roughnessFactor = 1.0f;
        MaterialUBO ubo{};
        ubo.baseColorFactor = m.baseColorFactor;
        ubo.metallicRoughness.x = m.metallicFactor;
        ubo.metallicRoughness.y = m.roughnessFactor;

        CreateBufferRaw(device, physical, sizeof(MaterialUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m.materialUBO, m.materialUBOMemory);
        void* mapped = nullptr;
        VkResult result = vkMapMemory(device, m.materialUBOMemory, 0, sizeof(MaterialUBO), 0, &mapped);
        if (result != VK_SUCCESS) throw std::runtime_error("Material UBO map failed.");
        std::memcpy(mapped, &ubo, sizeof(ubo));
        vkUnmapMemory(device, m.materialUBOMemory);

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &m_materialSetLayout;
        result = vkAllocateDescriptorSets(device, &alloc, &m.descriptorSet);
        if (result != VK_SUCCESS) throw std::runtime_error("Material descriptor-set allocation failed.");

        VkDescriptorImageInfo baseInfo{};
        baseInfo.sampler = m.sampler;
        baseInfo.imageView = m.imageView;
        baseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo mrInfo{};
        mrInfo.sampler = m.metallicRoughnessSampler;
        mrInfo.imageView = m.metallicRoughnessImageView;
        mrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m.materialUBO;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(MaterialUBO);

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m.descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &baseInfo;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m.descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &mrInfo;
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = m.descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        m_materials.push_back(m);
        return static_cast<uint32_t>(m_materials.size() - 1);
    }
    catch (...)
    {
        DestroyMaterialResources(device, m);
        throw;
    }
}

uint32_t MaterialManager::CreateDefaultMaterial()
{
    static const unsigned char whitePixel[4] = { 255, 255, 255, 255 };
    return CreateMaterialFromMemory(whitePixel, 1, 1);
}
