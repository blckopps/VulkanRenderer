#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <string>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "ThirdParty/gltf/json.hpp"
#include "ThirdParty/Common/stb_image.h"
#include "ThirdParty/gltf/stb_image_write.h"

#include "VulkanContext.h"
#include "MaterialManager.h"
#include "VkHelper.h"

namespace vkapp
{
    class VulkanContext;
}

// ============================================================
//  Vertex  —  pos / color / uv / normal / tangent
//  
// ============================================================
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec4 tangent;
    glm::vec2 texCoord0;
    glm::vec2 texCoord1;
    glm::vec4 color;

    // Optional skinning data
    glm::uvec4 joints;
    glm::vec4 weights;

    static VkVertexInputBindingDescription getBindingDesc()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return binding;
    }

    static std::array<VkVertexInputAttributeDescription, 8>
        getAttribDesc()
    {
        std::array<VkVertexInputAttributeDescription, 8> attributes{};

        // POSITION
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, pos);

        // NORMAL
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, normal);

        // TANGENT
        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, tangent);

        // TEXCOORD_0
        attributes[3].location = 3;
        attributes[3].binding = 0;
        attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[3].offset = offsetof(Vertex, texCoord0);

        // TEXCOORD_1
        attributes[4].location = 4;
        attributes[4].binding = 0;
        attributes[4].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[4].offset = offsetof(Vertex, texCoord1);

        // COLOR_0
        attributes[5].location = 5;
        attributes[5].binding = 0;
        attributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[5].offset = offsetof(Vertex, color);

        // JOINTS_0
        attributes[6].location = 6;
        attributes[6].binding = 0;
        attributes[6].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[6].offset = offsetof(Vertex, joints);

        // WEIGHTS_0
        attributes[7].location = 7;
        attributes[7].binding = 0;
        attributes[7].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[7].offset = offsetof(Vertex, weights);

        return attributes;
    }
};
// ============================================================
//  GPU-side mesh buffers
// ============================================================
struct Mesh
{
    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;

    VkBuffer       indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;

    uint32_t       indexCount = 0;
    uint32_t       vertexCount = 0;

    uint32_t       materialIndex = 0;
};

// ============================================================
//  One renderable = mesh + material + world transform
// ============================================================
struct Renderable
{
    uint32_t  meshIndex = 0;
    uint32_t  materialIndex = 0;
    glm::mat4 transform = glm::mat4(1.0f);
};

// ============================================================
//  ModelLoader
// ============================================================
class ModelLoader
{
public:
    // Loads a .glb (binary glTF) or .gltf (JSON + external buffers).
    // Fills outMeshes and outRenderables; materials are registered
    // through materialManager.
    bool LoadModel(
        const std::string& path,
        vkapp::VulkanContext* context,
        MaterialManager& materialManager,
        std::vector<Mesh>& outMeshes,
        std::vector<Renderable>& outRenderables);

    void CleanUp();

private:
    vkapp::VulkanContext* m_context = nullptr;
    std::vector<Mesh> m_meshes; // internal scratch — not used across calls
};