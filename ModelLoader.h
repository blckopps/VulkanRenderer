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
//  NOTE: Renderer.h must use THIS definition (or include this
//        header and remove its own Vertex struct).
// ============================================================
struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 uv;
    glm::vec3 normal;
    glm::vec4 tangent;   // xyz = tangent dir, w = bitangent sign (+1/-1)

    // ---- Vulkan pipeline descriptions ----
    static VkVertexInputBindingDescription getBindingDesc()
    {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 5> getAttribDesc()
    {
        std::array<VkVertexInputAttributeDescription, 5> attrs{};

        // location 0 : position
        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(Vertex, pos);

        // location 1 : color
        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(Vertex, color);

        // location 2 : uv
        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(Vertex, uv);

        // location 3 : normal
        attrs[3].binding = 0;
        attrs[3].location = 3;
        attrs[3].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[3].offset = offsetof(Vertex, normal);

        // location 4 : tangent  (vec4 — w holds bitangent sign)
        attrs[4].binding = 0;
        attrs[4].location = 4;
        attrs[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[4].offset = offsetof(Vertex, tangent);

        return attrs;
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

private:
    std::vector<Mesh> m_meshes; // internal scratch — not used across calls
};