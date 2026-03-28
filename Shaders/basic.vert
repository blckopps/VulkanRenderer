#version 450

// -------------------------------------------------------
// Vertex inputs  (must match Vertex struct in ModelLoader.h)
// -------------------------------------------------------
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in vec4 inTangent;   // w = bitangent sign

// -------------------------------------------------------
// Set 0: per-frame UBO  (view + proj, written once per frame)
// model matrix is now a push constant — not in the UBO.
// -------------------------------------------------------
layout(set = 0, binding = 0) uniform FrameUBO
{
    mat4 model;   // kept for struct ABI compat, not used here
    mat4 view;
    mat4 proj;
} ubo;

// -------------------------------------------------------
// Push constant: per-object model matrix
// One vkCmdPushConstants call per renderable in the draw loop.
// -------------------------------------------------------
layout(push_constant) uniform PushConstants
{
    mat4 model;
} push;

// -------------------------------------------------------
// Outputs to fragment shader
// -------------------------------------------------------
layout(location = 0) out vec2  outUV;
layout(location = 1) out vec3  outNormal;     // world-space normal
layout(location = 2) out vec3  outTangent;    // world-space tangent
layout(location = 3) out vec3  outBitangent;  // world-space bitangent
layout(location = 4) out vec3  outWorldPos;   // world-space position
layout(location = 5) out vec3  outColor;

void main()
{
    // World-space position
    vec4 worldPos = push.model * vec4(inPos, 1.0);
    outWorldPos   = worldPos.xyz;

    // Clip-space position
    gl_Position = ubo.proj * ubo.view * worldPos;

    // Pass UVs and vertex color through
    outUV    = inUV;
    outColor = inColor;

    // Transform normal and tangent to world space.
    // Use the normal matrix (transpose of inverse) to handle non-uniform scale.
    mat3 normalMatrix = transpose(inverse(mat3(push.model)));

    outNormal  = normalize(normalMatrix * inNormal);
    outTangent = normalize(normalMatrix * inTangent.xyz);

    // Reconstruct bitangent: B = (N x T) * tangent.w
    // tangent.w is +1 or -1 (bitangent handedness from glTF)
    outBitangent = cross(outNormal, outTangent) * inTangent.w;
}
