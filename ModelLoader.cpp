#include "ModelLoader.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/gltf/tiny_gltf.h"

#include <iostream>
#include <stack>

// ============================================================
//  Internal helpers
// ============================================================

// ---------------------------------------------------------------------------
// Typed accessor reader
// Returns a typed pointer into the buffer for accessor[i], respecting
// byteStride so interleaved buffers work correctly.
//
// IMPORTANT: T must be the *element* type (e.g. glm::vec3 for POSITION,
// glm::vec2 for TEXCOORD), NOT float.  When byteStride is 0 the buffer is
// tightly packed and the natural stride is sizeof(T) — i.e. the full element.
// ---------------------------------------------------------------------------
template<typename T>
static const T* AccessorPtr(const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    size_t elementIndex)
{
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];

    // byteStride == 0  →  tightly packed  →  stride = sizeof full element (T)
    // byteStride  > 0  →  interleaved     →  use the declared stride
    const size_t stride = (view.byteStride > 0) ? view.byteStride : sizeof(T);

    const uint8_t* base = buffer.data.data()
        + view.byteOffset
        + accessor.byteOffset;

    return reinterpret_cast<const T*>(base + elementIndex * stride);
}

// ---------------------------------------------------------------------------
// Resolve a node's local transform to a glm::mat4.
// glTF nodes store EITHER a matrix OR TRS components.
// ---------------------------------------------------------------------------
static glm::mat4 NodeLocalTransform(const tinygltf::Node& node)
{
    // Case 1 : explicit 4x4 matrix (column-major in glTF, same as GLM)
    if (node.matrix.size() == 16)
    {
        return glm::make_mat4(node.matrix.data());
    }

    glm::mat4 T(1.0f), R(1.0f), S(1.0f);

    if (node.translation.size() == 3)
    {
        T = glm::translate(glm::mat4(1.0f),
            glm::vec3((float)node.translation[0],
                (float)node.translation[1],
                (float)node.translation[2]));
    }

    if (node.rotation.size() == 4)
    {
        // glTF quaternion order: x y z w
        glm::quat q((float)node.rotation[3],   // w
            (float)node.rotation[0],   // x
            (float)node.rotation[1],   // y
            (float)node.rotation[2]);  // z
        R = glm::mat4_cast(q);
    }

    if (node.scale.size() == 3)
    {
        S = glm::scale(glm::mat4(1.0f),
            glm::vec3((float)node.scale[0],
                (float)node.scale[1],
                (float)node.scale[2]));
    }

    return T * R * S;
}

// ---------------------------------------------------------------------------
// Upload CPU vertex + index data to device-local GPU buffers.
// Uses a single staging buffer for both to minimise SubmitImmediate calls.
// ---------------------------------------------------------------------------
static bool UploadMesh(
    VkDevice           device,
    VkPhysicalDevice   phys,
    VkCommandPool      cmdPool,
    VkQueue            queue,
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    Mesh& outMesh)
{
    const VkDeviceSize vertexSize = sizeof(Vertex) * vertices.size();
    const VkDeviceSize indexSize = sizeof(uint32_t) * indices.size();

    // --- Staging buffers ---
    VkBuffer       vStaging;    VkDeviceMemory vStagingMem;
    VkBuffer       iStaging;    VkDeviceMemory iStagingMem;

    CreateBufferRaw(device, phys, vertexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        vStaging, vStagingMem);

    CreateBufferRaw(device, phys, indexSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        iStaging, iStagingMem);

    // Map and copy CPU -> staging
    void* ptr;
    vkMapMemory(device, vStagingMem, 0, vertexSize, 0, &ptr);
    memcpy(ptr, vertices.data(), (size_t)vertexSize);
    vkUnmapMemory(device, vStagingMem);

    vkMapMemory(device, iStagingMem, 0, indexSize, 0, &ptr);
    memcpy(ptr, indices.data(), (size_t)indexSize);
    vkUnmapMemory(device, iStagingMem);

    // --- Device-local buffers ---
    CreateBufferRaw(device, phys, vertexSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outMesh.vertexBuffer, outMesh.vertexMemory);

    CreateBufferRaw(device, phys, indexSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        outMesh.indexBuffer, outMesh.indexMemory);

    // Single submit: copy both vertex and index buffers
    SubmitImmediate(device, cmdPool, queue, [&](VkCommandBuffer cmd)
        {
            VkBufferCopy vc{}; vc.size = vertexSize;
            vkCmdCopyBuffer(cmd, vStaging, outMesh.vertexBuffer, 1, &vc);

            VkBufferCopy ic{}; ic.size = indexSize;
            vkCmdCopyBuffer(cmd, iStaging, outMesh.indexBuffer, 1, &ic);
        });

    // Cleanup staging
    vkDestroyBuffer(device, vStaging, nullptr); vkFreeMemory(device, vStagingMem, nullptr);
    vkDestroyBuffer(device, iStaging, nullptr); vkFreeMemory(device, iStagingMem, nullptr);

    outMesh.indexCount = static_cast<uint32_t>(indices.size());
    return true;
}

// ---------------------------------------------------------------------------
// Extract one glTF primitive into CPU Vertex/Index arrays.
// Handles: POSITION, TEXCOORD_0, NORMAL, TANGENT + byteStride per attribute.
// ---------------------------------------------------------------------------
static bool ExtractPrimitive(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    // ---- POSITION (required) ----
    auto posIt = primitive.attributes.find("POSITION");
    if (posIt == primitive.attributes.end())
    {
        std::cerr << "[ModelLoader] Primitive missing POSITION\n";
        return false;
    }

    const tinygltf::Accessor& posAcc = model.accessors[posIt->second];
    const size_t vertexCount = posAcc.count;

    // ---- Optional attributes ----
    auto uvIt = primitive.attributes.find("TEXCOORD_0");
    auto normalIt = primitive.attributes.find("NORMAL");
    auto tangentIt = primitive.attributes.find("TANGENT");

    const bool hasUV = (uvIt != primitive.attributes.end());
    const bool hasNormal = (normalIt != primitive.attributes.end());
    const bool hasTangent = (tangentIt != primitive.attributes.end());

    const tinygltf::Accessor* uvAcc = hasUV ? &model.accessors[uvIt->second] : nullptr;
    const tinygltf::Accessor* normalAcc = hasNormal ? &model.accessors[normalIt->second] : nullptr;
    const tinygltf::Accessor* tangentAcc = hasTangent ? &model.accessors[tangentIt->second] : nullptr;

    // ---- Build vertices ----
    outVertices.resize(vertexCount);

    for (size_t i = 0; i < vertexCount; ++i)
    {
        Vertex v{};

        // Position
        const glm::vec3* p = AccessorPtr<glm::vec3>(model, posAcc, i);
        v.pos = *p;

        // Default color white
        v.color = glm::vec3(1.0f);

        // UV — flip V axis for Vulkan
        if (hasUV)
        {
            const glm::vec2* t = AccessorPtr<glm::vec2>(model, *uvAcc, i);
            v.uv = glm::vec2(t->x, 1.0f - t->y);
        }

        // Normal
        if (hasNormal)
        {
            const glm::vec3* n = AccessorPtr<glm::vec3>(model, *normalAcc, i);
            v.normal = *n;
        }
        else
        {
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f); // fallback up
        }

        // Tangent (vec4: xyz = tangent, w = bitangent sign)
        if (hasTangent)
        {
            const glm::vec4* t = AccessorPtr<glm::vec4>(model, *tangentAcc, i);
            v.tangent = *t;
        }
        else
        {
            v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // fallback
        }

        outVertices[i] = v;
    }

    // ---- Indices ----
    if (primitive.indices < 0)
    {
        std::cerr << "[ModelLoader] Non-indexed primitives not supported\n";
        return false;
    }

    const tinygltf::Accessor& indexAcc = model.accessors[primitive.indices];
    const tinygltf::BufferView& indexView = model.bufferViews[indexAcc.bufferView];
    const tinygltf::Buffer& indexBuf = model.buffers[indexView.buffer];

    const uint8_t* dataPtr =
        indexBuf.data.data() + indexView.byteOffset + indexAcc.byteOffset;

    outIndices.resize(indexAcc.count);

    switch (indexAcc.componentType)
    {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    {
        for (size_t i = 0; i < indexAcc.count; ++i)
            outIndices[i] = static_cast<uint32_t>(dataPtr[i]);
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    {
        const uint16_t* buf = reinterpret_cast<const uint16_t*>(dataPtr);
        for (size_t i = 0; i < indexAcc.count; ++i)
            outIndices[i] = static_cast<uint32_t>(buf[i]);
        break;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    {
        const uint32_t* buf = reinterpret_cast<const uint32_t*>(dataPtr);
        for (size_t i = 0; i < indexAcc.count; ++i)
            outIndices[i] = buf[i];
        break;
    }
    default:
        std::cerr << "[ModelLoader] Unsupported index component type: "
            << indexAcc.componentType << "\n";
        return false;
    }

    return true;
}

// ============================================================
//  ModelLoader::LoadModel
// ============================================================
bool ModelLoader::LoadModel(
    const std::string& path,
    vkapp::VulkanContext* context,
    MaterialManager& materialManager,
    std::vector<Mesh>& outMeshes,
    std::vector<Renderable>& outRenderables)
{
    // ---- 1. Parse glTF file (.glb binary or .gltf JSON) ----
    tinygltf::Model     model;
    tinygltf::TinyGLTF  loader;
    std::string         err, warn;

    bool loaded = false;

    const std::string ext = path.substr(path.rfind('.'));
    if (ext == ".glb")
    {
        loaded = loader.LoadBinaryFromFile(&model, &err, &warn, path);
    }
    else if (ext == ".gltf")
    {
        loaded = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    }
    else
    {
        std::cerr << "[ModelLoader] Unknown file extension: " << ext << "\n";
        return false;
    }

    if (!warn.empty()) std::cerr << "[ModelLoader] Warning: " << warn << "\n";
    if (!err.empty())  std::cerr << "[ModelLoader] Error: " << err << "\n";
    if (!loaded)       return false;

    // ---- 2. Upload materials ----
    materialManager.Init(context, static_cast<uint32_t>(model.materials.size() + 1));

    // Map: glTF material index -> engine material handle
    std::vector<uint32_t> matHandles(model.materials.size());

    for (size_t mi = 0; mi < model.materials.size(); ++mi)
    {
        const auto& mat = model.materials[mi];
        int baseColorTexIdx = mat.pbrMetallicRoughness.baseColorTexture.index;

        if (baseColorTexIdx >= 0)
        {
            const tinygltf::Texture& tex = model.textures[baseColorTexIdx];
            const tinygltf::Image& img = model.images[tex.source];

            matHandles[mi] = materialManager.CreateMaterialFromMemory(
                img.image.data(),
                img.width,
                img.height);
        }
        else
        {
            matHandles[mi] = materialManager.CreateDefaultMaterial();
        }
    }

    uint32_t defaultMat = materialManager.CreateDefaultMaterial();

    // ---- 3. Walk the scene graph (iterative DFS) ----
    //
    // We maintain a stack of (nodeIndex, parentWorldTransform) pairs.
    // Each node may reference a mesh; if it does we extract all its
    // primitives, applying the accumulated world transform.

    VkDevice         device = context->Device();
    VkPhysicalDevice phys = context->PhysicalDevice();
    VkCommandPool    cmdPool = context->CommandPool();
    VkQueue          queue = context->GraphicsQueue();

    // Use the default scene, or scene 0 if none specified
    const int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (model.scenes.empty())
    {
        std::cerr << "[ModelLoader] No scenes in file\n";
        return false;
    }

    const tinygltf::Scene& scene = model.scenes[sceneIndex];

    // Stack stores { nodeIndex, accumulated world transform }
    std::stack<std::pair<int, glm::mat4>> nodeStack;

    for (int rootNode : scene.nodes)
        nodeStack.push({ rootNode, glm::mat4(1.0f) });

    while (!nodeStack.empty())
    {
        auto [nodeIdx, parentWorld] = nodeStack.top();
        nodeStack.pop();

        const tinygltf::Node& node = model.nodes[nodeIdx];

        // Accumulate transform: world = parent * local
        const glm::mat4 worldTransform = parentWorld * NodeLocalTransform(node);

        // Push children so they inherit this node's world transform
        for (int child : node.children)
            nodeStack.push({ child, worldTransform });

        // If this node has no mesh, skip geometry extraction
        if (node.mesh < 0)
            continue;

        const tinygltf::Mesh& gltfMesh = model.meshes[node.mesh];

        for (const auto& primitive : gltfMesh.primitives)
        {
            std::vector<Vertex>   vertices;
            std::vector<uint32_t> indices;

            if (!ExtractPrimitive(model, primitive, vertices, indices))
            {
                std::cerr << "[ModelLoader] Skipping primitive in mesh '"
                    << gltfMesh.name << "'\n";
                continue;
            }

            // Upload to GPU
            Mesh gpuMesh{};
            if (!UploadMesh(device, phys, cmdPool, queue, vertices, indices, gpuMesh))
            {
                std::cerr << "[ModelLoader] GPU upload failed for mesh '"
                    << gltfMesh.name << "'\n";
                continue;
            }

            uint32_t meshIndex = static_cast<uint32_t>(outMeshes.size());
            outMeshes.push_back(gpuMesh);

            // Resolve material
            uint32_t matHandle = defaultMat;
            if (primitive.material >= 0 &&
                primitive.material < (int)matHandles.size())
            {
                matHandle = matHandles[primitive.material];
            }

            Renderable r{};
            r.meshIndex = meshIndex;
            r.materialIndex = matHandle;
            r.transform = worldTransform;   // node's world-space matrix

            outRenderables.push_back(r);

            std::cout << "[ModelLoader] Mesh '" << gltfMesh.name
                << "' -> " << vertices.size() << " verts, "
                << indices.size() << " indices\n";
        }
    }

    std::cout << "[ModelLoader] Loaded '" << path << "' -> "
        << outMeshes.size() << " meshes, "
        << outRenderables.size() << " renderables\n";

    return true;
}