#version 450
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 colorOut0;
layout(location = 1) out vec4 colorOut1;

#define UPDATE_IRRADIANCE   0
#define UPDATE_DEPTH        1
layout (push_constant) uniform PushConstants {
    int probeSize;  
    ivec2 atlasSize; // width, height
    int raysPerProbe;
    float probeMaxDistance;
    float depthSharpness; // See IrradianceField.h depthSharpness
    float hysteresis;     // See IrradianceField.h hysteresis
    uint updateType; // 0 -- irradiance, 1 -- depth moments. Had to do it this way because irradiance and depth atlases have different dimensions. Cant write to both in one shader
    uint atlasIndex; // 0, 1
} consts;

// Set 3: Managed by framegraph
//  binding 0 - 15:     textures
//  binding 16 - 23:    SSBO
//  binding 24 - 31:    storage image
layout(set = 3, binding = 0) uniform texture2D texRayOrigins;
layout(set = 3, binding = 1) uniform texture2D texRayDirections;
layout(set = 3, binding = 2) uniform texture2D texRayHitLocations;
layout(set = 3, binding = 3) uniform texture2D texRayHitRadiance;
layout(set = 3, binding = 4) uniform texture2D texRayHitNormals;

// layout(set = 3, binding = 24) uniform writeonly image2D imgIrradianceAtlas;
// layout(set = 3, binding = 25) uniform writeonly image2D imgDepthAtlas;

#include "probe_indexing.glsl" //! #include "../common/probe_indexing.glsl"
#include "math_utils.glsl" //! #include "../common/math_utils.glsl"

const float epsilon = 1e-6;

void main() {
    int probeID = atlasTexelBelongsToProbeID(ivec2(gl_FragCoord), consts.probeSize, consts.atlasSize.x);
    vec3 texelDirection = octDecode(normalizedOctCoord(ivec2(gl_FragCoord.xy), consts.probeSize));

    const float energyConservation = 1.0; // TODO: what's this? Perhaps to prevent multibounce feedback from amplifying itself?

    // For each ray
    // vec4 irradianceSum = vec4(0.0);
    // vec4 depthSum = vec4(0.0);
    vec4 result = vec4(0.0);
	for (int r = 0; r < consts.raysPerProbe; ++r) {
		ivec2 C = ivec2(r, probeID);

		vec3 rayDirection = texelFetch(texRayDirections, C, 0).xyz;
        vec3 rayHitRadiance = texelFetch(texRayHitRadiance, C, 0).xyz * energyConservation;
		vec3 rayHitLocation = texelFetch(texRayHitLocations, C, 0).xyz;

        vec3 probeLocation = texelFetch(texRayOrigins, C, 0).xyz;
        // Will be zero on a miss
		vec3 rayHitNormal = texelFetch(texRayHitNormals, C, 0).xyz;

        rayHitLocation += rayHitNormal * 0.01f; // TODO: why?

         // maxDistance should be slightly larger than probe grid cell diagonal length
         // see IrradianceField.cpp m_maxDistance
		float rayProbeDistance = min(consts.probeMaxDistance, length(probeLocation - rayHitLocation));
        
        // Detect misses and force depth
		if (dot(rayHitNormal, rayHitNormal) < epsilon) {
            rayProbeDistance = consts.probeMaxDistance;
        }

        float weight = max(0.0, dot(texelDirection, rayDirection)); // cosine weight for irradiance
        if (consts.updateType == UPDATE_IRRADIANCE) {
            if (weight >= epsilon) {
                result += vec4(rayHitRadiance * weight, weight);
            }
        } else { // update depth
            weight = pow(weight, consts.depthSharpness);
            if (weight >= epsilon) {
                result += vec4(rayProbeDistance * weight, rayProbeDistance * rayProbeDistance * weight, 0.0, weight);
            }
        }
    }

    // conceptually, weight sum (w component) smaller than epsilon is unlikely if ray directions are sampled uniformly from a spherical surface
    // but with depthSharpness, cosine lobe turns thin, so small weight sum is more likely to happen to depth than irradiance.
    // Also, if the sampling process is true random, it is possible that sampled rays directions all point away from a specific octo surface direction,
    // which results in w smaller that epsilon
    if (result.a > epsilon) {
        result.xyz /= result.w;
        result.w = 1.0 - consts.hysteresis;
    }

    if (consts.atlasIndex == 0) {
        colorOut0 = result;
        colorOut1 = vec4(0.0); // blend nothing
    } else if (consts.atlasIndex == 1) {
        colorOut1 = result;
        colorOut0 = vec4(0.0); // blend nothing
    } else {
        // erranous index. blend in magenta
        colorOut0 = vec4(1.0, 0.0, 1.0, 1.0);
        colorOut1 = vec4(1.0, 0.0, 1.0, 1.0);
    }
}