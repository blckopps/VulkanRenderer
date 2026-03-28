#version 450

layout(location = 0) in vec2 outUV;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D uTextureSampler;

void main() 
{
    outColor = texture(uTextureSampler, outUV);
    //outColor = vec4(1.0, 0.0f, 0.0f, 1.0f);
}
