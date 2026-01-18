// Renderer.cpp
#include <iostream>
#include <array>
#include <fstream>
#include <vector>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "Renderer.h"
#include "VulkanContext.h"
#include "UniformBufferObject.h"

using namespace vkapp;

// Simple triangle data
//static const std::vector<Vertex> TRI_VERTS =
//{
//    // positions (x,y),    color (r,g,b)
//    { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } }, // bottom center (red)
//    { { 0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } }, // right (green)
//    { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } }, // left (blue)
//};

static const std::vector<Vertex> CUBE_VERTS = {
    // Front
    {{-0.5f,-0.5f, 0.5f},{1,1,1},{0,0}},
    {{ 0.5f,-0.5f, 0.5f},{1,1,1},{1,0}},
    {{ 0.5f, 0.5f, 0.5f},{1,1,1},{1,1}},
    {{-0.5f, 0.5f, 0.5f},{1,1,1},{0,1}},
    // Back
    {{ 0.5f,-0.5f,-0.5f},{1,1,1},{0,0}},
    {{-0.5f,-0.5f,-0.5f},{1,1,1},{1,0}},
    {{-0.5f, 0.5f,-0.5f},{1,1,1},{1,1}},
    {{ 0.5f, 0.5f,-0.5f},{1,1,1},{0,1}},
    // Left
    {{-0.5f,-0.5f,-0.5f},{1,1,1},{0,0}},
    {{-0.5f,-0.5f, 0.5f},{1,1,1},{1,0}},
    {{-0.5f, 0.5f, 0.5f},{1,1,1},{1,1}},
    {{-0.5f, 0.5f,-0.5f},{1,1,1},{0,1}},
    // Right
    {{ 0.5f,-0.5f, 0.5f},{1,1,1},{0,0}},
    {{ 0.5f,-0.5f,-0.5f},{1,1,1},{1,0}},
    {{ 0.5f, 0.5f,-0.5f},{1,1,1},{1,1}},
    {{ 0.5f, 0.5f, 0.5f},{1,1,1},{0,1}},
    // Top
    {{-0.5f, 0.5f, 0.5f},{1,1,1},{0,0}},
    {{ 0.5f, 0.5f, 0.5f},{1,1,1},{1,0}},
    {{ 0.5f, 0.5f,-0.5f},{1,1,1},{1,1}},
    {{-0.5f, 0.5f,-0.5f},{1,1,1},{0,1}},
    // Bottom
    {{-0.5f,-0.5f,-0.5f},{1,1,1},{0,0}},
    {{ 0.5f,-0.5f,-0.5f},{1,1,1},{1,0}},
    {{ 0.5f,-0.5f, 0.5f},{1,1,1},{1,1}},
    {{-0.5f,-0.5f, 0.5f},{1,1,1},{0,1}},
};

static const std::vector<uint16_t> CUBE_INDICES = {
    0,1,2, 2,3,0,
    4,5,6, 6,7,4,
    8,9,10, 10,11,8,
    12,13,14, 14,15,12,
    16,17,18, 18,19,16,
    20,21,22, 22,23,20
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


Renderer::Renderer() = default;
Renderer::~Renderer() { Cleanup(); }

/////////////////// 
VkFormat getSupportedDepthFormat(VkPhysicalDevice physicalDevice)
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

// Create buffer and allocate memory (host or device local depending on properties requested)
// Create buffer will only creates object kind of structure with usage and other details.
//It should be backed by device memory through bind call.
/*
* vkCreateBuffer - creates a buffer object (resource description + usage), no memory
    ↓
vkGetBufferMemoryRequirements
    ↓
vkAllocateMemory - Allocate host visible, device only memory.
    ↓
vkBindBufferMemory - Bind buffer and device allocated memory.

*/
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
static bool SubmitImmediate(VulkanContext* ctx, std::function<void(VkCommandBuffer)> recordFunc)
{
    VkDevice device = ctx->Device();
    VkCommandPool pool = ctx->CommandPool();
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

    vkResult = vkQueueSubmit(ctx->GraphicsQueue(), 1, &submitInfo, fence);
    if (vkResult != VK_SUCCESS)
        return false;

    vkResult  = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    if (vkResult != VK_SUCCESS)
        return false;

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);

    return true;
}

bool Renderer::Init(VulkanContext* context, Scene* scene)
{
    m_context = context;
    m_scene = scene;

    if (!CreateRenderPass()) {
        std::cerr << "Failed to create render pass\n";
        return false;
    }

    if (!CreateDepthResource())
    {
        std::cerr << "Failed to create depth resource\n";
        return false;
    }


    if (!CreateFramebuffers()) {
        std::cerr << "Failed to create framebuffers\n";
        return false;
    }


    //Descriptor init..needs before pipeline
   /* if (!CreateDescriptorResources())
    {
        std::cerr << "Failed to create descriptor resources\n";
        return false;
    }*/

    //Create texture
    if (!CreateTexture("D:\\VKRender\\VulkanRender\\rock.png", &m_texture))
    {
        std::cerr << "Failed to create texture!!!\n";
        return false;
    }

    if (!CreateGraphicsPipeline()) {
        std::cerr << "Failed to create graphics pipeline\n";
        return false;
    }

    if (!CreateVertexBuffer()) {
        std::cerr << "Failed to create vertex buffer\n";
        return false;
    }

    if (!CreateIndexBuffer()) {
        std::cerr << "Failed to create index buffer\n";
        return false;
    }

    //info
    std::cerr << "--- Renderer debug info ---\n";
    std::cerr << "swap format: " << m_context->GetSwapchainInfo().imageFormat << "\n";
    auto ext = m_context->GetSwapchainInfo().extent;
    std::cerr << "swap extent: " << ext.width << " x " << ext.height << "\n";
    std::cerr << "framebuffers: " << m_framebuffers.size() << " commandBuffers: " << (m_context->GetCommandBuffer(0) != VK_NULL_HANDLE) << "\n";
    std::cerr << "pipeline: " << (uintptr_t)m_graphicsPipeline << " pipelineLayout: " << (uintptr_t)m_pipelineLayout << "\n";
    std::cerr << "vertexBuffer: " << (uintptr_t)m_vertexBuffer << " staging: " << (uintptr_t)m_stagingBuffer << "\n";
    std::cerr << "TRI_VERTS count: " << CUBE_VERTS.size() << " vertexSize: " << sizeof(Vertex) << "\n";
    std::cerr << "offset pos: " << offsetof(Vertex, pos) << " offset color: " << offsetof(Vertex, color) << "\n";
    std::cerr << "--------------------------\n";

    std::cerr << "------------Descriptor info--------------\n";

  /*  std::cerr << "m_descriptorSetLayout = " << (uintptr_t)m_descriptorSetLayout
        << ", m_descriptorPool = " << (uintptr_t)m_descriptorPool
        << ", m_descriptorSets[0] = " << (uintptr_t)(m_descriptorSets.empty() ? 0 : m_descriptorSets[0])
        << ", m_pipelineLayout = " << (uintptr_t)m_pipelineLayout
        << ", m_graphicsPipeline = " << (uintptr_t)m_graphicsPipeline << "\n";

    std::cerr << "--------------------------\n";*/

    return true;
}

void Renderer::Cleanup()
{
    if (!m_context)
        return;

    VkDevice device = m_context->Device();

    if (device == VK_NULL_HANDLE)
        return;

    DestroyIndexBuffer();
    DestroyVertexBuffer();
    DestroyGraphicsPipelineAndLayout();
    //DestroyDescriptorResources();
    CleanupFramebuffers();

    DestroyDepthResource();

    if (m_renderPass)
    {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// create render pass 
bool Renderer::CreateRenderPass()
{
    std::vector<VkAttachmentDescription> vkAttachmentDescriptionVector;  //color and depth
    //COLOR attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_context->GetSwapchainInfo().imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;          //first attachment index
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    //add color attachment descriptipon
    vkAttachmentDescriptionVector.push_back(colorAttachment);

    //DEPTH attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = getSupportedDepthFormat(m_context->PhysicalDevice());
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;                  // second attachment index
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    //add depth attachment descriptipon
    vkAttachmentDescriptionVector.push_back(depthAttachment);


    //Create subpass
    VkSubpassDescription vkSubpassDescription{};
    vkSubpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    vkSubpassDescription.colorAttachmentCount = 1;
    vkSubpassDescription.pColorAttachments = &colorAttachmentRef;
    vkSubpassDescription.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo vkRenderPassCreateInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    vkRenderPassCreateInfo.attachmentCount = vkAttachmentDescriptionVector.size();
    vkRenderPassCreateInfo.pAttachments = vkAttachmentDescriptionVector.data();
    vkRenderPassCreateInfo.subpassCount = 1;
    vkRenderPassCreateInfo.pSubpasses = &vkSubpassDescription;
    vkRenderPassCreateInfo.dependencyCount = 1;
    vkRenderPassCreateInfo.pDependencies = &dependency;

    VkResult res = vkCreateRenderPass(m_context->Device(), &vkRenderPassCreateInfo, nullptr, &m_renderPass);
    if (res != VK_SUCCESS)
    {
        std::cerr << "vkCreateRenderPass failed: " << res << "\n";
        return false;
    }
    return true;
}

bool Renderer::CreateFramebuffers()
{
    const auto& swap = m_context->GetSwapchainInfo();
    m_framebuffers.resize(swap.imageViews.size());

    for (size_t i = 0; i < swap.imageViews.size(); ++i)
    {
        std::array<VkImageView, 2> attachments =
        {
            swap.imageViews[i],
            m_depthImageView
        };

        VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbci.renderPass = m_renderPass;
        fbci.attachmentCount = static_cast<uint32_t>(attachments.size());;
        fbci.pAttachments = attachments.data();
        fbci.width = swap.extent.width;
        fbci.height = swap.extent.height;
        fbci.layers = 1;

        VkResult res = vkCreateFramebuffer(m_context->Device(), &fbci, nullptr, &m_framebuffers[i]);
        if (res != VK_SUCCESS) {
            std::cerr << "vkCreateFramebuffer failed: " << res << "\n";
            return false;
        }
    }
    return true;
}

void Renderer::CleanupFramebuffers()
{
    VkDevice device = m_context->Device();
    for (auto fb : m_framebuffers) {
        if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    }
    m_framebuffers.clear();
}

// --- Pipeline creation ---
bool Renderer::CreateGraphicsPipeline()
{
    VkDevice device = m_context->Device();

    // Load SPIR-V modules
    auto vertCode = ReadFile("shaders/basic.vert.spv");
    auto fragCode = ReadFile("shaders/basic.frag.spv");

    std::cerr << "Vert SPV size: " << vertCode.size() << "\n";
    std::cerr << "Vert SPV size: " << fragCode.size() << "\n";

    //Create vertex shader module
    VkShaderModuleCreateInfo vkShaderModuleVertexCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    vkShaderModuleVertexCreateInfo.pNext = nullptr;
    vkShaderModuleVertexCreateInfo.flags = 0;
    vkShaderModuleVertexCreateInfo.codeSize = vertCode.size();
    vkShaderModuleVertexCreateInfo.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());

    VkShaderModule vertModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &vkShaderModuleVertexCreateInfo, nullptr, &vertModule) != VK_SUCCESS)
    {
        std::cerr << "Failed to create vertex shader module\n";
        return false;
    }

    //Fragment shader module
    VkShaderModuleCreateInfo vkShaderModuleFragmentCreateInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    vkShaderModuleFragmentCreateInfo.pNext = nullptr;
    vkShaderModuleFragmentCreateInfo.flags = 0;
    vkShaderModuleFragmentCreateInfo.codeSize = fragCode.size();
    vkShaderModuleFragmentCreateInfo.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());

    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &vkShaderModuleFragmentCreateInfo, nullptr, &fragModule) != VK_SUCCESS)
    {
        std::cerr << "Failed to create fragment shader module\n";
        vkDestroyShaderModule(device, vertModule, nullptr);
        return false;
    }


    //Just check..debug only
    if (!vertModule) std::cerr << "Vert module null\n";
    if (!fragModule) std::cerr << "Frag module null\n";



    //
    std::vector<VkPipelineShaderStageCreateInfo> vkPipelineShaderStageCreateInfoVector(2);
    //Vertex module
    vkPipelineShaderStageCreateInfoVector[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vkPipelineShaderStageCreateInfoVector[0].pNext = nullptr;
    vkPipelineShaderStageCreateInfoVector[0].flags = 0;
    vkPipelineShaderStageCreateInfoVector[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    vkPipelineShaderStageCreateInfoVector[0].module = vertModule;
    vkPipelineShaderStageCreateInfoVector[0].pName = "main";
    vkPipelineShaderStageCreateInfoVector[0].pSpecializationInfo = nullptr;

    //Fragment module
    vkPipelineShaderStageCreateInfoVector[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vkPipelineShaderStageCreateInfoVector[1].pNext = nullptr;
    vkPipelineShaderStageCreateInfoVector[1].flags = 0;
    vkPipelineShaderStageCreateInfoVector[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    vkPipelineShaderStageCreateInfoVector[1].module = fragModule;
    vkPipelineShaderStageCreateInfoVector[1].pName = "main";
    vkPipelineShaderStageCreateInfoVector[1].pSpecializationInfo = nullptr;
    
    // Vertex input
    auto bindingDesc = Vertex::getBindingDesc();
    auto attribDesc = Vertex::getAttribDesc();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attribDesc.size());
    vertexInputInfo.pVertexAttributeDescriptions = attribDesc.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport & scissor (dynamic would be nicer; keep static for simplicity)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)m_context->GetSwapchainInfo().extent.width;
    viewport.height = (float)m_context->GetSwapchainInfo().extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = { 0,0 };
    scissor.extent = m_context->GetSwapchainInfo().extent;

    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    raster.depthClampEnable = VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE; // VK_CULL_MODE_BACK_BIT
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.depthBiasEnable = VK_FALSE;

    // Multisample (disabled)
    VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.sampleShadingEnable = VK_FALSE;

    // Color blend
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlendingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlendingCreateInfo.logicOpEnable = VK_FALSE;
    colorBlendingCreateInfo.attachmentCount = 1;
    colorBlendingCreateInfo.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencilCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencilCreateInfo.depthTestEnable = VK_TRUE;
    depthStencilCreateInfo.depthWriteEnable = VK_TRUE;
    depthStencilCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencilCreateInfo.depthBoundsTestEnable = VK_FALSE;
    depthStencilCreateInfo.stencilTestEnable = VK_FALSE;

    // Create Pipeline layout
    if (!CreateGraphicsPipelineLayout())
    {
        std::cerr << "Failed to create pipeline layout\n";
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo vkGraphicsPipelineCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    vkGraphicsPipelineCreateInfo.stageCount = static_cast<uint32_t>(vkPipelineShaderStageCreateInfoVector.size());
    vkGraphicsPipelineCreateInfo.pStages = vkPipelineShaderStageCreateInfoVector.data();
    vkGraphicsPipelineCreateInfo.pVertexInputState = &vertexInputInfo;
    vkGraphicsPipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    vkGraphicsPipelineCreateInfo.pViewportState = &viewportState;
    vkGraphicsPipelineCreateInfo.pRasterizationState = &raster;
    vkGraphicsPipelineCreateInfo.pMultisampleState = &multisample;
    vkGraphicsPipelineCreateInfo.pColorBlendState = &colorBlendingCreateInfo;       //color
    vkGraphicsPipelineCreateInfo.pDepthStencilState = &depthStencilCreateInfo;      //depth
    vkGraphicsPipelineCreateInfo.layout = m_pipelineLayout;
    vkGraphicsPipelineCreateInfo.renderPass = m_renderPass;
    vkGraphicsPipelineCreateInfo.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(m_context->Device(), VK_NULL_HANDLE, 1, &vkGraphicsPipelineCreateInfo, nullptr, &m_graphicsPipeline);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateGraphicsPipelines failed: " << res << "\n";
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        return false;
    }

    // Destroy shader modules after pipeline is created
    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
    return true;
}

void Renderer::DestroyGraphicsPipelineAndLayout()
{
    if (!m_context)
        return;

    VkDevice device = m_context->Device();
    if (m_graphicsPipeline) { vkDestroyPipeline(device, m_graphicsPipeline, nullptr); m_graphicsPipeline = VK_NULL_HANDLE; }
    if (m_pipelineLayout) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
}

//Graphics pipeline layout
bool Renderer::CreateGraphicsPipelineLayout()
{
    VkResult vkResult = VK_SUCCESS;

    const std::vector<VkDescriptorSetLayout> vkDescriptorSetLayoutArray =
    {
        m_descriptorSetLayoutFrame,
        m_descriptorSetLayoutMaterial
    };

    VkPipelineLayoutCreateInfo vkPipelineLayoutCreateInfo{};
    vkPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    vkPipelineLayoutCreateInfo.pNext = nullptr;
    vkPipelineLayoutCreateInfo.flags = 0;
    vkPipelineLayoutCreateInfo.setLayoutCount = vkDescriptorSetLayoutArray.size();
    vkPipelineLayoutCreateInfo.pSetLayouts = vkDescriptorSetLayoutArray.data();
    vkPipelineLayoutCreateInfo.pushConstantRangeCount = 0;
    vkPipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

    vkResult = vkCreatePipelineLayout(m_context->Device(), &vkPipelineLayoutCreateInfo, nullptr, &m_pipelineLayout);
    if (vkResult != VK_SUCCESS)
    {
        std::cerr << "vkCreateGraphicsPipelineLayout failed: " << vkResult << "\n";
        m_pipelineLayout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

// Descriptor set layouts define the interface between our application and the shader
// Basically connects the different shader stages to descriptors for binding uniform buffers, image samplers, etc.
// So every shader binding should map to one descriptor set layout binding
bool Renderer::CreateDescriptorResourcesFrame()
{
    size_t swapchainImageCount = static_cast<uint32_t>(VulkanContext::DEFAULT_SWAPCHAIN_IMAGE_COUNT);

    //Create descriptor set layout
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;                           //Binding in shader "layout(binding=0) uniform mvpMatrix"
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo vkDescriptorSetLayoutCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    vkDescriptorSetLayoutCreateInfo.pNext = nullptr;
    vkDescriptorSetLayoutCreateInfo.bindingCount = 1;
    vkDescriptorSetLayoutCreateInfo.flags = 0;
    vkDescriptorSetLayoutCreateInfo.pBindings = &uboLayoutBinding;          //////pBindings is array of struct VkDescriptorSetLayoutBinding


    if (vkCreateDescriptorSetLayout(m_context->Device(), &vkDescriptorSetLayoutCreateInfo, nullptr, &m_descriptorSetLayoutFrame) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor set layout");

    //Create uniform buffers
    m_uniformBuffers.resize(swapchainImageCount);
    m_uniformBuffersMemory.resize(swapchainImageCount);

    for (size_t i = 0; i < m_uniformBuffers.size(); i++)
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);
        CreateBufferRaw(
            m_context->Device(), m_context->PhysicalDevice(), bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_uniformBuffers[i], m_uniformBuffersMemory[i]
        );
    }

    // Descriptor pool
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = swapchainImageCount;

    VkDescriptorPoolCreateInfo vkDescriptorPoolCreateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    vkDescriptorPoolCreateInfo.poolSizeCount = 1;       ////numof above struct count i.e poolsize
    vkDescriptorPoolCreateInfo.pPoolSizes = &poolSize;
   vkDescriptorPoolCreateInfo.maxSets = swapchainImageCount;

    if (vkCreateDescriptorPool(m_context->Device(), &vkDescriptorPoolCreateInfo, nullptr, &m_descriptorPoolFrame) != VK_SUCCESS)
    {
        std::cerr << "Failed to create descriptor pool\n";
        return false;
    }

    // Allocate sets
    std::vector<VkDescriptorSetLayout> layouts(VulkanContext::DEFAULT_SWAPCHAIN_IMAGE_COUNT, m_descriptorSetLayoutFrame);

    VkDescriptorSetAllocateInfo vkDescriptorSetAllocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    vkDescriptorSetAllocateInfo.descriptorPool = m_descriptorPoolFrame;
    vkDescriptorSetAllocateInfo.descriptorSetCount = swapchainImageCount;
    vkDescriptorSetAllocateInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(swapchainImageCount);
    if (vkAllocateDescriptorSets(m_context->Device(), &vkDescriptorSetAllocateInfo, m_descriptorSets.data()) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate descriptor sets\n";
        return false;
    }

    //Update above descriptor set directly to the shader
    //We have two options, either write or copy. Same uniform but different shader then use copy.

    // Update descriptor sets
    for (size_t i = 0; i < swapchainImageCount; ++i)
    {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_context->Device(), 1, &descriptorWrite, 0, nullptr);
        std::cerr << "Descriptor sets updated!!!\n";
    }

    return true;
}

bool Renderer::CreateDepthResource()
{
    VkDevice device = m_context->Device();
    VkPhysicalDevice phys = m_context->PhysicalDevice();
    VkFormat depthFormat = getSupportedDepthFormat(phys);
    VkExtent2D extent = m_context->GetSwapchainInfo().extent;

    // create image
    VkImageCreateInfo vkImageCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    vkImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    vkImageCreateInfo.format = depthFormat;
    vkImageCreateInfo.extent = { extent.width, extent.height, 1 };
    vkImageCreateInfo.mipLevels = 1;
    vkImageCreateInfo.arrayLayers = 1;
    vkImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    vkImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    vkImageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    vkImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &vkImageCreateInfo, nullptr, &m_depthImage) != VK_SUCCESS)
    {
        std::cerr << "Failed to create depth image\n";
        return false;
    }

    VkMemoryRequirements vkMemoryRequirements;
    vkGetImageMemoryRequirements(device, m_depthImage, &vkMemoryRequirements);

    VkMemoryAllocateInfo vkMemoryAllocateInfo{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    vkMemoryAllocateInfo.allocationSize = vkMemoryRequirements.size;
    vkMemoryAllocateInfo.memoryTypeIndex = FindMemoryType(vkMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &vkMemoryAllocateInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate depth image memory\n";
        return false;
    }

    if (vkBindImageMemory(device, m_depthImage, m_depthImageMemory, 0) != VK_SUCCESS)
    {
        std::cerr << "Failed to bind depth image memory\n";
        return false;
    }

    // create image view
    VkImageViewCreateInfo vkImageViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vkImageViewCreateInfo.image = m_depthImage;
    vkImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vkImageViewCreateInfo.format = depthFormat;
    //aspectMask:--whch part of Image or whole of the Image is going to be affected by image barrier.
    //https://registry.khronos.org/vulkan/specs/latest/man/html/VkImageSubresourceRange.html
    vkImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    vkImageViewCreateInfo.subresourceRange.levelCount = 1;
    vkImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
    vkImageViewCreateInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &vkImageViewCreateInfo, nullptr, &m_depthImageView) != VK_SUCCESS)
    {
        std::cerr << "Failed to create depth image view\n";
        return false;
    }

    // Transition depth image to DEPTH_ATTACHMENT_OPTIMAL
    SubmitImmediate(m_context, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier vkImgageMemBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        vkImgageMemBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkImgageMemBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        vkImgageMemBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkImgageMemBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vkImgageMemBarrier.image = m_depthImage;
        vkImgageMemBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        vkImgageMemBarrier.subresourceRange.baseMipLevel = 0;
        vkImgageMemBarrier.subresourceRange.levelCount = 1;
        vkImgageMemBarrier.subresourceRange.baseArrayLayer = 0;
        vkImgageMemBarrier.subresourceRange.layerCount = 1;
        vkImgageMemBarrier.srcAccessMask = 0;
        vkImgageMemBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &vkImgageMemBarrier
        );
        });

    return true;
}


uint32_t Renderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->PhysicalDevice(), &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

bool Renderer::CreateVertexBuffer()
{
    VkDevice device = m_context->Device();
    VkPhysicalDevice phys = m_context->PhysicalDevice();

    VkDeviceSize bufferSize = sizeof(Vertex) * CUBE_VERTS.size();

    // Create staging buffer (host visible)
    CreateBufferRaw(device, phys, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_stagingBuffer, m_stagingBufferMemory);

    // Map and copy vertex data
    void* data;
    vkMapMemory(device, m_stagingBufferMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, CUBE_VERTS.data(), (size_t)bufferSize);
    vkUnmapMemory(device, m_stagingBufferMemory);

    // Create device local vertex buffer (destination)
    CreateBufferRaw(device, phys, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vertexBuffer, m_vertexBufferMemory);

    // Copy staging -> device local
    SubmitImmediate(m_context, [&](VkCommandBuffer cmd)
        {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = bufferSize;
        vkCmdCopyBuffer(cmd, m_stagingBuffer, m_vertexBuffer, 1, &copyRegion);
        });


    // Now staging buffer can be destroyed (we keep handles so DestroyVertexBuffer can free them)
    return true;
}

void Renderer::DestroyVertexBuffer()
{
    if (!m_context) return;
    VkDevice device = m_context->Device();
    if (m_stagingBuffer) {
        vkDestroyBuffer(device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }
    if (m_stagingBufferMemory) {
        vkFreeMemory(device, m_stagingBufferMemory, nullptr);
        m_stagingBufferMemory = VK_NULL_HANDLE;
    }
    if (m_vertexBuffer) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
    }
    if (m_vertexBufferMemory) {
        vkFreeMemory(device, m_vertexBufferMemory, nullptr);
        m_vertexBufferMemory = VK_NULL_HANDLE;
    }
}

bool Renderer::CreateIndexBuffer()
{
    VkDevice device = m_context->Device();
    VkPhysicalDevice phys = m_context->PhysicalDevice();

    VkDeviceSize bufferSize = sizeof(uint16_t) * CUBE_INDICES.size();

    VkBuffer indexStageBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexStageBufferDeviceMemory = VK_NULL_HANDLE;

    // Create staging buffer (host visible)
    CreateBufferRaw(device, phys, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        indexStageBuffer, indexStageBufferDeviceMemory);

    // Map and copy index buffer data
    void* data;
    vkMapMemory(device, indexStageBufferDeviceMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, CUBE_INDICES.data(), (size_t)bufferSize);
    vkUnmapMemory(device, indexStageBufferDeviceMemory);

    // Create device local index buffer (destination)
    CreateBufferRaw(device, phys, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,    //should be index buff bit
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_indexBuffer, m_indexBufferMemory);

    // Copy staging -> device local
    SubmitImmediate(m_context, [&](VkCommandBuffer cmd)
        {
            VkBufferCopy copyRegion{};
            //copyRegion.srcOffset = 0;
            //copyRegion.dstOffset = 0;
            copyRegion.size = bufferSize;
            vkCmdCopyBuffer(cmd, indexStageBuffer, m_indexBuffer, 1, &copyRegion);
        });

      //Free local mem
    if (indexStageBuffer)
        vkDestroyBuffer(device, indexStageBuffer, nullptr);

    if (indexStageBufferDeviceMemory)
        vkFreeMemory(device, indexStageBufferDeviceMemory, nullptr);

    return true;
}

void Renderer::DestroyIndexBuffer()
{
    if (!m_context)
        return;

    VkDevice device = m_context->Device();
   /* if (m_stagingBuffer)
    {
        vkDestroyBuffer(device, m_stagingBuffer, nullptr);
        m_stagingBuffer = VK_NULL_HANDLE;
    }

    if (m_stagingBufferMemory)
    {
        vkFreeMemory(device, m_stagingBufferMemory, nullptr);
        m_stagingBufferMemory = VK_NULL_HANDLE;
    }*/

    if (m_indexBuffer)
    {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
    }

    if (m_indexBufferMemory) 
    {
        vkFreeMemory(device, m_indexBufferMemory, nullptr);
        m_indexBufferMemory = VK_NULL_HANDLE;
    }
}

void Renderer::DestroyDescriptorResourcesFrame()
{
    if (!m_context)
        return;

    VkDevice device = m_context->Device();

    //delete buffer memory
    for (auto buffer : m_uniformBuffers)
    {
        if (buffer) 
            vkDestroyBuffer(device, buffer, nullptr);
    }

    //delete device buffer mem
    for (auto mem : m_uniformBuffersMemory)
    {
        if (mem)
            vkFreeMemory(device, mem, nullptr);
    }

    m_uniformBuffers.clear();
    m_uniformBuffersMemory.clear();

    if (m_descriptorPoolFrame)
    { 
        vkDestroyDescriptorPool(device, m_descriptorPoolFrame, nullptr); m_descriptorPoolFrame = VK_NULL_HANDLE;
    }

    if (m_descriptorSetLayoutFrame)
    { 
        vkDestroyDescriptorSetLayout(device, m_descriptorSetLayoutFrame, nullptr); m_descriptorSetLayoutFrame = VK_NULL_HANDLE;
    }
}

void Renderer::DestroyDepthResource()
{
    if (!m_context)
        return;

    VkDevice vkDevice = m_context->Device();

    if (m_depthImageView)
        vkDestroyImageView(vkDevice, m_depthImageView, nullptr);

    if (m_depthImage)
        vkDestroyImage(vkDevice, m_depthImage, nullptr);

    if (m_depthImageMemory)
        vkFreeMemory(vkDevice, m_depthImageMemory, nullptr);
}

bool Renderer::CreateTexture(const char* path, Texture* texture)
{
    int imageWidth, imageHeight, texC;
    stbi_uc* pixels = stbi_load(path, &imageWidth, &imageHeight, &texC, STBI_rgb_alpha);
    if (!pixels)
        return false;

    texture->width = imageWidth;
    texture->height = imageHeight;

    const VkDevice vkDevice = m_context->Device();
    const VkPhysicalDevice physicalDevice = m_context->PhysicalDevice();

    VkDeviceSize imageSize = imageWidth * imageHeight * 4;
    VkBuffer vkBufferStaging;
    VkDeviceMemory vkDeviceMemStaging;

    CreateBufferRaw(vkDevice, physicalDevice, imageSize,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    vkBufferStaging,
                    vkDeviceMemStaging);

    void* data;
    vkMapMemory(vkDevice, vkDeviceMemStaging, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(vkDevice, vkDeviceMemStaging);

    stbi_image_free(pixels);

    //create image
    VkImageCreateInfo vkImageCreateInfo{};
    vkImageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    vkImageCreateInfo.pNext = NULL;
    vkImageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    vkImageCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    vkImageCreateInfo.extent = { (uint32_t)imageWidth, (uint32_t)imageHeight, 1 };
    vkImageCreateInfo.mipLevels = 1;
    vkImageCreateInfo.arrayLayers = 1;
    vkImageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    vkImageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    vkImageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkImageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    vkImageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkResult vResult = vkCreateImage(vkDevice, &vkImageCreateInfo, nullptr, &texture->vkImage);
    if (vResult != VK_SUCCESS)
    {
        std::cerr << "Failed to create VkImage\n";
        return false;
    }

    //Create device memory for texture 
    VkMemoryRequirements vkMemoryRequirements;
    vkGetImageMemoryRequirements(vkDevice, texture->vkImage, &vkMemoryRequirements);

    VkMemoryAllocateInfo vkMemoryAllocateInfo{};
    vkMemoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    vkMemoryAllocateInfo.pNext = nullptr;
    vkMemoryAllocateInfo.allocationSize = vkMemoryRequirements.size;
    vkMemoryAllocateInfo.memoryTypeIndex = FindMemoryType(vkMemoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vResult = vkAllocateMemory(vkDevice, &vkMemoryAllocateInfo, nullptr, &texture->vkDeviceMemory) ;
    if (vResult != VK_SUCCESS)
    {
        std::cerr << "Failed to allocate image memory!!\n";
        return false;
    }

    vkBindImageMemory(vkDevice, texture->vkImage, texture->vkDeviceMemory, 0);

    bool bCmdSuccess = SubmitImmediate(m_context, [&](VkCommandBuffer cmd)
    {
            VkImageMemoryBarrier vkImageMemoryBarrier{};
            vkImageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            vkImageMemoryBarrier.pNext = nullptr;
            vkImageMemoryBarrier.image = texture->vkImage;
            vkImageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vkImageMemoryBarrier.subresourceRange.levelCount = 1;
            vkImageMemoryBarrier.subresourceRange.layerCount = 1;
            vkImageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
            vkImageMemoryBarrier.subresourceRange.baseMipLevel = 0;
            vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkImageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkImageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            vkCmdPipelineBarrier(cmd, 
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0,
                                 0,
                                 NULL,
                                 0,
                                 NULL,
                                 1,
                                 &vkImageMemoryBarrier);

            VkBufferImageCopy vkBufferImageCopy{};
            vkBufferImageCopy.bufferOffset = 0;
            vkBufferImageCopy.bufferRowLength = 0;
            vkBufferImageCopy.bufferImageHeight = 0;
            vkBufferImageCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vkBufferImageCopy.imageSubresource.mipLevel = 0;
            vkBufferImageCopy.imageSubresource.baseArrayLayer = 0;
            vkBufferImageCopy.imageSubresource.layerCount = 1;
            vkBufferImageCopy.imageOffset.x = 0;
            vkBufferImageCopy.imageOffset.y = 0;
            vkBufferImageCopy.imageOffset.z = 0;
            vkBufferImageCopy.imageExtent.width = imageWidth;
            vkBufferImageCopy.imageExtent.height = imageHeight;
            vkBufferImageCopy.imageExtent.depth = 1;


            ///COPY BUFFER TO IMAGE
            vkCmdCopyBufferToImage(
                cmd, 
                vkBufferStaging, 
                texture->vkImage,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &vkBufferImageCopy);


            ///
            vkImageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            vkImageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            vkImageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkImageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0,  nullptr, 0 , nullptr, 1, &vkImageMemoryBarrier);
                
    });

    if (!bCmdSuccess)
    {
        std::cerr << "Failed to submit command buffer!!\n";
        return false;
    }

    vkDestroyBuffer(vkDevice, vkBufferStaging, nullptr);
    vkFreeMemory(vkDevice, vkDeviceMemStaging, nullptr);

    //Create image view
    VkImageViewCreateInfo vkImageViewCreateInfo{};
    vkImageViewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vkImageViewCreateInfo.pNext = nullptr;
    vkImageViewCreateInfo.image = texture->vkImage;
    vkImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vkImageViewCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    vkImageViewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
    vkImageViewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
    vkImageViewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
    vkImageViewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
    vkImageViewCreateInfo.flags = 0;
    vkImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkImageViewCreateInfo.subresourceRange.layerCount = 1;
    vkImageViewCreateInfo.subresourceRange.levelCount = 1;
    vkImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
    vkImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;

    vResult = vkCreateImageView(vkDevice, &vkImageViewCreateInfo, nullptr, &texture->vkImageView);
    if (vResult != VK_SUCCESS)
    {
        std::cerr << "Failed to create image view!!\n";
        return false;
    }

    //Create sampler
    VkSamplerCreateInfo vkSamplerCreateInfo{};
    vkSamplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    vkSamplerCreateInfo.pNext = nullptr;
    vkSamplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    vkSamplerCreateInfo.minFilter = VK_FILTER_LINEAR;
    vkSamplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    vkSamplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkSamplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkSamplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    vkSamplerCreateInfo.anisotropyEnable = VK_FALSE;
    vkSamplerCreateInfo.maxAnisotropy = 16;
    vkSamplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    vkSamplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
    vkSamplerCreateInfo.compareEnable = VK_FALSE;
    vkSamplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    vResult = vkCreateSampler(vkDevice, &vkSamplerCreateInfo, NULL, &texture->vkSampler);
    if (vResult != VK_SUCCESS)
    {
        std::cerr << "Failed to create sampler!!\n";
        return false;
    }

    return true;
}

// --- Frame lifecycle ---
bool Renderer::BeginFrame()
{
    if (!m_context) return false;

    VkDevice device = m_context->Device();
    const size_t frameIndex = m_currentFrame % VulkanContext::DEFAULT_SWAPCHAIN_IMAGE_COUNT;

    // Wait until the previous frame using this slot is finished
    VkFence fence = m_context->GetFence(frameIndex);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    // Acquire next image
    VkSemaphore imageAvailable = m_context->GetImageAvailableSemaphore(frameIndex);
    VkResult res = m_context->AcquireNextImage(&m_imageIndex, imageAvailable);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        OnResize(-1, -1);
        return false;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        std::cerr << "AcquireNextImageKHR failed: " << res << "\n";
        return false;
    }

    // Reset the command buffer for this frame so we can record new commands
    VkCommandBuffer cmd = m_context->GetCommandBuffer(frameIndex);
    if (cmd == VK_NULL_HANDLE) {
        std::cerr << "No command buffer for frame\n";
        return false;
    }

    if (vkResetCommandBuffer(cmd, 0) != VK_SUCCESS)
    {
        std::cerr << "No command buffer reset failed\n";
        return false;
    }

    m_frameStarted = true;
    return true;
}

//void Renderer::Render(Scene& /*scene*/, double /*dt*/)
void Renderer::Render( double /*dt*/)
{
    if (!m_frameStarted || !m_context) return;

    const size_t frameIndex = m_currentFrame % VulkanContext::DEFAULT_SWAPCHAIN_IMAGE_COUNT;

    //Update UBO
    UniformBufferObject ubo{};
    // model: a simple rotation over time (optional). If you don't have time source, use identity.
    float angle = static_cast<float>((m_currentFrame % 360) * 0.1f * 3.14159f / 180.0f);
    ubo.model = glm::rotate(glm::mat4(1.0f), 10.0f, glm::vec3(0, 1, 0));

    if (false) {    //scene
       
       // ubo.view = scene->mainCamera.GetView();
       // ubo.proj = scene->mainCamera.GetProjection();
    }
    else 
    {
        //position, target and up vector
        glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 2.0f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                          (float)m_context->GetSwapchainInfo().extent.width / (float)m_context->GetSwapchainInfo().extent.height,
                                           0.1f, 
                                            100.0f);
        proj[1][1] *= -1.0f;
        ubo.view = view;
        ubo.proj = proj;
    }

  /*  ubo.model = glm::mat4(1.0f);
    ubo.view = glm::mat4(1.0f);
    ubo.proj = glm::mat4(1.0f);*/


    // write to the per-frame uniform buffer (host-coherent)
    void* data;
    vkMapMemory(m_context->Device(), m_uniformBuffersMemory[frameIndex], 0, sizeof(ubo), 0, &data);
    std::memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(m_context->Device(), m_uniformBuffersMemory[frameIndex]);


    VkCommandBuffer cmd = m_context->GetCommandBuffer(frameIndex);

    // Begin command buffer
    VkCommandBufferBeginInfo vkCommandBufferBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    vkCommandBufferBeginInfo.flags = 0;
    VkResult res = vkBeginCommandBuffer(cmd, &vkCommandBufferBeginInfo);
    if (res != VK_SUCCESS) {
        std::cerr << "vkBeginCommandBuffer failed: " << res << "\n";
        return;
    }

    // Begin render pass with a clear color
    const auto& swap = m_context->GetSwapchainInfo();

    vkClearColorValue.float32[0] = 0.0f;
    vkClearColorValue.float32[1] = 0.0f;
    vkClearColorValue.float32[2] = 1.0f;
    vkClearColorValue.float32[3] = 1.0f;

    vkClearDepthStencilValue.depth = 1.0f;	//float
    vkClearDepthStencilValue.stencil = 0;	//uint32_t


    std::vector<VkClearValue> vkClearValueVector(2);

    vkClearValueVector[0].color = vkClearColorValue;
    vkClearValueVector[1].depthStencil = vkClearDepthStencilValue;


    VkRenderPassBeginInfo vkRenderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    vkRenderPassBeginInfo.renderPass = m_renderPass;
    vkRenderPassBeginInfo.framebuffer = m_framebuffers[m_imageIndex];
    vkRenderPassBeginInfo.renderArea.offset = { 0, 0 };
    vkRenderPassBeginInfo.renderArea.extent = swap.extent;
    vkRenderPassBeginInfo.clearValueCount = 2;
    vkRenderPassBeginInfo.pClearValues = vkClearValueVector.data();

    vkCmdBeginRenderPass(cmd, &vkRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    //Draw here....
    {
        // Bind pipeline + descriptor set + vertex/index buffers + draw indexed
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

        // bind the descriptor set that was allocated using m_descriptorSetLayout
        vkCmdBindDescriptorSets(cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_pipelineLayout,    
            0, 1, &m_descriptorSets[frameIndex],
            0, nullptr);

        VkBuffer vertexBuffers[] = { m_vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);


        vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT16);
        //vkCmdDraw(cmd, static_cast<uint32_t>(TRI_VERTS.size()), 1, 0, 0);
        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(CUBE_INDICES.size()), 1, 0, 0, 0);

    }


    vkCmdEndRenderPass(cmd);

    res = vkEndCommandBuffer(cmd);
    if (res != VK_SUCCESS) {
        std::cerr << "vkEndCommandBuffer failed: " << res << "\n";
        return;
    }
}

void Renderer::EndFrame()
{
    if (!m_frameStarted || !m_context)
        return;

    const size_t frameIndex = m_currentFrame % VulkanContext::DEFAULT_SWAPCHAIN_IMAGE_COUNT;
    VkCommandBuffer cmd = m_context->GetCommandBuffer(frameIndex);
    VkSemaphore waitSem = m_context->GetImageAvailableSemaphore(frameIndex);
    VkSemaphore signalSem = m_context->GetRenderFinishedSemaphore(frameIndex);
    VkFence fence = m_context->GetFence(frameIndex);

    // Submit the command buffer
    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    VkSemaphore waitSemaphores[] = { waitSem };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSem;

    VkResult res = vkQueueSubmit(m_context->GraphicsQueue(), 1, &submitInfo, fence);
    if (res != VK_SUCCESS) {
        std::cerr << "vkQueueSubmit failed: " << res << "\n";
    }

    // Present
    res = m_context->QueuePresent(m_imageIndex, signalSem);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR)
    {
        OnResize(-1, -1);
    }
    else if (res != VK_SUCCESS) {
        std::cerr << "QueuePresent failed: " << res << "\n";
    }

    m_currentFrame++;
    m_frameStarted = false;
}

void Renderer::OnResize(int width, int height)
{
    if (!m_context) return;

    m_resized = true;
    vkDeviceWaitIdle(m_context->Device());

    //First clean up
    CleanupFramebuffers();
    DestroyDepthResource();

    //Second recreate
    if (!m_context->RecreateSwapchain(width, height))
    {
        std::cerr << "Failed to recreate swapchain during resize\n";
        return;
    }

    if (!CreateDepthResource())
    {
        std::cerr << "Failed to recreate Depth resouces after resize\n";
    }

    if (!CreateFramebuffers())
    {
        std::cerr << "Failed to recreate framebuffers after resize\n";
    }
}

