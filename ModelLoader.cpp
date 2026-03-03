#include "ModelLoader.h"


#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION


#include"ThirdParty/gltf/tiny_gltf.h"

bool ExtractPrimitive(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    // ---------- POSITION ----------
    auto posAttr = primitive.attributes.find("POSITION");
    if (posAttr == primitive.attributes.end())
        return false;

    const tinygltf::Accessor& posAccessor =
        model.accessors[posAttr->second];

    const tinygltf::BufferView& posView =
        model.bufferViews[posAccessor.bufferView];

    const tinygltf::Buffer& posBuffer =
        model.buffers[posView.buffer];

    const float* positions =
        reinterpret_cast<const float*>(
            &posBuffer.data[posAccessor.byteOffset + posView.byteOffset]);

    size_t vertexCount = posAccessor.count;

    // ---------- TEXCOORD (optional) ----------
    const float* texcoords = nullptr;

    auto uvAttr = primitive.attributes.find("TEXCOORD_0");
    if (uvAttr != primitive.attributes.end())
    {
        const tinygltf::Accessor& uvAccessor =
            model.accessors[uvAttr->second];

        const tinygltf::BufferView& uvView =
            model.bufferViews[uvAccessor.bufferView];

        const tinygltf::Buffer& uvBuffer =
            model.buffers[uvView.buffer];

        texcoords = reinterpret_cast<const float*>(
            &uvBuffer.data[uvAccessor.byteOffset + uvView.byteOffset]);
    }

    // ---------- BUILD VERTICES ----------
    outVertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; i++)
    {
        Vertex v{};

        v.pos = glm::vec3(
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2]);

        v.color = glm::vec3(1.0f); // glTF color optional, default white

        if (texcoords)
        {
            v.uv = glm::vec2(
                texcoords[i * 2 + 0],
                1.0f - texcoords[i * 2 + 1]); // flip V for Vulkan
        }
        else
        {
            v.uv = glm::vec2(0.0f);
        }

        outVertices[i] = v;
    }

    // ---------- INDICES ----------
    if (primitive.indices < 0)
        return false;

    const tinygltf::Accessor& indexAccessor =
        model.accessors[primitive.indices];

    const tinygltf::BufferView& indexView =
        model.bufferViews[indexAccessor.bufferView];

    const tinygltf::Buffer& indexBuffer =
        model.buffers[indexView.buffer];

    const uint8_t* dataPtr =
        &indexBuffer.data[indexAccessor.byteOffset + indexView.byteOffset];

    outIndices.resize(indexAccessor.count);

    if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
    {
        const uint16_t* buf =
            reinterpret_cast<const uint16_t*>(dataPtr);

        for (size_t i = 0; i < indexAccessor.count; i++)
            outIndices[i] = static_cast<uint32_t>(buf[i]);
    }
    else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
    {
        const uint32_t* buf =
            reinterpret_cast<const uint32_t*>(dataPtr);

        for (size_t i = 0; i < indexAccessor.count; i++)
            outIndices[i] = buf[i];
    }
    else
    {
        return false; // unsupported index type
    }

    return true;
}

bool ModelLoader::LoadModel(const std::string& path, vkapp::VulkanContext* context, MaterialManager& materialManager,
	std::vector<Mesh>& outMeshes, std::vector<Renderable>& outRenderables)
{

	tinygltf::Model model;
	tinygltf::TinyGLTF loader;
	std::string err, warn;

	bool ret = loader.LoadBinaryFromFile(&model, &err, &warn, path);
	if (!ret) return false;

	//Extract the model data

    //1.Extract glTF Materials
    std::vector<uint32_t> gltfMaterialToEngineMaterial;

    for (const auto& mat : model.materials)
    {
        int baseColorTexIndex = mat.pbrMetallicRoughness.baseColorTexture.index;

        if (baseColorTexIndex >= 0)
        {
            const tinygltf::Texture& tex = model.textures[baseColorTexIndex];
            const tinygltf::Image& img = model.images[tex.source];

            // Create material from raw image data
            uint32_t matHandle =
                materialManager.CreateMaterialFromMemory(
                    img.image.data(),
                    img.width,
                    img.height);

            gltfMaterialToEngineMaterial.push_back(matHandle);
        }
        else
        {
            // fallback white texture
            uint32_t defaultMat = materialManager.CreateDefaultMaterial();
            gltfMaterialToEngineMaterial.push_back(defaultMat);
        }
    }

    //2. Extract Meshes
    for (const auto& mesh : model.meshes)
    {
        for (const auto& primitive : mesh.primitives)
        {
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            if (!ExtractPrimitive(model, primitive, vertices, indices))
                continue;

            Mesh gpuMesh{};
            gpuMesh.indexCount = static_cast<uint32_t>(indices.size());

            VkDevice device = context->Device();
            VkPhysicalDevice phys = context->PhysicalDevice();
            VkCommandPool cmdPool = context->CommandPool();
            VkQueue queue = context->GraphicsQueue();

            // =========================
            // 1️⃣ VERTEX BUFFER
            // =========================

            VkDeviceSize vertexSize =
                sizeof(Vertex) * vertices.size();

            // Create staging buffer
            VkBuffer vertexStaging;
            VkDeviceMemory vertexStagingMem;

            CreateBufferRaw(
                device,
                phys,
                vertexSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                vertexStaging,
                vertexStagingMem);

            // Copy CPU data into staging
            void* vData;
            vkMapMemory(device, vertexStagingMem, 0, vertexSize, 0, &vData);
            memcpy(vData, vertices.data(), (size_t)vertexSize);
            vkUnmapMemory(device, vertexStagingMem);

            // Create device-local vertex buffer
            CreateBufferRaw(
                device,
                phys,
                vertexSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                gpuMesh.vertexBuffer,
                gpuMesh.vertexMemory);

            // Copy staging → device buffer
            SubmitImmediate(device, cmdPool, queue , [&](VkCommandBuffer cmd)
                {
                    VkBufferCopy copy{};
                    copy.size = vertexSize;

                    vkCmdCopyBuffer(
                        cmd,
                        vertexStaging,
                        gpuMesh.vertexBuffer,
                        1,
                        &copy);
                });

            // Destroy staging
            vkDestroyBuffer(device, vertexStaging, nullptr);
            vkFreeMemory(device, vertexStagingMem, nullptr);


            // =========================
            // 2️. INDEX BUFFER
            // =========================

            VkDeviceSize indexSize =
                sizeof(uint32_t) * indices.size();

            VkBuffer indexStaging;
            VkDeviceMemory indexStagingMem;

            CreateBufferRaw(
                device,
                phys,
                indexSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                indexStaging,
                indexStagingMem);

            void* iData;
            vkMapMemory(device, indexStagingMem, 0, indexSize, 0, &iData);
            memcpy(iData, indices.data(), (size_t)indexSize);
            vkUnmapMemory(device, indexStagingMem);

            // Create device-local index buffer
            CreateBufferRaw(
                device,
                phys,
                indexSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                gpuMesh.indexBuffer,
                gpuMesh.indexMemory);

            // Copy staging → device
            SubmitImmediate(device, cmdPool, queue, [&](VkCommandBuffer cmd)
                {
                    VkBufferCopy copy{};
                    copy.size = indexSize;

                    vkCmdCopyBuffer(
                        cmd,
                        indexStaging,
                        gpuMesh.indexBuffer,
                        1,
                        &copy);
                });

            // Destroy staging
            vkDestroyBuffer(device, indexStaging, nullptr);
            vkFreeMemory(device, indexStagingMem, nullptr);


            // =========================
            // 3️⃣ Store Mesh
            // =========================

            m_meshes.push_back(gpuMesh);

        }
    }


}
