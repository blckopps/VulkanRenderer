#version 450

// -------------------------------------------------------
// Inputs from vertex shader
// -------------------------------------------------------
layout(location = 0) in vec2  inUV;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec3  inTangent;
layout(location = 3) in vec3  inBitangent;
layout(location = 4) in vec3  inWorldPos;
layout(location = 5) in vec4  inColor;

// -------------------------------------------------------
// Set 1: per-material texture (bound per renderable)
// -------------------------------------------------------
layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessTexture;

//Match CPU side struct
layout(std140, set = 1, binding = 2) uniform MaterialUBO
{
    vec4 baseColorFactor;
    vec4 metallicRoughness;
} material;

// -------------------------------------------------------
// Output
// -------------------------------------------------------
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;
const vec3 LIGHT_DIRECTION = vec3(-0.4, -1.0, -0.3);
const vec3 LIGHT_COLOR = vec3(3.0);
const vec3 AMBIENT_COLOR = vec3(0.03);


float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-6);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}


float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denominator = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(PI * denominator * denominator, 1e-6);
}

void main()
{
    // Base color
    vec4 baseColor = texture(baseColorTexture, inUV) *  material.baseColorFactor * inColor;

    vec4 mrSample = texture(metallicRoughnessTexture, inUV);

    // glTF convention: G = roughness, B = metallic.
    float roughness = clamp( material.metallicRoughness.y * mrSample.g, 0.04, 1.0);

    float metallic = clamp( material.metallicRoughness.x * mrSample.b, 0.0, 1.0);

    vec3 N = normalize(inNormal);
    // Assumes the camera is at world-space origin. If the vertex shader
    // provides camera position, use normalize(cameraPosition - inWorldPos).

    //vec3 V = normalize(-inWorldPos);
    //vec3 L = normalize(-LIGHT_DIRECTION);
    //vec3 H = normalize(V + L);
    //
    //float NdotL = max(dot(N, L), 0.0);
    //float NdotV = max(dot(N, V), 1e-4);
    //vec3 F0 = mix(vec3(0.04), baseColor.rgb, metallic);
    //vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    //float D = DistributionGGX(N, H, roughness);
    //float G = GeometrySmith(N, V, L, roughness);
    //vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    //
    //vec3 kS = F;
    //vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    //vec3 diffuse = kD * baseColor.rgb / PI;
    //vec3 directLighting = (diffuse + specular) * LIGHT_COLOR * NdotL;
    //vec3 ambientLighting = AMBIENT_COLOR * baseColor.rgb * (1.0 - metallic);
    //
    //outColor = vec4(ambientLighting + directLighting, baseColor.a);

    //DEBUG
    //vec3 N = normalize(inNormal);
    vec3 L = normalize(vec3(0.4, 0.8, 0.5));
    vec3 V = normalize(-inWorldPos);

    float NdotL = max(dot(N, L), 0.0);

    vec3 debugColor =
        baseColor.rgb * (vec3(0.25) + vec3(NdotL));

    outColor = vec4(debugColor, baseColor.a);
    //



    // Simple directional light in world space
    //vec3 lightDir   = normalize(vec3(1.0, 2.0, 1.0));
    //vec3 normal     = normalize(inNormal);

   // float diffuse   = max(dot(normal, lightDir), 0.0);
    //float ambient   = 0.15;

    //outColor = vec4(albedo.rgb * (ambient + diffuse), albedo.a);
}
