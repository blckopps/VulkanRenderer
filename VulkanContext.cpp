// VulkanContext.cpp
#include "VulkanContext.h"
#include <iostream>
#include <set>
#include <algorithm>
#include <cstring>

using namespace vkapp;


static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT /*messageTypes*/,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* /*pUserData*/)
{
    std::cerr << "[Vulkan] " << pCallbackData->pMessage << "\n";
    return VK_FALSE;
}

VulkanContext::VulkanContext() = default;

VulkanContext::~VulkanContext()
{
    Cleanup();
}

bool VulkanContext::Init(HWND hwnd, int width, int height, bool enableValidation)
{
    m_enableValidation = enableValidation;

    if (!CreateInstance(enableValidation)) {
        std::cerr << "Failed to create Vulkan instance\n";
        return false;
    }

    if (!CreateSurface(hwnd)) {
        std::cerr << "Failed to create Win32 surface\n";
        return false;
    }

    if (!PickPhysicalDevice()) {
        std::cerr << "Failed to pick physical device\n";
        return false;
    }

    if (!CreateLogicalDevice()) {
        std::cerr << "Failed to create logical device\n";
        return false;
    }

    if (!CreateSwapchain(width, height)) {
        std::cerr << "Failed to create swapchain\n";
        return false;
    }

    if (!CreateImageViews()) {
        std::cerr << "Failed to create image views\n";
        return false;
    }

    if (!CreateCommandPoolAndBuffers()) {
        std::cerr << "Failed to create command pool/buffers\n";
        return false;
    }

    if (!CreateSyncObjects()) {
        std::cerr << "Failed to create sync objects\n";
        return false;
    }

    return true;
}

void VulkanContext::Cleanup()
{
    // Wait before destroying
    if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);

    CleanupSwapchain();

    for (size_t i = 0; i < m_swapchainImageCount && !m_imageAvailableSemaphores.empty(); ++i) {
        if (m_imageAvailableSemaphores[i]) vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        if (m_renderFinishedSemaphores[i]) vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        if (m_Fences[i]) vkDestroyFence(m_device, m_Fences[i], nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_Fences.clear();

    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

// --- Instance creation + debug messenger ---
bool VulkanContext::CreateInstance(bool enableValidation)
{
    // Application info (optional)
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "VulkanApp";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "VKEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    createInfo.pApplicationInfo = &appInfo;

    //device extensions
    // Extensions: surface + platform surface
    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);

    // Optional: validation layers & debug utils extension
    std::vector<const char*> layers;
    if (enableValidation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    VkResult res = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateInstance failed: " << res << "\n";
        return false;
    }

    // Setup debug messenger if enabled
    if (enableValidation) {
        VkDebugUtilsMessengerCreateInfoEXT dbgci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        dbgci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgci.pfnUserCallback = &DebugCallback;

        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (func) {
            func(m_instance, &dbgci, nullptr, &m_debugMessenger);
        }
    }

    return true;
}

// --- Surface creation ---
bool VulkanContext::CreateSurface(HWND hwnd)
{
    if (m_instance == VK_NULL_HANDLE) return false;

    VkWin32SurfaceCreateInfoKHR sci{ VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR };
    sci.hwnd = hwnd;
    sci.hinstance = GetModuleHandle(nullptr);

    VkResult res = vkCreateWin32SurfaceKHR(m_instance, &sci, nullptr, &m_surface);
    if (res != VK_SUCCESS) {
        std::cerr << "vkCreateWin32SurfaceKHR failed: " << res << "\n";
        return false;
    }
    return true;
}

// --- Physical device selection ---
bool VulkanContext::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "No Vulkan physical devices found\n";
        return false;
    }

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, physicalDevices.data());

    //struct PhysicalDeviceInfo { VkPhysicalDevice dev; int score; };
    //std::vector<PhysicalDeviceInfo> PhysicalDeviceInfos;

    //for (auto dev : devices)
    //{
    //    VkPhysicalDeviceProperties props;
    //    vkGetPhysicalDeviceProperties(dev, &props);

    //    VkPhysicalDeviceFeatures feats;
    //    vkGetPhysicalDeviceFeatures(dev, &feats);

    //    //prefer discrete GPU and geometry shader support
    //    int index = 0;
    //    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ;
    //    if (feats.geometryShader) score += 100;
    //    // More heuristics here: memory, timestamp, required extensions...

    //    PhysicalDeviceInfos.push_back({ dev, score });
    //}

    // sort descending
    //std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.score > b.score; });

    for (auto& device : physicalDevices)
    {
        m_physicalDevice = device;

        // Query queue families
        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qCount, nullptr);

        std::vector<VkQueueFamilyProperties> qprops(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qCount, qprops.data());

        m_graphicsFamily = -1;
        m_presentFamily = -1;
        for (uint32_t i = 0; i < qCount; ++i)
        {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_graphicsFamily = static_cast<int>(i);
            }
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
            if (presentSupport)
            {
                m_presentFamily = static_cast<int>(i);
            }
            if (m_graphicsFamily >= 0 && m_presentFamily >= 0) break;
        }

        if (m_graphicsFamily < 0 || m_presentFamily < 0)
        {
            
            continue;
        }

        // Check required device extensions (swapchain)
        const std::vector<const char*> requiredDeviceExt = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);

        std::vector<VkExtensionProperties> devExtProp(extCount);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, devExtProp.data());

        std::set<std::string> availableExt;
        for (auto& e : devExtProp) availableExt.insert(e.extensionName);

        bool ok = true;
        for (auto req : requiredDeviceExt) {
            if (availableExt.find(req) == availableExt.end()) { ok = false; break; }
        }
        if (!ok) continue;

        // If we reach here, device is acceptable
        break;
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "No suitable physical device found\n";
        return false;
    }

    //Print info for selected device if required....
    /*VkPhysicalDeviceProperties vkPhyDeviceProp;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &vkPhyDeviceProp);
    const char* deviceName = vkPhyDeviceProp.deviceName;
    std::cerr << "Selected device: " << deviceName << std::endl;*/

    //Memory Propeties

    //vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &vkPhysicalDeviceMemoryProp);

   // vkGetPhysicalDeviceFeatures(vkPhysicalDeviceSelected, &vkPhysicalDeviceFeatures);
    return true;
}

// --- Logical device + queues ---
bool VulkanContext::CreateLogicalDevice()
{
    if (m_physicalDevice == VK_NULL_HANDLE)
        return false;

    // Prepare queue create infos (unique families only)
    std::vector<uint32_t> uniqueFamilies;
    uniqueFamilies.push_back(static_cast<uint32_t>(m_graphicsFamily));

    if (m_presentFamily != m_graphicsFamily) 
        uniqueFamilies.push_back(static_cast<uint32_t>(m_presentFamily));

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    float queuePriority = 1.0f;
    for (uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qci.queueFamilyIndex = family;
        qci.queueCount = 1;
        qci.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(qci);
    }
    
    //CHECK:
    // Device features you want to enable
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_FALSE; // enable if you want wireframe

    // Required extensions
    const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
    dci.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    dci.pQueueCreateInfos = queueCreateInfos.data();
    dci.pEnabledFeatures = &deviceFeatures;
    dci.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    dci.ppEnabledExtensionNames = deviceExtensions.data();

    // Validation layers on device (deprecated on newer Vulkan, but keep if needed)
    std::vector<const char*> layers;
    if (m_enableValidation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }
    dci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    dci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    VkResult res = vkCreateDevice(m_physicalDevice, &dci, nullptr, &m_device);
    if (res != VK_SUCCESS)
    {
        std::cerr << "vkCreateDevice failed: " << res << "\n";
        return false;
    }

    // get queues
    vkGetDeviceQueue(m_device, static_cast<uint32_t>(m_graphicsFamily), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, static_cast<uint32_t>(m_presentFamily), 0, &m_presentQueue);

    return true;
}

// --- Swapchain creation (basic) ---
bool VulkanContext::CreateSwapchain(uint32_t width, uint32_t height)
{
    // Query surface capabilities and formats
    VkSurfaceCapabilitiesKHR surfCaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfCaps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &presentModeCount, presentModes.data());

    // Choose surface format (prefer SRGB + B8G8R8A8)
    VkSurfaceFormatKHR chosenSurfaceFormat = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenSurfaceFormat = f;
            break;
        }
    }

    // Choose present mode (prefer MAILBOX if available)
    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed
    for (auto& p : presentModes) {
        if (p == VK_PRESENT_MODE_MAILBOX_KHR) { chosenPresentMode = p; break; }
    }

    // Swap extent
    VkExtent2D extent = {};
    if (surfCaps.currentExtent.width != UINT32_MAX) {
        extent = surfCaps.currentExtent;
    }
    else {
        extent.width = std::clamp<uint32_t>(width, surfCaps.minImageExtent.width, surfCaps.maxImageExtent.width);
        extent.height = std::clamp<uint32_t>(height, surfCaps.minImageExtent.height, surfCaps.maxImageExtent.height);
    }

    // Image count
    uint32_t imageCount = surfCaps.minImageCount + 1;
    if (surfCaps.maxImageCount > 0 && imageCount > surfCaps.maxImageCount) {
        imageCount = surfCaps.maxImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR vkSwapchainCreateInfo{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
    vkSwapchainCreateInfo.pNext = nullptr;
    vkSwapchainCreateInfo.flags = 0;
    vkSwapchainCreateInfo.surface = m_surface;
    vkSwapchainCreateInfo.minImageCount = imageCount;
    vkSwapchainCreateInfo.imageFormat = chosenSurfaceFormat.format;
    vkSwapchainCreateInfo.imageColorSpace = chosenSurfaceFormat.colorSpace;
    vkSwapchainCreateInfo.imageExtent = extent;
    vkSwapchainCreateInfo.imageArrayLayers = 1;
    vkSwapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    uint32_t queueFamilyIndices[] = { static_cast<uint32_t>(m_graphicsFamily), static_cast<uint32_t>(m_presentFamily) };
    if (m_graphicsFamily != m_presentFamily) {
        vkSwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        vkSwapchainCreateInfo.queueFamilyIndexCount = 2;
        vkSwapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        vkSwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkSwapchainCreateInfo.queueFamilyIndexCount = 0;
        vkSwapchainCreateInfo.pQueueFamilyIndices = nullptr;
    }

    vkSwapchainCreateInfo.preTransform = surfCaps.currentTransform;
    vkSwapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    vkSwapchainCreateInfo.presentMode = chosenPresentMode;
    vkSwapchainCreateInfo.clipped = VK_TRUE;
    vkSwapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkResult res = vkCreateSwapchainKHR(m_device, &vkSwapchainCreateInfo, nullptr, &swapchain);
    if (res != VK_SUCCESS)
    {
        std::cerr << "vkCreateSwapchainKHR failed: " << res << "\n";
        return false;
    }

    // store
    m_swapchainInfo.swapchain = swapchain;
    m_swapchainInfo.imageFormat = chosenSurfaceFormat.format;
    m_swapchainInfo.extent = extent;

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, swapchain, &actualCount, nullptr);
    m_swapchainInfo.images.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, swapchain, &actualCount, m_swapchainInfo.images.data());

    return true;
}

bool VulkanContext::CreateImageViews()
{
    m_swapchainInfo.imageViews.clear();
    m_swapchainInfo.imageViews.resize(m_swapchainInfo.images.size());

    for (size_t i = 0; i < m_swapchainInfo.images.size(); ++i)
    {
        VkImageViewCreateInfo vkImageViewCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vkImageViewCreateInfo.image = m_swapchainInfo.images[i];
        vkImageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vkImageViewCreateInfo.format = m_swapchainInfo.imageFormat;
        vkImageViewCreateInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        vkImageViewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vkImageViewCreateInfo.subresourceRange.baseMipLevel = 0;
        vkImageViewCreateInfo.subresourceRange.levelCount = 1;
        vkImageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
        vkImageViewCreateInfo.subresourceRange.layerCount = 1;

        VkResult res = vkCreateImageView(m_device, &vkImageViewCreateInfo, nullptr, &m_swapchainInfo.imageViews[i]);
        if (res != VK_SUCCESS) {
            std::cerr << "vkCreateImageView failed: " << res << "\n";
            return false;
        }
    }
    return true;
}

bool VulkanContext::CreateCommandPoolAndBuffers()
{
    //Create command buffer pool
    VkCommandPoolCreateInfo vkCommandPoolCreateInfo{};
    vkCommandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    vkCommandPoolCreateInfo.pNext = nullptr;
    vkCommandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCommandPoolCreateInfo.queueFamilyIndex = static_cast<uint32_t>(m_graphicsFamily);;

    VkResult res = vkCreateCommandPool(m_device, &vkCommandPoolCreateInfo, nullptr, &m_commandPool);
    if (res != VK_SUCCESS)
    {
        std::cerr << "vkCreateCommandPool failed: " << res << "\n";
        return false;
    }

    // Create command buffers
    m_commandBuffers.resize(m_swapchainImageCount);

    VkCommandBufferAllocateInfo vkCommandBufferAllocInfo{};
    vkCommandBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    vkCommandBufferAllocInfo.pNext = nullptr;
    vkCommandBufferAllocInfo.commandPool = m_commandPool;
    vkCommandBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    vkCommandBufferAllocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());;

    res = vkAllocateCommandBuffers(m_device, &vkCommandBufferAllocInfo, m_commandBuffers.data());
    if (res != VK_SUCCESS)
    {
        std::cerr << "vkAllocateCommandBuffers failed: " << res << "\n";
        return false;
    }

    return true;
}

bool VulkanContext::CreateSyncObjects()
{
    //TODO:CHECK
    m_imageAvailableSemaphores.resize(m_swapchainImageCount);
    m_renderFinishedSemaphores.resize(m_swapchainImageCount);
    m_Fences.resize(m_swapchainImageCount);

    //By default semaphore is BINARY, if not specified
    VkSemaphoreCreateInfo vkSemaphoreCreateInfo{};
    vkSemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkSemaphoreCreateInfo.pNext = nullptr;
    vkSemaphoreCreateInfo.flags = 0;

    VkFenceCreateInfo vkFenceCreateInfo{};
    vkFenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkFenceCreateInfo.pNext = nullptr;
    vkFenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; //0 if signaled, if VK_FENCE_CREATE_SIGNALED_BIT then unsignaled.

    for (size_t i = 0; i < m_swapchainImageCount; ++i) 
    {
        if (vkCreateSemaphore(m_device, &vkSemaphoreCreateInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device, &vkSemaphoreCreateInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device, &vkFenceCreateInfo, nullptr, &m_Fences[i]) != VK_SUCCESS)
        {
            std::cerr << "failed to create sync objects for a frame\n";
            return false;
        }
    }
    return true;
}

void VulkanContext::CleanupSwapchain()
{
    if (m_device == VK_NULL_HANDLE) return;

    for (auto iv : m_swapchainInfo.imageViews) {
        if (iv) vkDestroyImageView(m_device, iv, nullptr);
    }
    m_swapchainInfo.imageViews.clear();

    if (m_swapchainInfo.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapchainInfo.swapchain, nullptr);
        m_swapchainInfo.swapchain = VK_NULL_HANDLE;
    }
    m_swapchainInfo.images.clear();
}

// Acquire next image (wrapper)
VkResult VulkanContext::AcquireNextImage(uint32_t* outImageIndex, VkSemaphore signalSemaphore)
{
    return vkAcquireNextImageKHR(m_device, m_swapchainInfo.swapchain, UINT64_MAX, signalSemaphore, VK_NULL_HANDLE, outImageIndex);
}

// Present wrapper
VkResult VulkanContext::QueuePresent(uint32_t imageIndex, VkSemaphore waitSemaphore)
{
    VkPresentInfoKHR vkPresentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    vkPresentInfo.waitSemaphoreCount = (waitSemaphore != VK_NULL_HANDLE) ? 1u : 0u;

    VkSemaphore waits[1] = { waitSemaphore };
    vkPresentInfo.pWaitSemaphores = (waitSemaphore != VK_NULL_HANDLE) ? waits : nullptr;

    VkSwapchainKHR vkSwapchain[1] = { m_swapchainInfo.swapchain };

    vkPresentInfo.swapchainCount = 1;
    vkPresentInfo.pSwapchains = vkSwapchain;
    vkPresentInfo.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(m_presentQueue, &vkPresentInfo);
}

VkSemaphore VulkanContext::GetImageAvailableSemaphore(size_t frame) const
{
    return m_imageAvailableSemaphores[frame % m_swapchainImageCount];
}
VkSemaphore VulkanContext::GetRenderFinishedSemaphore(size_t frame) const
{
    return m_renderFinishedSemaphores[frame % m_swapchainImageCount];
}
VkFence VulkanContext::GetFence(size_t frame) const
{
    return m_Fences[frame % m_swapchainImageCount];
}

VkCommandBuffer VulkanContext::GetCommandBuffer(size_t frame) const
{
    return m_commandBuffers[frame % m_swapchainImageCount];
}


// Recreate swapchain (basic)
bool VulkanContext::RecreateSwapchain(int width, int height)
{
    vkDeviceWaitIdle(m_device);

    CleanupSwapchain();

    if (!CreateSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) return false;
    if (!CreateImageViews()) return false;
    // If you have depth buffers, framebuffers, renderpasses - recreate them here too.

    return true;
}
