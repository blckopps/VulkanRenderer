// Renderer.h
#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>


namespace vkapp 
{

    class VulkanContext;
    class Scene; 

    struct Vertex
    {
        float pos[3];
        float color[3];

        static VkVertexInputBindingDescription getBindingDesc()
        {
            VkVertexInputBindingDescription vkVertexInputBindingDescription{};
            vkVertexInputBindingDescription.binding = 0;
            vkVertexInputBindingDescription.stride = sizeof(Vertex);
            vkVertexInputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            return vkVertexInputBindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 2> getAttribDesc()
        {
            std::array<VkVertexInputAttributeDescription, 2> attrs{};
            attrs[0].binding = 0;                   // is the binding number which this attribute takes its data from.
            attrs[0].location = 0;                  //layout(location=0) in vec3 vPosition
            attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attrs[0].offset = offsetof(Vertex, pos);

            attrs[1].binding = 0;                   // is the binding number which this attribute takes its data from.
            attrs[1].location = 1;                  //layout(location=1) in vec3 color
            attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT; 
            attrs[1].offset = offsetof(Vertex, color);  //Jump
            return attrs;
        }
    };

    class Renderer
    {
    public:
        Renderer();
        ~Renderer();

        bool Init(VulkanContext* context, Scene* scene = nullptr);
        void Cleanup();

        bool BeginFrame();
        //void Render(Scene& scene, double dt);
        void Render(double dt);
        void EndFrame();

        void OnResize(int width, int height);

    private:
        bool CreateRenderPass();
        bool CreateFramebuffers();
        void CleanupFramebuffers();

        bool CreateGraphicsPipelineLayout();

        bool CreateGraphicsPipeline();
        void DestroyGraphicsPipelineAndLayout();

        bool CreateVertexBuffer();
        void DestroyVertexBuffer();

        bool CreateIndexBuffer();
        void DestroyIndexBuffer();

        bool CreateDescriptorResources();
        void DestroyDescriptorResources();
         
        bool CreateDepthResource();
        void DestroyDepthResource();

        // low-level helpers used internally
        uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);


        VulkanContext* m_context = nullptr;
        Scene* m_scene = nullptr;

        //Depth
        VkImage m_depthImage = VK_NULL_HANDLE;
        VkImageView m_depthImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;

        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_framebuffers;

        // Drawing pipeline + layout
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;

        // Vertex buffer (device local) + staging
        VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;

        //Index buffer
        VkBuffer m_indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_indexBufferMemory = VK_NULL_HANDLE;

        VkBuffer m_stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_stagingBufferMemory = VK_NULL_HANDLE;

        //Descriptor set + layout + descriptor pool
        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> m_descriptorSets;

        std::vector<VkBuffer> m_uniformBuffers;
        std::vector<VkDeviceMemory> m_uniformBuffersMemory;

        size_t m_currentFrame = 0;
        uint32_t m_imageIndex = 0;

        bool m_frameStarted = false;
        bool m_resized = false;


        VkClearDepthStencilValue vkClearDepthStencilValue{};
        VkClearColorValue vkClearColorValue{};
    };
} // namespace vkapp
