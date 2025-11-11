// Renderer.cpp
#include "Renderer.h"
#include "VulkanContext.h"
#include <iostream>
#include <array>

using namespace vkapp;

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
    return true;
}

void Renderer::Cleanup()
{
    if (!m_context) return;
    VkDevice device = m_context->Device();
    if (device == VK_NULL_HANDLE) return;

    CleanupFramebuffers();

    if (m_renderPass) {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
}

// create render pass (same as before)
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

void Renderer::Render(Scene& /*scene*/, double /*dt*/)
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

    // --- place for future drawing: bind pipeline, bind descriptors, vkCmdDraw calls ---
    // For now we just clear the framebuffer (renderpass clear above does that).

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
