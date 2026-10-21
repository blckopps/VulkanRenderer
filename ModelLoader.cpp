#define NOMINMAX

#include "ModelLoader.h"

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/gltf/tiny_gltf.h"

#include <iostream>
#include <stack>
#include <algorithm>

// ============================================================
//  Internal helpers
// ============================================================
// 
//LoadModel()
//│
//├── Load glTF
//│
//├── Create MaterialManager materials
//│
//└── Traverse scene nodes
//│
//└── glTF Mesh
//│
//└── Primitive
//│
//├── ExtractPrimitive()
//│      ├── POSITION
//│      ├── NORMAL
//│      ├── TANGENT
//│      ├── TEXCOORD_0
//│      ├── TEXCOORD_1
//│      ├── COLOR_0
//│      ├── JOINTS_0
//│      └── WEIGHTS_0
//│
//├── UploadMesh()
//│
//├── outMeshes.push_back()
//│
//└── Renderable
//├── meshIndex
//├── materialIndex
//└── world transform
// 
// 
// ---------------------------------------------------------------------------
// Typed accessor reader
// Returns a typed pointer into the buffer for accessor[i], respecting
// byteStride so interleaved buffers work correctly.
//
// IMPORTANT: T must be the *element* type (e.g. glm::vec3 for POSITION,
// glm::vec2 for TEXCOORD), NOT float.  When byteStride is 0 the buffer is
// tightly packed and the natural stride is sizeof(T) — i.e. the full element.
// ---------------------------------------------------------------------------
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
static float ReadComponentAsFloat(
    const unsigned char* p,
    int componentType,
    bool normalized)
{
    switch (componentType)
    {
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
        return *reinterpret_cast<const float*>(p);

    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    {
        const uint8_t v = *reinterpret_cast<const uint8_t*>(p);
        return normalized ? static_cast<float>(v) / 255.0f
            : static_cast<float>(v);
    }

    case TINYGLTF_COMPONENT_TYPE_BYTE:
    {
        const uint8_t v = *reinterpret_cast<const uint8_t*>(p);

        return normalized
            ? static_cast<float>(v) / 255.0f
            : static_cast<float>(v);
    }

    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
    {
        const uint16_t v = *reinterpret_cast<const uint16_t*>(p);
        return normalized ? static_cast<float>(v) / 65535.0f
            : static_cast<float>(v);
    }

    case TINYGLTF_COMPONENT_TYPE_SHORT:
    {
        const int16_t v = *reinterpret_cast<const int16_t*>(p);

        return normalized
            ? std::max(-1.0f, static_cast<float>(v) / 32767.0f)
            : static_cast<float>(v);
    }

    default:
        throw std::runtime_error("Unsupported glTF floating component type");
    }
}

static uint32_t ReadComponentAsUInt(
    const unsigned char* p,
    int componentType)
{
    switch (componentType)
    {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        return static_cast<uint32_t>(
            *reinterpret_cast<const uint8_t*>(p));

    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        return static_cast<uint32_t>(
            *reinterpret_cast<const uint16_t*>(p));

    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        return *reinterpret_cast<const uint32_t*>(p);

    default:
        throw std::runtime_error("Unsupported glTF JOINTS_0 component type");
    }
}

static void ReadFloatAttribute(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    size_t index,
    float* output,
    int outputComponentCount)
{
    if (accessor.bufferView < 0)
        throw std::runtime_error("glTF accessor has no bufferView");

    const tinygltf::BufferView& view =
        model.bufferViews[accessor.bufferView];

    const tinygltf::Buffer& buffer =
        model.buffers[view.buffer];

    const int sourceComponentCount =
        tinygltf::GetNumComponentsInType(accessor.type);

    if (sourceComponentCount < outputComponentCount)
        throw std::runtime_error("glTF attribute has too few components");

    const size_t componentSize =
        tinygltf::GetComponentSizeInBytes(accessor.componentType);

    const size_t elementSize =
        componentSize * sourceComponentCount;

    const size_t stride =
        view.byteStride > 0 ? view.byteStride : elementSize;

    const unsigned char* element =
        buffer.data.data()
        + view.byteOffset
        + accessor.byteOffset
        + index * stride;

    for (int c = 0; c < outputComponentCount; ++c)
    {
        output[c] = ReadComponentAsFloat(
            element + c * componentSize,
            accessor.componentType,
            accessor.normalized);
    }
}

static void ReadJointsAttribute(
    const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    size_t index,
    glm::uvec4& output)
{
    if (accessor.bufferView < 0)
        throw std::runtime_error("glTF JOINTS_0 accessor has no bufferView");

    const tinygltf::BufferView& view =
        model.bufferViews[accessor.bufferView];

    const tinygltf::Buffer& buffer =
        model.buffers[view.buffer];

    const int componentCount =
        tinygltf::GetNumComponentsInType(accessor.type);

    if (componentCount != 4)
        throw std::runtime_error("JOINTS_0 must contain 4 components");

    const size_t componentSize =
        tinygltf::GetComponentSizeInBytes(accessor.componentType);

    const size_t elementSize =
        componentSize * componentCount;

    const size_t stride =
        view.byteStride > 0 ? view.byteStride : elementSize;

    const unsigned char* element =
        buffer.data.data()
        + view.byteOffset
        + accessor.byteOffset
        + index * stride;

    output.x = ReadComponentAsUInt(
        element + componentSize * 0,
        accessor.componentType);

    output.y = ReadComponentAsUInt(
        element + componentSize * 1,
        accessor.componentType);

    output.z = ReadComponentAsUInt(
        element + componentSize * 2,
        accessor.componentType);

    output.w = ReadComponentAsUInt(
        element + componentSize * 3,
        accessor.componentType);
}

// ---------------------------------------------------------------------------
// Extract one glTF primitive into CPU Vertex/Index arrays.
//
// Supported standard vertex attributes:
//   POSITION
//   NORMAL
//   TANGENT
//   TEXCOORD_0
//   TEXCOORD_1
//   COLOR_0
//   JOINTS_0
//   WEIGHTS_0
//
// Missing optional attributes receive sensible defaults.
// ---------------------------------------------------------------------------
static bool ExtractPrimitive(
    const tinygltf::Model& model,
    const tinygltf::Primitive& primitive,
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices)
{
    try
    {
        // POSITION is required.
        const auto posIt = primitive.attributes.find("POSITION");

        if (posIt == primitive.attributes.end())
        {
            std::cerr
                << "[ModelLoader] Primitive missing POSITION\n";
            return false;
        }

        const tinygltf::Accessor& posAcc =
            model.accessors[posIt->second];

        if (posAcc.type != TINYGLTF_TYPE_VEC3)
        {
            std::cerr
                << "[ModelLoader] POSITION is not VEC3\n";
            return false;
        }

        const size_t vertexCount = posAcc.count;

        // Optional attributes.
        const auto normalIt =
            primitive.attributes.find("NORMAL");

        const auto tangentIt =
            primitive.attributes.find("TANGENT");

        const auto uv0It =
            primitive.attributes.find("TEXCOORD_0");

        const auto uv1It =
            primitive.attributes.find("TEXCOORD_1");

        const auto colorIt =
            primitive.attributes.find("COLOR_0");

        const auto jointsIt =
            primitive.attributes.find("JOINTS_0");

        const auto weightsIt =
            primitive.attributes.find("WEIGHTS_0");

        outVertices.resize(vertexCount);

        for (size_t i = 0; i < vertexCount; ++i)
        {
            Vertex v{};

            // ---------------------------------------------------------
            // POSITION
            // ---------------------------------------------------------

            float position[3]{};

            ReadFloatAttribute(
                model,
                posAcc,
                i,
                position,
                3);

            v.pos =
                glm::vec3(
                    position[0],
                    position[1],
                    position[2]);

            // ---------------------------------------------------------
            // NORMAL
            // ---------------------------------------------------------

            if (normalIt != primitive.attributes.end())
            {
                const tinygltf::Accessor& accessor =
                    model.accessors[normalIt->second];

                float normal[3]{};

                ReadFloatAttribute(
                    model,
                    accessor,
                    i,
                    normal,
                    3);

                v.normal =
                    glm::normalize(
                        glm::vec3(
                            normal[0],
                            normal[1],
                            normal[2]));
            }
            else
            {
                v.normal =
                    glm::vec3(0.0f, 1.0f, 0.0f);
            }

            // ---------------------------------------------------------
            // TANGENT
            // ---------------------------------------------------------

            if (tangentIt != primitive.attributes.end())
            {
                const tinygltf::Accessor& accessor =
                    model.accessors[tangentIt->second];

                float tangent[4]{};

                ReadFloatAttribute(
                    model,
                    accessor,
                    i,
                    tangent,
                    4);

                v.tangent =
                    glm::vec4(
                        tangent[0],
                        tangent[1],
                        tangent[2],
                        tangent[3]);
            }
            else
            {
                v.tangent =
                    glm::vec4(
                        1.0f,
                        0.0f,
                        0.0f,
                        1.0f);
            }

            // ---------------------------------------------------------
            // TEXCOORD_0
            // ---------------------------------------------------------

            if (uv0It != primitive.attributes.end())
            {
                const tinygltf::Accessor& accessor =
                    model.accessors[uv0It->second];

                float uv[2]{};

                ReadFloatAttribute(
                    model,
                    accessor,
                    i,
                    uv,
                    2);

                // Keep the glTF UV unchanged here.
                // Do the Vulkan image-origin convention in the shader/
                // texture setup instead of silently modifying asset data.
                v.texCoord0 =
                    glm::vec2(
                        uv[0],
                        uv[1]);
            }
            else
            {
                v.texCoord0 =
                    glm::vec2(0.0f);
            }

            // ---------------------------------------------------------
            // TEXCOORD_1
            // ---------------------------------------------------------

            if (uv1It != primitive.attributes.end())
            {
                const tinygltf::Accessor& accessor =
                    model.accessors[uv1It->second];

                float uv[2]{};

                ReadFloatAttribute(
                    model,
                    accessor,
                    i,
                    uv,
                    2);

                v.texCoord1 =
                    glm::vec2(
                        uv[0],
                        uv[1]);
            }
            else
            {
                v.texCoord1 =
                    glm::vec2(0.0f);
            }

            // ---------------------------------------------------------
            // COLOR_0
            // ---------------------------------------------------------

            if (colorIt != primitive.attributes.end())
            {
                const tinygltf::Accessor& accessor =
                    model.accessors[colorIt->second];

                const int componentCount =
                    tinygltf::GetNumComponentsInType(accessor.type);

                float color[4] =
                {
                    1.0f,
                    1.0f,
                    1.0f,
                    1.0f
                };

                if (componentCount == 3)
                {
                    ReadFloatAttribute(
                        model,
                        accessor,
                        i,
                        color,
                        3);
                }
                else if (componentCount == 4)
                {
                    ReadFloatAttribute(
                        model,
                        accessor,
                        i,
                        color,
                        4);
                }
                else
                {
                    throw std::runtime_error(
                        "COLOR_0 must be VEC3 or VEC4");
                }

                v.color =
                    glm::vec4(
                        color[0],
                        color[1],
                        color[2],
                        color[3]);
            }
            else
            {
                v.color =
                    glm::vec4(1.0f);
            }

            // ---------------------------------------------------------
            // JOINTS_0
            // ---------------------------------------------------------

            if (jointsIt != primitive.attributes.end())
            {
                const tinygltf::Accessor& accessor =
                    model.accessors[jointsIt->second];

                ReadJointsAttribute(
                    model,
                    accessor,
                    i,
                    v.joints);
            }
            else
            {
                v.joints =
                    glm::uvec4(0);
            }

            // ---------------------------------------------------------
            // WEIGHTS_0
            // ---------------------------------------------------------

            if (weightsIt != primitive.attributes.end())
            {
                const tinygltf::Accessor& accessor =
                    model.accessors[weightsIt->second];

                float weights[4]{};

                ReadFloatAttribute(
                    model,
                    accessor,
                    i,
                    weights,
                    4);

                v.weights =
                    glm::vec4(
                        weights[0],
                        weights[1],
                        weights[2],
                        weights[3]);
            }
            else
            {
                v.weights =
                    glm::vec4(0.0f);
            }

            outVertices[i] = v;
        }

        // -------------------------------------------------------------
        // INDICES
        // -------------------------------------------------------------

        outIndices.clear();

        if (primitive.indices < 0)
        {
            // glTF permits non-indexed primitives.
            // Generate sequential indices instead of rejecting them.
            outIndices.resize(vertexCount);

            for (uint32_t i = 0;
                i < static_cast<uint32_t>(vertexCount);
                ++i)
            {
                outIndices[i] = i;
            }
        }
        else
        {
            const tinygltf::Accessor& indexAcc =
                model.accessors[primitive.indices];

            if (indexAcc.bufferView < 0)
            {
                throw std::runtime_error(
                    "Index accessor has no bufferView");
            }

            const tinygltf::BufferView& indexView =
                model.bufferViews[indexAcc.bufferView];

            const tinygltf::Buffer& indexBuffer =
                model.buffers[indexView.buffer];

            const size_t componentSize =
                tinygltf::GetComponentSizeInBytes(
                    indexAcc.componentType);

            const size_t stride =
                indexView.byteStride > 0
                ? indexView.byteStride
                : componentSize;

            const unsigned char* data =
                indexBuffer.data.data()
                + indexView.byteOffset
                + indexAcc.byteOffset;

            outIndices.resize(indexAcc.count);

            for (size_t i = 0;
                i < indexAcc.count;
                ++i)
            {
                const unsigned char* element =
                    data + i * stride;

                switch (indexAcc.componentType)
                {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    outIndices[i] =
                        static_cast<uint32_t>(
                            *reinterpret_cast<const uint8_t*>(element));
                    break;

                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    outIndices[i] =
                        static_cast<uint32_t>(
                            *reinterpret_cast<const uint16_t*>(element));
                    break;

                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    outIndices[i] =
                        *reinterpret_cast<const uint32_t*>(element);
                    break;

                default:
                    throw std::runtime_error(
                        "Unsupported glTF index component type");
                }
            }
        }

        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "[ModelLoader] ExtractPrimitive failed: "
            << e.what() << "\n";

        outVertices.clear();
        outIndices.clear();

        return false;
    }
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
    if (context != nullptr)
        m_context = context;

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

        const auto& pbr = mat.pbrMetallicRoughness;

        glm::vec4 baseColorFactor(
            static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]),
            static_cast<float>(pbr.baseColorFactor[3]));

        float metallicFactor = static_cast<float>(pbr.metallicFactor);
        float roughnessFactor = static_cast<float>(pbr.roughnessFactor);

        int baseColorTexIdx = mat.pbrMetallicRoughness.baseColorTexture.index;
        //int baseColorTexIdx = mat.normalTexture.index;



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
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            if (!ExtractPrimitive(model, primitive, vertices, indices))
            {
                std::cerr << "[ModelLoader] Skipping primitive in mesh '"
                    << gltfMesh.name << "'\n";
                continue;
            }

            // Upload geometry to GPU.
            Mesh gpuMesh{};

            if (!UploadMesh(
                device,
                phys,
                cmdPool,
                queue,
                vertices,
                indices,
                gpuMesh))
            {
                std::cerr << "[ModelLoader] GPU upload failed for mesh '"
                    << gltfMesh.name << "'\n";
                continue;
            }

            const uint32_t meshIndex =
                static_cast<uint32_t>(outMeshes.size());

            outMeshes.push_back(gpuMesh);

            // Renderable references the uploaded mesh and the resolved
            // MaterialManager handle.
            Renderable renderable{};
            renderable.meshIndex = meshIndex;

            if (primitive.material >= 0 &&
                primitive.material < static_cast<int>(matHandles.size()))
            {
                renderable.materialIndex =
                    matHandles[primitive.material];
            }
            else
            {
                renderable.materialIndex = defaultMat;
            }

            // Node transform is kept on the Renderable.
            // Do NOT bake worldTransform into vertex positions here.
            renderable.transform = worldTransform;

            outRenderables.push_back(renderable);

            std::cout
                << "[ModelLoader] Mesh '" << gltfMesh.name
                << "' -> "
                << vertices.size() << " verts, "
                << indices.size() << " indices, "
                << "material=" << renderable.materialIndex
                << "\n";
        }

    }

    std::cout << "[ModelLoader] Loaded '" << path << "' -> "
        << outMeshes.size() << " meshes, "
        << outRenderables.size() << " renderables\n";

    return true;
}

void ModelLoader::CleanUp()
{
    for (auto& mesh : m_meshes)
    {
        //VERTEX
        if (mesh.vertexBuffer)
        {
            vkDestroyBuffer(m_context->Device(), mesh.vertexBuffer, nullptr);
            mesh.vertexBuffer = VK_NULL_HANDLE;
        }

        if (mesh.vertexMemory)
        {
            vkFreeMemory(m_context->Device(), mesh.vertexMemory, nullptr);
            mesh.vertexMemory = VK_NULL_HANDLE;
        }

        //INDEX
        if (mesh.indexBuffer)
        {
            vkDestroyBuffer(m_context->Device(), mesh.indexBuffer, nullptr);
            mesh.indexBuffer = VK_NULL_HANDLE;
        }

        if (mesh.indexMemory)
        {
            vkFreeMemory(m_context->Device(), mesh.indexMemory, nullptr);
            mesh.indexMemory = VK_NULL_HANDLE;
        }
    }
}
