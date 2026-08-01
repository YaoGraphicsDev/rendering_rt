#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_samplerless_texture_functions: require


layout(location = 0) in vec3 inLocalPosition;
layout(location = 1) flat in uint inProbeID;

layout(location = 0) out vec4 outColor;

layout (push_constant) uniform PushConstants {
	mat4 projectView;
	vec3 probeStart;
	ivec3 probeCounts; // powers of two in all dimensions. x * y * z equal to the height of generated image
	vec3 probeStep;
	int probeSize;  
    ivec2 atlasSize; // width, height
	float probeScale;
    uint sampleAtlasIndex; // 0, 1
} consts;

layout(set = 0, binding = 0) uniform sampler samplerAtlas;    // linear

// Set 3: Managed by framegraph
//  binding 0 - 15:     textures
//  binding 16 - 23:    SSBO
layout(set = 3, binding = 0) uniform texture2D texAtlas0;
layout(set = 3, binding = 1) uniform texture2D texAtlas1;

#include "math_utils.glsl" //! #include "../common/math_utils.glsl" // replacement comment for visual studio glsl extension
#include "probe_indexing.glsl" //! #include "../common/probe_indexing.glsl"

// not correct. This pin direction at 0.5 positions. Pixel center. Does not align with normalizedOctCoord() convention
/*
vec2 textureCoordFromDirection(vec3 dir, uint probeIndex, ivec2 atlasSize, int probeSideLength) {
    vec2 oct = octEncode(normalize(dir));
    vec2 oct01 = oct * 0.5 + 0.5;

    int tileSide = probeSideLength + 2;
    int probesPerRow = (atlasSize.x - 2) / tileSide;

    ivec2 tileCoord = ivec2(
        int(probeIndex) % probesPerRow,
        int(probeIndex) / probesPerRow
    );

    // First interior texel of this probe.
    ivec2 interiorBase = ivec2(1) + tileCoord * tileSide + ivec2(1);

    // Sample from center of first interior texel to center of last interior texel.
    vec2 texelCoord =
        vec2(interiorBase) +
        vec2(0.5) +
        oct01 * float(probeSideLength - 1);

    return texelCoord / vec2(atlasSize);
}
*/

// ivec2 probeTexelFromDirection(
//     vec3 dir,
//     uint probeIndex,
//     ivec2 atlasSize,
//     int probeSideLength)
// {
//     vec2 oct = octEncode(normalize(dir));
//     vec2 oct01 = oct * 0.5 + 0.5;
// 
//     int tileSide = probeSideLength + 2;
//     int probesPerRow = (atlasSize.x - 2) / tileSide;
// 
//     ivec2 tileCoord = ivec2(
//         int(probeIndex) % probesPerRow,
//         int(probeIndex) / probesPerRow
//     );
// 
//     ivec2 interiorBase = ivec2(1) + tileCoord * tileSide + ivec2(1);
// 
//     ivec2 localInterior = ivec2(
//         clamp(floor(oct01 * float(probeSideLength)), vec2(0.0), vec2(float(probeSideLength - 1)))
//     );
// 
//     return interiorBase + localInterior;
// }

void main() {
    vec2 texCoord = textureCoordFromDirection(inLocalPosition, inProbeID, consts.atlasSize, consts.probeSize);
    // cosine weighted average radiance. irradiance / pi. Which also happens to be the diffuse outgoing radiance if the probe is a real sphere with white lambertian surface
    vec3 averageRadiance = vec3(0.0);
    if (consts.sampleAtlasIndex == 0) {
        averageRadiance = textureLod(sampler2D(texAtlas0, samplerAtlas), texCoord, 0).rgb;
    } else if (consts.sampleAtlasIndex == 1) {
        averageRadiance = textureLod(sampler2D(texAtlas1, samplerAtlas), texCoord, 0).rgb;
    }
    outColor = vec4(averageRadiance, 1.0);
}