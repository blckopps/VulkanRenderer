#version 450

// -------------------------------------------------------
// Inputs from vertex shader
// -------------------------------------------------------
layout(location = 0) in vec2  inUV;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec3  inTangent;
layout(location = 3) in vec3  inBitangent;
layout(location = 4) in vec3  inWorldPos;
layout(location = 5) in vec3  inColor;

// -------------------------------------------------------
// Set 1: per-material texture (bound per renderable)
// -------------------------------------------------------
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

// -------------------------------------------------------
// Output
// -------------------------------------------------------
layout(location = 0) out vec4 outColor;

void main()
{
    // Sample albedo texture
    vec4 albedo = texture(albedoTexture, inUV) * vec4(inColor, 1.0);

    // Simple directional light in world space
    vec3 lightDir   = normalize(vec3(1.0, 2.0, 1.0));
    vec3 normal     = normalize(inNormal);

    float diffuse   = max(dot(normal, lightDir), 0.0);
    float ambient   = 0.15;

    outColor = vec4(albedo.rgb * (ambient + diffuse), albedo.a);
}
