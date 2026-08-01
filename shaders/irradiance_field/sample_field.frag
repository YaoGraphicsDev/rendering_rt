#version 450
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 result;

layout (push_constant) uniform PushConstants {
    vec4 projEncoded;
    vec4 viewBaseQuat;
    vec3 camPos;

    vec3 probeStart;
    vec3 probeStep; // > 0. distance between probes, in x, y, z directions.

    ivec2 irradAtlasSize; // width, height
    int irradProbeSize;

    ivec2 depthAtlasSize; // width, height
    int depthProbeSize;

    ivec3 probeCounts; // >= 2. powers of two in all dimensions. x * y * z equal to the height of generated image.
    float normalBias;
    uint sampleAtlasIndex; // 0, 1
} consts;


layout(set = 0, binding = 0) uniform sampler samplerAtlas;    // linear

// Set 3: Managed by framegraph
//  binding 0 - 15:     textures
//  binding 16 - 23:    SSBO
layout(set = 3, binding = 0) uniform texture2D texGDepth;
layout(set = 3, binding = 1) uniform texture2D texGAlbedo;
layout(set = 3, binding = 2) uniform texture2D texGNormal;
layout(set = 3, binding = 3) uniform texture2D texGMetallicRoughness;
layout(set = 3, binding = 4) uniform texture2D texIrradAtlas0;
layout(set = 3, binding = 5) uniform texture2D texIrradAtlas1;
layout(set = 3, binding = 6) uniform texture2D texDepthAtlas0;
layout(set = 3, binding = 7) uniform texture2D texDepthAtlas1;

#include "math_utils.glsl" //! #include "../common/math_utils.glsl"
#include "probe_indexing.glsl" //! #include "../common/probe_indexing.glsl"

// find the base coord of the cell that a point in space resides in
ivec3 baseGridCoord(vec3 p) {
    vec3 grid = (p - consts.probeStart) / consts.probeStep;
    return ivec3(floor(grid));
}

void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    // world position
    float depth = texelFetch(texGDepth, pixel, 0).r;
    vec4 ndc = vec4(inUV * 2.0f - 1.0f, depth, 1.0f);
    vec4 viewSpaceCoord = ndcToView(ndc, consts.projEncoded);
    vec3 worldSpacePosition = viewInvMult(consts.viewBaseQuat, consts.camPos, viewSpaceCoord).xyz;
    vec3 worldSpaceNormal = normalize(texelFetch(texGNormal, pixel, 0).xyz);

    vec3 view = normalize(consts.camPos - worldSpacePosition);

    ivec3 baseGridCoord = baseGridCoord(worldSpacePosition);
    if (any(greaterThanEqual(baseGridCoord, consts.probeCounts - ivec3(1)))) {
        result = vec4(vec3(0.0f), 1.0f);
        return;
    }
    if (any(lessThan(baseGridCoord, ivec3(0)))) {
        result = vec4(vec3(0.0f), 1.0f);
        return;
    }

    vec3 baseProbePos = gridCoordToPosition(baseGridCoord, consts.probeStart, consts.probeStep);
    vec3 sumIrradiance = vec3(0.0);
    float sumWeight = 0.0;

    // alpha is how far from the floor(currentVertex) position. on [0, 1] for each axis.
    vec3 alpha = clamp((worldSpacePosition - baseProbePos) / consts.probeStep, vec3(0.0), vec3(1.0));

    for (int i = 0; i < 8; ++i) {
        // Compute the offset grid coord and clamp to the probe grid boundary
        // Offset = 0 or 1 along each axis
        ivec3 offset = ivec3(i, i >> 1, i >> 2) & ivec3(1);
        ivec3 probeGridCoord = clamp(baseGridCoord + offset, ivec3(0), ivec3(consts.probeCounts - 1));
        int p = gridCoordToProbeIndex(probeGridCoord, consts.probeCounts);

        vec3 probePos = gridCoordToPosition(probeGridCoord, consts.probeStart, consts.probeStep);

        vec3 trilinear = mix(1.0 - alpha, alpha, offset);
        float weight = 1.0;

        // Smooth backface test
        {
            vec3 dirToProbe = normalize(probePos - worldSpacePosition);
            // wrap shading
            weight *= square((dot(dirToProbe, worldSpaceNormal) + 1.0) * 0.5) + 0.2;
            // weight *= dot(dirToProbe, worldSpaceNormal);
        }

        // Moment visibility test
        vec3 fromProbeBiased = worldSpacePosition - probePos + (worldSpaceNormal + 3.0 * view) * consts.normalBias;
        vec3 dirFromProbeBiased = normalize(fromProbeBiased);
        {
            vec2 texCoord = textureCoordFromDirection(dirFromProbeBiased, p, consts.depthAtlasSize, consts.depthProbeSize);

            float distToProbeBiased = length(fromProbeBiased);

            vec2 meanVariance = vec2(0.0f);
            if (consts.sampleAtlasIndex == 0) {
                meanVariance = textureLod(sampler2D(texDepthAtlas0, samplerAtlas), texCoord, 0).rg;
            } else if (consts.sampleAtlasIndex == 1) {
                meanVariance = textureLod(sampler2D(texDepthAtlas1, samplerAtlas), texCoord, 0).rg;
            }
            float mean = meanVariance.x;
            float variance = abs(square(meanVariance.x) - meanVariance.y);

            // https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-8-summed-area-variance-shadow-maps#:~:text=8.4%20Variance%20Shadow%20Maps
            // Need the max in the denominator because biasing can cause a negative displacement
            float chebyshevWeight = variance / (variance + square(max(distToProbeBiased - mean, 0.0)));
                
            // Increase contrast in the weight 
            chebyshevWeight = max(chebyshevWeight * chebyshevWeight * chebyshevWeight, 0.0);

            weight *= (distToProbeBiased <= mean) ? 1.0 : chebyshevWeight;
        }

        // crush tiny weight. TODO: what is this doing here
        {
            // Avoid zero weight
            weight = max(1e-5, weight);

            // A tiny bit of light is really visible due to log perception, so
            // crush tiny weights but keep the curve continuous. This must be done
            // before the trilinear weights, because those should be preserved.
            const float crushThreshold = 0.2;
            if (weight < crushThreshold) {
                weight *= weight * weight * (1.0 / square(crushThreshold)); 
            }
        }

        // Trilinear weights
        weight *= trilinear.x * trilinear.y * trilinear.z;

        vec2 texCoord = textureCoordFromDirection(worldSpaceNormal, p, consts.irradAtlasSize, consts.irradProbeSize);
        
        vec3 probeIrradiance = vec3(1.0f, 0.0f, 1.0f); // magenta upon error
        if (consts.sampleAtlasIndex == 0) {
            probeIrradiance = textureLod(sampler2D(texIrradAtlas0, samplerAtlas), texCoord, 0).rgb;
        } else if (consts.sampleAtlasIndex == 1) {
            probeIrradiance = textureLod(sampler2D(texIrradAtlas1, samplerAtlas), texCoord, 0).rgb;
        }
        sumIrradiance += weight * probeIrradiance;
        sumWeight += weight;
    }

    vec3 netIrradiance = sumIrradiance / sumWeight;
     // TODO: // netIrradiance *= energyPreservation;

    // Is the factor 2 necessary?
    // irradiance = vec4(/*2 **/ PI * netIrradiance, 1.0); // this is the true diffuse irradiance. netIrradiance is actually cosine-weighted average radiance


    vec3 albedo = texelFetch(texGAlbedo, pixel, 0).rgb;
    vec2 metallicRoughness = texelFetch(texGMetallicRoughness, pixel, 0).rg;
    float metallic = metallicRoughness.x;
    float perceptualRoughness = metallicRoughness.y;
    float alphaRoughness = perceptualRoughness * perceptualRoughness;
    vec3 BRDFDiff = netIrradiance * (1.0 - metallic) * albedo; // Lambertian BRDF 1/PI factor and netIrradiance PI factor cancel out

    result = vec4(BRDFDiff, 1.0f);
}