// Renderer.h
#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace vkapp 
{

    class VulkanContext;
    class Scene; // forward-declare for future use

    class Renderer {
    public:
        Renderer();
        ~Renderer();

        bool Init(VulkanContext* context, Scene* scene = nullptr);
        void Cleanup();

        bool BeginFrame();
        void Render(Scene& scene, double dt);
        void EndFrame();

        void OnResize(int width, int height);

    private:
        bool CreateRenderPass();
        bool CreateFramebuffers();
        void CleanupFramebuffers();

        VulkanContext* m_context = nullptr;
        Scene* m_scene = nullptr;

        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_framebuffers;

        size_t m_currentFrame = 0;
        uint32_t m_imageIndex = 0;

        bool m_frameStarted = false;
        bool m_resized = false;
    };
} // namespace vkapp
