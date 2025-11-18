// VulkanContext.h
#pragma once

//vulkan header
#define VK_USE_PLATFORM_WIN32_KHR //It states what to include, changes as per platform.
#include <vulkan/vulkan.h>  //Update project prop to VK SDK path...Includes and libs
#include <vector>
#include <optional>
#include <functional>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>


#include <glm/gtc/matrix_transform.hpp> // Required for glm::rotate

namespace vkapp {

    struct SwapchainInfo 
    {
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat imageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D extent = { 0,0 };
        std::vector<VkImage> images;
        std::vector<VkImageView> imageViews;
    };

    class VulkanContext {
    public:
        VulkanContext();
        ~VulkanContext();

        // Initialize context with native Win32 HWND and initial width/height.
        // Returns true on success.
        bool Init(HWND hwnd, int width, int height, bool enableValidation = true);

        // Cleanup all Vulkan objects.
        void Cleanup();

        // Block until GPU idle
        void WaitIdle();

        // Recreate swapchain (call when window resized)
        bool RecreateSwapchain(int width, int height);

        // Accessors
        VkDevice Device() const noexcept { return m_device; }
        VkPhysicalDevice PhysicalDevice() const noexcept { return m_physicalDevice; }
        VkQueue GraphicsQueue() const noexcept { return m_graphicsQueue; }
        VkQueue PresentQueue() const noexcept { return m_presentQueue; }
        VkCommandPool CommandPool() const noexcept { return m_commandPool; }
        const SwapchainInfo& GetSwapchainInfo() const noexcept { return m_swapchainInfo; }

        // Acquire/Present helpers (simple wrappers you'll call from Renderer)
        VkResult AcquireNextImage(uint32_t* outImageIndex, VkSemaphore signalSemaphore);
        VkResult QueuePresent(uint32_t imageIndex, VkSemaphore waitSemaphore);

        // Per-frame sync handles getters
        VkSemaphore GetImageAvailableSemaphore(size_t frameIndex) const;
        VkSemaphore GetRenderFinishedSemaphore(size_t frameIndex) const;
        VkFence GetFence(size_t frameIndex) const;
        VkCommandBuffer GetCommandBuffer(size_t frameIndex) const;

        // swapchain image count/frames(2 or 3)
        static constexpr size_t DEFAULT_SWAPCHAIN_IMAGE_COUNT = 2;

    private:
        bool CreateInstance(bool enableValidation);
        bool PickPhysicalDevice();
        bool CreateLogicalDevice();
        bool CreateSurface(HWND hwnd);
        bool CreateSwapchain(uint32_t width, uint32_t height);
        bool CreateImageViews();
        bool CreateCommandPoolAndBuffers();
        bool CreateSyncObjects();

        void CleanupSwapchain();

    private:
        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;

        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        SwapchainInfo m_swapchainInfo;

        VkQueue m_graphicsQueue = VK_NULL_HANDLE;
        VkQueue m_presentQueue = VK_NULL_HANDLE;

        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> m_commandBuffers;

        // Per-frame sync
        size_t m_swapchainImageCount = DEFAULT_SWAPCHAIN_IMAGE_COUNT;
        std::vector<VkSemaphore> m_imageAvailableSemaphores;
        std::vector<VkSemaphore> m_renderFinishedSemaphores;
        std::vector<VkFence> m_Fences;

        // Queue family indices (cached)
        int m_graphicsFamily = -1;
        int m_presentFamily = -1;

        // Settings
        bool m_enableValidation = false;
        uint32_t m_queueFamilyCount = 0;


    };
} // namespace vkapp
