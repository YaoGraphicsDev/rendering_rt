#ifndef PROBE_INDEXING_GLSL
#define PROBE_INDEXING_GLSL

#include "math_utils.glsl" //! #include "../common/math_utils.glsl"

ivec3 probeIndexToGridCoord(int index, ivec3 probeCounts) {
    // Slow, but works for any # of probes
    /*
    iPos.x = index % L.probeCounts.x;
    iPos.y = (index % (L.probeCounts.x * L.probeCounts.y)) / L.probeCounts.x;
    iPos.z = index / (L.probeCounts.x * L.probeCounts.y);
    */

    // Assumes probeCounts are powers of two.
    // Saves ~10ms compared to the divisions above
    // Precomputing the MSB actually slows this code down substantially
    ivec3 iPos;
    iPos.x = index & (probeCounts.x - 1);
    iPos.y = (index & ((probeCounts.x * probeCounts.y) - 1)) >> findMSB(probeCounts.x);
    iPos.z = index >> findMSB(probeCounts.x * probeCounts.y);

    return iPos;
}


// convert grid coord to linear id
int gridCoordToProbeIndex(ivec3 probeCoords, ivec3 probeCounts) {
    return probeCoords.x + 
           probeCoords.y * probeCounts.x +
           probeCoords.z * probeCounts.x * probeCounts.y;
}

int atlasTexelBelongsToProbeID(ivec2 texelXY, int probeSize, int atlasWidth) {
    /*  4 texels per probe, padded with 1 border texel on each end. probeSize = 4, probeWithBorderSide = 6
        texelX:     0  1  2  3  4  5  6  7  8  9 10 11 12 13 ...
        content:    O||B  I  I  I  I  B||B  I  I  I  I  B||B ...
        probeID:    0  0  0  0  0  0  1  1  1  1  1  1  2  2 ... (texelXY.x / probeWithBorderSide)
        -2, mod(6): -  -  0  1  2  3  4  5  0  1  2  3  4  5 ... (ivec2((texelXY.x - 2) % probeWithBorderSide)
        octcoord*4: -  - -3 -1  1  3  5  7 -3 -1  1  3  5  7 ... (vec2(octFragCoord) + vec2(0.5f))*(2.0f / float(consts.probeSize)) - vec2(1.0f, 1.0f);
    */
    int probeWithBorderSide = probeSize + 2;
    int probesPerSide = (atlasWidth - 2) / probeWithBorderSide; // number of probes on width side
    return int(texelXY.x / probeWithBorderSide) + probesPerSide * int(texelXY.y / probeWithBorderSide);
}

vec2 normalizedOctCoord(ivec2 texelXY, int probeSize) {
    int probeWithBorderSide = probeSize + 2;
    vec2 octFragCoord = ivec2((texelXY.x - 2) % probeWithBorderSide, (texelXY.y - 2) % probeWithBorderSide);
    // Add back the half pixel to get pixel center normalized coordinates
    return (vec2(octFragCoord) + vec2(0.5f))*(2.0f / float(probeSize)) - vec2(1.0f, 1.0f);
}

vec3 gridCoordToPosition(ivec3 c, vec3 probeStart, vec3 probeStep) {
    return probeStep * vec3(c) + probeStart;
}

vec2 textureCoordFromDirection(vec3 dir, uint probeIndex, ivec2 atlasSize, int probeSideLength) {
    vec2 normalizedOctCoord = octEncode(normalize(dir));
    vec2 normalizedOctCoordZeroOne = (normalizedOctCoord + vec2(1.0f)) * 0.5f;

    // Length of a probe side, plus one pixel on each edge for the border
    float probeWithBorderSide = float(probeSideLength) + 2.0f;

    vec2 octCoordNormalizedToTextureDimensions = (normalizedOctCoordZeroOne * probeSideLength) / vec2(atlasSize);

    int probesPerRow = (atlasSize.x - 2) / int(probeWithBorderSide);

    // Add (2,2) back to texCoord within larger texture. Compensates for 1 pix 
    // border around texture and further 1 pix border around top left probe.
    // We're looking at corner, not pixel center. Aligns with normalizedOctCoord() convention
    vec2 probeTopLeftPosition = vec2(
        float(probeIndex % probesPerRow) * probeWithBorderSide,
        float(probeIndex / probesPerRow) * probeWithBorderSide) + vec2(2.0f, 2.0f);

    vec2 normalizedProbeTopLeftPosition = vec2(probeTopLeftPosition) / vec2(atlasSize);

    return vec2(normalizedProbeTopLeftPosition + octCoordNormalizedToTextureDimensions);
}

#endif