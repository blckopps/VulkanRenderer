#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <array>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


#include"ThirdParty/gltf/json.hpp"
#include"ThirdParty/Common/stb_image.h"
#include"ThirdParty/gltf/stb_image_write.h"

#include "VulkanContext.h"
#include "MaterialManager.h"


#include "VkHelper.h"

//not good
namespace vkapp
{
    class VulkanContext;
   
}

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 uv;
};

struct Mesh
{
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexMemory;

    VkBuffer indexBuffer;
    VkDeviceMemory indexMemory;

    uint32_t indexCount;
};


struct Renderable
{
    uint32_t meshIndex;
    uint32_t materialIndex;
    glm::mat4 transform;
};

class ModelLoader
{

    
    

public:
    bool LoadModel(
        const std::string& path,
        vkapp::VulkanContext* context,
        MaterialManager& materialManager,
        std::vector<Mesh>& outMeshes,
        std::vector<Renderable>& outRenderables);


private:
    std::vector<Mesh> m_meshes;

};

