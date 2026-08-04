#ifndef IRRADIANCE_FIELD_GLSL
#define IRRADIANCE_FIELD_GLSL

vec3 sampleDDGI(vec3 position, vec3 normal, vec3 view, DDGIParams params, float normalBias, texture2D irradAtlas, texture2D depthAtlas) {
    ivec3 baseGridCoord = baseGridCoord(position);
    if (any(greaterThanEqual(baseGridCoord, params.probeCounts - ivec3(1)))) {
        result = vec4(vec3(0.0f), 1.0f);
        return vec3(0.0);
    }
    if (any(lessThan(baseGridCoord, ivec3(0)))) {
        result = vec4(vec3(0.0f), 1.0f);
        return vec3(0.0);
    }

    vec3 baseProbePos = gridCoordToPosition(baseGridCoord, params.probeStart, params.probeStep);
    vec3 sumIrradiance = vec3(0.0);
    float sumWeight = 0.0;

    // alpha is how far from the floor(currentVertex) position. on [0, 1] for each axis.
    vec3 alpha = clamp((position - baseProbePos) / params.probeStep, vec3(0.0), vec3(1.0));

    for (int i = 0; i < 8; ++i) {
        // Compute the offset grid coord and clamp to the probe grid boundary
        // Offset = 0 or 1 along each axis
        ivec3 offset = ivec3(i, i >> 1, i >> 2) & ivec3(1);
        ivec3 probeGridCoord = clamp(baseGridCoord + offset, ivec3(0), ivec3(params.probeCounts - 1));
        int p = gridCoordToProbeIndex(probeGridCoord, params.probeCounts);

        vec3 probePos = gridCoordToPosition(probeGridCoord, params.probeStart, params.probeStep);

        vec3 trilinear = mix(1.0 - alpha, alpha, offset);
        float weight = 1.0;

        // Smooth backface test
        {
            vec3 dirToProbe = normalize(probePos - position);
            // wrap shading
            weight *= square((dot(dirToProbe, normal) + 1.0) * 0.5) + 0.2;
            // weight *= dot(dirToProbe, worldSpaceNormal);
        }

        // Moment visibility test
        vec3 fromProbeBiased = position - probePos + (normal + 3.0 * view) * normalBias;
        vec3 dirFromProbeBiased = normalize(fromProbeBiased);
        {
            vec2 texCoord = textureCoordFromDirection(dirFromProbeBiased, p, params.depthAtlasSize, params.depthProbeSize);

            float distToProbeBiased = length(fromProbeBiased);

            vec2 meanVariance = textureLod(sampler2D(depthAtlas, samplerAtlas), texCoord, 0).rg;
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

        vec2 texCoord = textureCoordFromDirection(normal, p, params.irradAtlasSize, params.irradProbeSize);
        
        vec3 probeIrradiance = textureLod(sampler2D(irradAtlas, samplerAtlas), texCoord, 0).rgb; // magenta upon error
        sumIrradiance += weight * probeIrradiance;
        sumWeight += weight;
    }

    // this is actually cosine-weighted average radiance
    vec3 avgRadiance = sumIrradiance / sumWeight; // = irradiance / PI
    return avgRadiance;
     // TODO: // netIrradiance *= energyPreservation;
}


#endif