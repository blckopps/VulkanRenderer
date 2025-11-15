// Renderer.cpp
#include "Renderer.h"
#include "VulkanContext.h"
#include <iostream>
#include <array>
#include <fstream>
#include <vector>
#include <cstring>

using namespace vkapp;

// Simple triangle data
static const std::vector<Vertex> TRI_VERTS = {
    // positions (x,y),    color (r,g,b)
    { { 0.0f, -0.5f }, { 1.0f, 0.0f, 0.0f } }, // bottom center (red)
    { { 0.5f,  0.5f }, { 0.0f, 1.0f, 0.0f } }, // right (green)
    { { -0.5f, 0.5f }, { 0.0f, 0.0f, 1.0f } }, // left (blue)
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

bool Renderer::Init(VulkanContext* context, Scene* scene)
{
    m_context = context;
    m_scene = scene;

    if (!CreateRenderPass()) {
        std::cerr << "Failed to create render pass\n";
        return false;
    }
    if (!CreateFramebuffers()) {
        std::cerr << "Failed to create framebuffers\n";
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

    //info
    std::cerr << "--- Renderer debug info ---\n";
    std::cerr << "swap format: " << m_context->GetSwapchainInfo().imageFormat << "\n";
    auto ext = m_context->GetSwapchainInfo().extent;
    std::cerr << "swap extent: " << ext.width << " x " << ext.height << "\n";
    std::cerr << "framebuffers: " << m_framebuffers.size() << " commandBuffers: " << (m_context->GetCommandBuffer(0) != VK_NULL_HANDLE) << "\n";
    std::cerr << "pipeline: " << (uintptr_t)m_graphicsPipeline << " pipelineLayout: " << (uintptr_t)m_pipelineLayout << "\n";
    std::cerr << "vertexBuffer: " << (uintptr_t)m_vertexBuffer << " staging: " << (uintptr_t)m_stagingBuffer << "\n";
    std::cerr << "TRI_VERTS count: " << TRI_VERTS.size() << " vertexSize: " << sizeof(Vertex) << "\n";
    std::cerr << "offset pos: " << offsetof(Vertex, pos) << " offset color: " << offsetof(Vertex, color) << "\n";
    std::cerr << "--------------------------\n";


    return true;
}

void Renderer::Cleanup()
{
    if (!m_context) return;
    VkDevice device = m_context->Device();
    if (device == VK_NULL_HANDLE) return;

    DestroyVertexBuffer();
    DestroyGraphicsPipeline();

    CleanupFramebuffers();

    if (m_renderPass) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// create render pass 
bool Renderer::CreateRenderPass()
{
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
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 1;
    rpci.pAttachments = &colorAttachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dependency;

    VkResult res = vkCreateRenderPass(m_context->Device(), &rpci, nullptr, &m_renderPass);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateRenderPass failed: " << res << "\n";
        return false;
    }
    return true;
}

bool Renderer::CreateFramebuffers()
{
    const auto& swap = m_context->GetSwapchainInfo();
    m_framebuffers.resize(swap.imageViews.size());

    for (size_t i = 0; i < swap.imageViews.size(); ++i) {
        VkImageView attachments[] = { swap.imageViews[i] };

        VkFramebufferCreateInfo fbci{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        fbci.renderPass = m_renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = attachments;
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

    VkShaderModuleCreateInfo vertSMCI{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    vertSMCI.codeSize = vertCode.size();
    vertSMCI.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());

    VkShaderModule vertModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &vertSMCI, nullptr, &vertModule) != VK_SUCCESS) {
        std::cerr << "Failed to create vertex shader module\n";
        return false;
    }

    VkShaderModuleCreateInfo fragSMCI{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    fragSMCI.codeSize = fragCode.size();
    fragSMCI.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());

    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &fragSMCI, nullptr, &fragModule) != VK_SUCCESS) {
        std::cerr << "Failed to create fragment shader module\n";
        vkDestroyShaderModule(device, vertModule, nullptr);
        return false;
    }


    //Just check..debug only
    if (!vertModule) std::cerr << "Vert module null\n";
    if (!fragModule) std::cerr << "Frag module null\n";




    VkPipelineShaderStageCreateInfo vertStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertStage, fragStage };

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

    VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Pipeline layout (no descriptors for now)
    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 0;
    plci.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(m_context->Device(), &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout\n";
        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
        return false;
    }

    VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pci.stageCount = 2;
    pci.pStages = shaderStages;
    pci.pVertexInputState = &vertexInputInfo;
    pci.pInputAssemblyState = &inputAssembly;
    pci.pViewportState = &viewportState;
    pci.pRasterizationState = &raster;
    pci.pMultisampleState = &multisample;
    pci.pColorBlendState = &colorBlending;
    pci.layout = m_pipelineLayout;
    pci.renderPass = m_renderPass;
    pci.subpass = 0;

    VkResult res = vkCreateGraphicsPipelines(m_context->Device(), VK_NULL_HANDLE, 1, &pci, nullptr, &m_graphicsPipeline);
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

void Renderer::DestroyGraphicsPipeline()
{
    if (!m_context)
        return;

    VkDevice device = m_context->Device();
    if (m_graphicsPipeline) { vkDestroyPipeline(device, m_graphicsPipeline, nullptr); m_graphicsPipeline = VK_NULL_HANDLE; }
    if (m_pipelineLayout) { vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
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

// Create buffer and allocate memory (host or device local depending on properties requested)
static void CreateBufferRaw(VkDevice device, VkPhysicalDevice phys, VkDeviceSize size,
    VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
    VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bci, nullptr, &outBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReq);

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = memReq.size;

    // find memory type
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(phys, &memProps);
    uint32_t memTypeIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            memTypeIdx = i;
            break;
        }
    }
    if (memTypeIdx == UINT32_MAX) throw std::runtime_error("failed to find memory type for buffer");

    mai.memoryTypeIndex = memTypeIdx;
    if (vkAllocateMemory(device, &mai, nullptr, &outMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory");
    }

    vkBindBufferMemory(device, outBuffer, outMemory, 0);
}

// One-time command submit helper (copy, transitions)
static void SubmitImmediate(VulkanContext* ctx, std::function<void(VkCommandBuffer)> recordFunc)
{
    VkDevice device = ctx->Device();
    VkCommandPool pool = ctx->CommandPool();
    VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    recordFunc(cmd);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence fence;
    vkCreateFence(device, &fci, nullptr, &fence);

    vkQueueSubmit(ctx->GraphicsQueue(), 1, &submit, fence);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

bool Renderer::CreateVertexBuffer()
{
    VkDevice device = m_context->Device();
    VkPhysicalDevice phys = m_context->PhysicalDevice();

    VkDeviceSize bufferSize = sizeof(Vertex) * TRI_VERTS.size();

    // Create staging buffer (host visible)
    CreateBufferRaw(device, phys, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_stagingBuffer, m_stagingBufferMemory);

    // Map and copy vertex data
    void* data;
    vkMapMemory(device, m_stagingBufferMemory, 0, bufferSize, 0, &data);
    std::memcpy(data, TRI_VERTS.data(), (size_t)bufferSize);
    vkUnmapMemory(device, m_stagingBufferMemory);

    // Create device local vertex buffer (destination)
    CreateBufferRaw(device, phys, bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_vertexBuffer, m_vertexBufferMemory);

    // Copy staging -> device local
    SubmitImmediate(m_context, [&](VkCommandBuffer cmd) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = bufferSize;
        vkCmdCopyBuffer(cmd, m_stagingBuffer, m_vertexBuffer, 1, &copyRegion);
        });

    std::cerr << "Staging buffer: " << (uintptr_t)m_stagingBuffer
        << " stagingMem: " << (uintptr_t)m_stagingBufferMemory
        << " vertexBuffer: " << (uintptr_t)m_vertexBuffer
        << " vertexMem: " << (uintptr_t)m_vertexBufferMemory
        << " bytesCopied: " << bufferSize << "\n";

    std::cerr << "TRI_VERTS count = " << TRI_VERTS.size() << "\n";

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
    vkResetCommandBuffer(cmd, 0);

    m_frameStarted = true;
    return true;
}

//void Renderer::Render(Scene& /*scene*/, double /*dt*/)
void Renderer::Render( double /*dt*/)
{
    if (!m_frameStarted || !m_context) return;

    const size_t frameIndex = m_currentFrame % VulkanContext::DEFAULT_SWAPCHAIN_IMAGE_COUNT;
    VkCommandBuffer cmd = m_context->GetCommandBuffer(frameIndex);

    // Begin command buffer
    VkCommandBufferBeginInfo cbbi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult res = vkBeginCommandBuffer(cmd, &cbbi);
    if (res != VK_SUCCESS) {
        std::cerr << "vkBeginCommandBuffer failed: " << res << "\n";
        return;
    }

    // Begin render pass with a clear color
    const auto& swap = m_context->GetSwapchainInfo();
    VkClearValue clearColor{};
    clearColor.color.float32[0] = 0.0f; // R
    clearColor.color.float32[1] = 0.2f; // G
    clearColor.color.float32[2] = 0.4f; // B
    clearColor.color.float32[3] = 1.0f; // A

    VkRenderPassBeginInfo rpbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpbi.renderPass = m_renderPass;
    rpbi.framebuffer = m_framebuffers[m_imageIndex];
    rpbi.renderArea.offset = { 0, 0 };
    rpbi.renderArea.extent = swap.extent;
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    //Draw here....
    {
        // Bind pipeline and vertex buffer, then draw
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

        VkBuffer vertexBuffers[] = { m_vertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);

        vkCmdDraw(cmd, static_cast<uint32_t>(TRI_VERTS.size()), 1, 0, 0);

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
    if (!m_frameStarted || !m_context) return;

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
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
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
    CleanupFramebuffers();
    if (!m_context->RecreateSwapchain(width, height)) {
        std::cerr << "Failed to recreate swapchain during resize\n";
        return;
    }
    if (!CreateFramebuffers()) {
        std::cerr << "Failed to recreate framebuffers after resize\n";
    }
}
