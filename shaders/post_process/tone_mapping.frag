#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLDR;

layout(set = 0, binding = 0) uniform sampler samplerHDR;

// Set 3: Managed by framegraph
//  binding 0 - 15: textures
layout(set = 3, binding = 0) uniform texture2D texHDR;

vec3 acesFitted(vec3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;

    return clamp(
        (color * (a * color + b)) /
        (color * (c * color + d) + e),
        0.0f,
        1.0f
    );
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec3 hdr = texelFetch(texHDR, texel, 0).rgb;
    hdr = max(hdr, vec3(0.0f));

    // Increase this value to brighten the scene.
    const float exposure = 1.5f;

    vec3 ldr = acesFitted(hdr * exposure);

    outLDR = vec4(ldr, 1.0f);
}