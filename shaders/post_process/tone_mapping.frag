#version 450

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLDR;

layout(set = 0, binding = 0) uniform sampler samplerHDR;

// Set 3: Managed by framegraph
//  binding 0 - 15:     textures
layout(set = 3, binding = 0) uniform texture2D texHDR;

void main() {
    vec3 hdr = texture(sampler2D(texHDR, samplerHDR), inUV).rgb;
    hdr = max(hdr, vec3(0.0f));
    // reinhard tone mapping
    outLDR = vec4(hdr / (hdr + vec3(1.0)), 1.0f);
}