#include "MaterialManager.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "VulkanContext.h"



class VulkanContext;

bool MaterialManager::Init(vkapp::VulkanContext* ctx, uint32_t maxMaterials)
{
    m_context = ctx;

    if (!CreateDescriptorLayout())
        return false;

    if (!CreateDescriptorPool(maxMaterials))
        return false;

    return true;
}

bool MaterialManager::CreateDescriptorLayout()
{
    VkDescriptorSetLayoutBinding sampler{};
    sampler.binding = 0;
    sampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler.descriptorCount = 1;
    sampler.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = &sampler;

    return vkCreateDescriptorSetLayout(
        m_context->Device(),
        &info,
        nullptr,
        &m_materialSetLayout) == VK_SUCCESS;
}

bool MaterialManager::CreateDescriptorPool(uint32_t maxMaterials)
{
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = maxMaterials;

    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = 1;
    info.pPoolSizes = &poolSize;
    info.maxSets = maxMaterials;

    return vkCreateDescriptorPool(
        m_context->Device(),
        &info,
        nullptr,
        &m_descriptorPool) == VK_SUCCESS;
}

uint32_t MaterialManager::CreateMaterial(const std::string& texturePath)
{
    Material mat{};

    // ---- Load texture with stb ----
    int w, h, c;
    stbi_uc* pixels = stbi_load(texturePath.c_str(), &w, &h, &c, STBI_rgb_alpha);
    if (!pixels)
        throw std::runtime_error("Failed to load texture");

    VkDeviceSize size = w * h * 4;

    // Create GPU image + copy (reuse your existing helper functions)
    // (Assume you already have CreateBufferRaw, SubmitImmediate, etc.)

    // ... (texture upload logic here exactly as before)

    // Allocate descriptor
    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = m_descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &m_materialSetLayout;

    vkAllocateDescriptorSets(
        m_context->Device(),
        &alloc,
        &mat.descriptorSet);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = mat.imageView;
    imageInfo.sampler = mat.sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = mat.descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(
        m_context->Device(),
        1,
        &write,
        0,
        nullptr);

    m_materials.push_back(mat);
    return static_cast<uint32_t>(m_materials.size() - 1);
}


uint32_t MaterialManager::CreateMaterialFromMemory(
    const unsigned char* pixels,
    int width,
    int height)
{
    Material mat{};

    VkDevice device = m_context->Device();
    VkPhysicalDevice phys = m_context->PhysicalDevice();
    VkCommandPool cmdPool = m_context->CommandPool();
    VkQueue vkQueue = m_context->GraphicsQueue();

    VkDeviceSize imageSize = width * height * 4;

    // =========================
    //  Create staging buffer
    // =========================
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    CreateBufferRaw(
        device,
        phys,
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingMemory);

    void* data;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, (size_t)imageSize);
    vkUnmapMemory(device, stagingMemory);

    // =========================
    // Create GPU image
    // =========================
    VkImageCreateInfo imageInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.extent = { (uint32_t)width, (uint32_t)height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkCreateImage(device, &imageInfo, nullptr, &mat.image);

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device, mat.image, &memReq);

    VkMemoryAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex =
        FindMemoryType(phys, memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(device, &allocInfo, nullptr, &mat.memory);
    vkBindImageMemory(device, mat.image, mat.memory, 0);

    // =========================
    // Transition + Copy
    // =========================
    SubmitImmediate(device, cmdPool, vkQueue , [&](VkCommandBuffer cmd)
        {
            VkImageMemoryBarrier barrier{
                VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };

            barrier.image = mat.image;
            barrier.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout =
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask =
                VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr,
                1, &barrier);

            VkBufferImageCopy region{};
            region.imageSubresource.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.layerCount = 1;
            region.imageExtent =
            { (uint32_t)width, (uint32_t)height, 1 };

            vkCmdCopyBufferToImage(
                cmd,
                stagingBuffer,
                mat.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region);

            barrier.oldLayout =
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout =
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask =
                VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr,
                1, &barrier);
        });

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // =========================
    //  Create Image View
    // =========================
    VkImageViewCreateInfo viewInfo{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = mat.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(
        device,
        &viewInfo,
        nullptr,
        &mat.imageView);

    // =========================
    // Create Sampler
    // =========================
    VkSamplerCreateInfo samplerInfo{
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.maxAnisotropy = 1.0f;

    vkCreateSampler(
        device,
        &samplerInfo,
        nullptr,
        &mat.sampler);

    // =========================
    // Allocate Descriptor
    // =========================
    VkDescriptorSetAllocateInfo dsAlloc{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    dsAlloc.descriptorPool = m_descriptorPool;
    dsAlloc.descriptorSetCount = 1;
    dsAlloc.pSetLayouts = &m_materialSetLayout;

    vkAllocateDescriptorSets(
        device,
        &dsAlloc,
        &mat.descriptorSet);

    VkDescriptorImageInfo imageInfoDesc{};
    imageInfoDesc.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfoDesc.imageView = mat.imageView;
    imageInfoDesc.sampler = mat.sampler;

    VkWriteDescriptorSet write{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = mat.descriptorSet;
    write.dstBinding = 0;
    write.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfoDesc;

    vkUpdateDescriptorSets(
        device,
        1,
        &write,
        0,
        nullptr);

    // =========================
    // Store & Return Handle
    // =========================
    m_materials.push_back(mat);

    return static_cast<uint32_t>(m_materials.size() - 1);
}


uint32_t MaterialManager::CreateDefaultMaterial()
{
    // 1x1 white pixel (RGBA)
    static const unsigned char whitePixel[4] = {
        255, 255, 255, 255
    };

    return CreateMaterialFromMemory(
        whitePixel,
        1,
        1);
}