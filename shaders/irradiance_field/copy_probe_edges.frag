#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(location = 0) out vec4 dstAtlas0;
layout(location = 1) out vec4 dstAtlas1;

layout (push_constant) uniform PushConstants {
    int probeSize;  
    ivec2 atlasSize; // width, height
    uint srcAtlasIndex; // 0, 1
} consts;

// Set 3: Managed by framegraph
//  binding 0 - 15:     textures
//  binding 16 - 23:    SSBO
//  binding 24 - 31:    storage image
layout(set = 3, binding = 0) uniform texture2D texSrcAtlas0;
layout(set = 3, binding = 1) uniform texture2D texSrcAtlas1;

const int border = 2;

vec4 fetchProbe(ivec2 tileBase, ivec2 local) {
    if (consts.srcAtlasIndex == 0) {
        return texelFetch(texSrcAtlas0, tileBase + local, 0);
    } else if (consts.srcAtlasIndex == 1) {
        return texelFetch(texSrcAtlas1, tileBase + local, 0);
    } else {
        return vec4(0.0);
    }
}

vec4 averageOctEdgeCorners(ivec2 tileBase, int N) {
    // The four interior oct-map corners.
    // All correspond to directions near -Z after oct folding.
    vec4 c0 = fetchProbe(tileBase, ivec2(1, 1));
    vec4 c1 = fetchProbe(tileBase, ivec2(N, 1));
    vec4 c2 = fetchProbe(tileBase, ivec2(1, N));
    vec4 c3 = fetchProbe(tileBase, ivec2(N, N));

    return 0.25 * (c0 + c1 + c2 + c3);
}

ivec2 octBorderSource(ivec2 local, int N) {
    // local is in [0, N + 1]
    // 0 and N+1 are border pixels.
    ivec2 s = local;

    bool left   = local.x == 0;
    bool right  = local.x == N + 1;
    bool top    = local.y == 0;
    bool bottom = local.y == N + 1;

    // Non-corner borders.
    if (left) {
        s.x = 1;
        s.y = N + 1 - local.y;
    }
    else if (right) {
        s.x = N;
        s.y = N + 1 - local.y;
    }

    if (top) {
        s.y = 1;
        s.x = N + 1 - s.x;
    }
    else if (bottom) {
        s.y = N;
        s.x = N + 1 - s.x;
    }

    return clamp(s, ivec2(1), ivec2(N));
}

// void main()
// {
// 	/*  4 texels per probe, padded with 1 border texel on each end. probeSize = 4, probeSize + border = 6
//         fragCoordX:	0  1  2  3  4  5  6  7  8  9 10 11 12 13 ...
//         content:    O||B  I  I  I  I  B||B  I  I  I  I  B||B ...
//         mod and op:	0  1<-2  3  4  5->0  1<-2  3  4  5->0  1<- ... (texelXY.x / (probeSize + border))
//     */
// 
// 	ivec2 P = ivec2(gl_FragCoord.xy);
// 
// 	if (P.x == 0 || P.y == 0 || P.x == (consts.atlasSize.x - 1) || P.y == (consts.atlasSize.y - 1)) discard;
// 
// 	if (P.x % (consts.probeSize + border) == 0) { dstAtlas = texelFetch(texSrcAtlas, ivec2(P.x-1, P.y), 0).rgba; return;}
// 	if (P.x % (consts.probeSize + border) == 1) { dstAtlas = texelFetch(texSrcAtlas, ivec2(P.x+1, P.y), 0).rgba; return;}
// 																											
// 	if (P.y % (consts.probeSize + border) == 0) { dstAtlas = texelFetch(texSrcAtlas, ivec2(P.x, P.y-1), 0).rgba; return;}
// 	if (P.y % (consts.probeSize + border) == 1) { dstAtlas = texelFetch(texSrcAtlas, ivec2(P.x, P.y+1), 0).rgba; return;}
// 
// 	dstAtlas = texelFetch(texSrcAtlas, ivec2(P.x, P.y), 0).rgba;
// }

void main()
{
    ivec2 P = ivec2(gl_FragCoord.xy);

    if (P.x == 0 || P.y == 0 || P.x == (consts.atlasSize.x - 1) || P.y == (consts.atlasSize.y - 1)) discard;

    int N = consts.probeSize;
    int tileSide = N + 2;

    // remove global atlas border.
    ivec2 q = P - ivec2(1);

    ivec2 tileCoord = q / tileSide;
    ivec2 local = q - tileCoord * tileSide; // [0, N + 1]

    ivec2 tileBase = ivec2(1) + tileCoord * tileSide;

    bool left   = local.x == 0;
    bool right  = local.x == N + 1;
    bool top    = local.y == 0;
    bool bottom = local.y == N + 1;

    bool isBorder = left || right || top || bottom;
    bool isCorner = (left || right) && (top || bottom);

    vec4 result = vec4(0.0);
    if (isCorner) {
        result = averageOctEdgeCorners(tileBase, N);
    }
    else if (isBorder) {
        ivec2 srcLocal = octBorderSource(local, N);
        result = fetchProbe(tileBase, srcLocal);
    }
    else {
        result = fetchProbe(tileBase, local);
    }

    if (consts.srcAtlasIndex == 0) {
        dstAtlas1 = result;
    } else if (consts.srcAtlasIndex == 1) {
        dstAtlas0 = result;
    } else {
        dstAtlas0 = vec4(1.0f, 0.0f, 1.0f, 1.0f);
        dstAtlas1 = vec4(1.0f, 0.0f, 1.0f, 1.0f);
    }
}