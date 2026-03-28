#pragma once
#include <iostream>
#include <array>
#include <fstream>
#include <vector>
#include <cstring>

#include <vulkan/vulkan.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <functional>

struct Material
{
    VkImage        image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    imageView = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

static std::vector<char> ReadFile(const std::string& filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
    size_t size = (size_t)file.tellg();
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    file.close();
    return buffer;
}


static void CreateBufferRaw(VkDevice device, VkPhysicalDevice phys, VkDeviceSize size,
    VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
    VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkBufferCreateInfo vkBufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    vkBufferCreateInfo.size = size;
    vkBufferCreateInfo.usage = usage;
    vkBufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &vkBufferCreateInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements vkMemoryRequirements;
    vkGetBufferMemoryRequirements(device, outBuffer, &vkMemoryRequirements);

    VkMemoryAllocateInfo vkMemoryAllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    vkMemoryAllocateInfo.allocationSize = vkMemoryRequirements.size;

    // find memory type
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    uint32_t memTypeIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((vkMemoryRequirements.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties)
        {
            memTypeIdx = i;
            break;
        }
    }
    if (memTypeIdx == UINT32_MAX) throw std::runtime_error("failed to find memory type for buffer");

    vkMemoryAllocateInfo.memoryTypeIndex = memTypeIdx;
    if (vkAllocateMemory(device, &vkMemoryAllocateInfo, nullptr, &outMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, outBuffer, outMemory, 0);
}


// One-time command submit helper (copy, transitions)
static bool SubmitImmediate(VkDevice device, VkCommandPool pool, VkQueue queue,
    std::function<void(VkCommandBuffer)> recordFunc)
{
    VkResult vkResult = VK_SUCCESS;

    //Create command buffer
    VkCommandBufferAllocateInfo vkCommandBufferAllocateInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    vkCommandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkCommandBufferAllocateInfo.commandPool = pool;
    vkCommandBufferAllocateInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkResult = vkAllocateCommandBuffers(device, &vkCommandBufferAllocateInfo, &cmd);
    if (vkResult != VK_SUCCESS)
        return false;

    VkCommandBufferBeginInfo vkCommandBufferBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkCommandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkResult = vkBeginCommandBuffer(cmd, &vkCommandBufferBeginInfo);
    if (vkResult != VK_SUCCESS)
        return false;

    recordFunc(cmd);

    vkResult = vkEndCommandBuffer(cmd);
    if (vkResult != VK_SUCCESS)
        return false;

    //Submit
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkResult = vkCreateFence(device, &fci, nullptr, &fence);
    if (vkResult != VK_SUCCESS)
        return false;

    vkResult = vkQueueSubmit(queue, 1, &submitInfo, fence);
    if (vkResult != VK_SUCCESS)
        return false;

    vkResult = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (vkResult != VK_SUCCESS)
        return false;

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);

    return true;
}

static VkFormat getSupportedDepthFormat(VkPhysicalDevice physicalDevice)
{
    // Since all depth formats may be optional, we need to find a suitable depth format to use
    // Start with the highest precision packed format
    std::vector<VkFormat> formatList =
    {
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D24_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM_S8_UINT,
        VK_FORMAT_D16_UNORM
    };

    for (auto& format : formatList)
    {
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
        if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            return format;
        }
    }

    throw std::runtime_error("No supported depth format found");
}


static uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}