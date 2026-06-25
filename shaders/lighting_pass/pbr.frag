#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLit;

layout (push_constant) uniform PushConstants {
    vec4 projEncoded;
    vec4 viewBaseQuat;
    vec3 camPos;
    uint nLights;
    uvec3 nClusters;
    vec2 zRangeAbs; // (zNear, zFar), should be positive
} consts;

const uint MAX_LIGHTS = 64;
const uint LIGHT_TYPE_POINT = 0;
const uint LIGHT_TYPE_DIR   = 1;
const uint LIGHT_TYPE_AREA  = 2;
const float LCT_LUT_SIZE  = 64.0;
const float LCT_LUT_SCALE = (LCT_LUT_SIZE - 1.0) / LCT_LUT_SIZE;
const float LCT_LUT_BIAS  = 0.5 / LCT_LUT_SIZE;

// layout(std430, set = 3, binding = 18) buffer LightBSCountBuffer { // atomicAdd() operation. Cant have writeonly keyword
//     Count visibleLightIdCount[]; // only one element, set to zero
// };

struct Light {
    uint type; // 0 -- point, 1 -- directional, 2 -- area
    float intensity;
    vec3 color;
    vec3 center; // in world space

    // Doesnt guarantee right-handedness
    vec3 direction;
    vec3 planeBasisX;
    vec3 planeBasisY; 
    vec2 halfDims;    // area light exclusive (half width, half height)
    float influenceDistance; // bounding sphere radius
};
layout(std430, set = 0, binding = 0) buffer readonly LightBuffer {
    Light lights[];
};

// layout (set = 0, binding = 0) uniform LightUBO {
//     uint type; // 0 -- point, 1 -- directional, 2 -- area
//     float intensity;
//     vec3 color;
//     uint nVertices; // non-zero for area light only
//     /*
//         point:          pose[0] -- position
//         directional:    pose[0] -- direction
//         area:           pose[]  -- polygon vertices. In world space
//     */
//     vec3 pose[MAX_AREA_LIGHT_VERTICES_COUNT];
// } lUbos[];

struct Cascade {
    float zBegin;
    float zEnd;
    mat4 lightSpaceView;
    mat4 lightSpaceProject;
};

const uint MAX_SHADOW_CASCADES = 4;
struct CascadedShadow {
    float blendDepth;
    uint nCascades;
    uint resolution;
    Cascade cascades[MAX_SHADOW_CASCADES];
};

struct ShadowJitter {
    vec2 nTiles;
    uint nStrataPerDim;
    float radius;
};

layout(set = 0, binding = 1) uniform ShadowUBO {
    // cascaded
    // allow only 1 directional light to cast cascaded shadow. -1 disables cascaded shadow
    int dLightId; 
    CascadedShadow cascadedShadow;

    // TODO: point/area light shadow

    // pcf shadow jitter
    ShadowJitter shadowJitter;
} sUbo;

layout(set = 0, binding = 2) uniform sampler2D samplerLTCParams[2];    // linear
layout(set = 0, binding = 3) uniform sampler samplerGBuffer;       // nearest
layout(set = 0, binding = 4) uniform sampler samplerShadowMap;      // linear
layout(set = 0, binding = 5) uniform sampler3D samplerShadowJitter; // nearest, address repeat

// Set 3: Managed by framegraph
//  binding 0 - 15:     textures
//  binding 16 - 23:    SSBO
layout(set = 3, binding = 0) uniform texture2D texDepth;
layout(set = 3, binding = 1) uniform texture2D texAlbedo;
layout(set = 3, binding = 2) uniform texture2D texNormal;
layout(set = 3, binding = 3) uniform texture2D texMetallicRoughness;
layout(set = 3, binding = 4) uniform texture2D texEmissive;
layout(set = 3, binding = 5) uniform utexture2D texMatFlags; //Currently unused. Currently only LSB in use: 0 -- lit, 1 -- unlit. Other bits available
layout(set = 3, binding = 6) uniform texture2DArray texCascadedShadow;
#define MAX_LIGHT_PER_CLUSTER 32
struct LightAssignment {
    uint nLights;
    uint lightIds[MAX_LIGHT_PER_CLUSTER];
};
layout(std430, set = 3, binding = 16) readonly buffer LightAssignmentBuffer {
    LightAssignment lightAssignments[];
}; 

#include "math_utils.glsl" //! #include "../common/math_utils.glsl" // replacement comment for visual studio glsl extension

uint findLightCluster(vec2 screenUV, float viewZ) {
    uvec2 clusterXY = uvec2(screenUV * vec2(consts.nClusters.xy));
    float camZNear = consts.zRangeAbs.x;
    float camZFar = consts.zRangeAbs.y;
    uint clusterZ = uint((log(abs(viewZ) / camZNear) / log(camZFar / camZNear)) * consts.nClusters.z);
    uint clusterId = clusterXY.x + (clusterXY.y * consts.nClusters.x) + (clusterZ * consts.nClusters.x * consts.nClusters.y);
    return clusterId;
}

// GGX
float D(vec3 n, vec3 h, float alpha) {
    // TODO: clamp alpha to [1E-3, 1.0]

    // GGX or Trowbridge-Reitz
    // RTR4 (Real-Time Rendering 4th Edition) book (9.41). Ignore Disney parameter convention
    float a2 = alpha * alpha;
    float nh = clamp(dot(n, h), 0.0, 1.0);
    float d = 1.0 + nh * nh * (a2 - 1.0);
    return a2 / (PI * d * d);
}

float Lambda(vec3 n, vec3 s, float alpha) {
    // GGX shape invariant
    // RTR4 (9.42) (9.37)
    float c = clamp(dot(n, s), 0.0f, 1.0f); // cosine
    float c2 = c * c;
    float s2 = 1.0 - c2; // sine^2
    float inv_a2 = (alpha * alpha * s2) / (c2 + 1E-10);
    float La = (-1.0 + sqrt(1.0 + inv_a2)) * 0.5;
    return La;
}

float G2(vec3 n, vec3 v, vec3 l, float alpha) {
    // smith height correlated shadow masking
    // RTR4 (9.31)
    // TODO: if (dot(n, v) <= 0.0 || dot(n, l) <= 0.0)
    float Lv = Lambda(n, v, alpha);
    float Ll = Lambda(n, l, alpha);
    return 1.0 / (1.0 + Lv + Ll);
}

vec3 F(vec3 h, vec3 l, vec3 F0) {
    // Fresnel Schlick approximation
    float c = clamp(dot(h, l), 0.0, 1.0);
    return F0 + (1.0 - F0) * pow(clamp(1.0 - c, 0.0, 1.0), 5.0);
}

float BRDFGeometry(vec3 v, vec3 l, vec3 h, vec3 n, float roughness) {
    // BRDF excluding fresnel term.
    // RTR4 (9.26) with perfect mirror microfacet assumption. (9.34)
    float D = D(n, h, roughness);
    float G = G2(n, v, l, roughness);
    float d = 4.0 * clamp(dot(n, v), 0.0, 1.0) * clamp(dot(n, l), 0.0, 1.0) + 1E-5;
    return D * G / d;
}

// Filament https://google.github.io/filament/Filament.md.html#listing_glslpunctuallight
float squareFalloffAttenuation(float distanceSquare, float lightInvRadius) {
    float factor = distanceSquare * lightInvRadius * lightInvRadius;
    float smoothFactor = max(1.0 - factor * factor, 0.0);
    return (smoothFactor * smoothFactor) / max(distanceSquare, 1e-4);
}

// LTC. LUTs sampled and generated in a manner consistent with GGX choices listed above
vec3 integrateEdgeVec(vec3 v1, vec3 v2) {
    float x = dot(v1, v2);
    float y = abs(x);

    float a = 0.8543985 + (0.4965155 + 0.0145206*y)*y;
    float b = 3.4175940 + (4.1616724 + y)*y;
    float v = a / b;

    float theta_sintheta = (x > 0.0) ? v : 0.5*inversesqrt(1.0 - x * x) - v; // this thing here fits 1/(2pi) * theta/sin(theta)

    return cross(v1, v2)*theta_sintheta; // normalized form factor of an edge. The term 'form factor' in this context is a vector, unlike its conventional scalar counterpart
}

vec3 findTangent(vec3 n, vec3 v) {
    vec3 t = v - n * dot(v, n);

    if (dot(t, t) < 1e-6) {
        vec3 up = abs(n.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
        t = normalize(cross(up, n));
    }
    else {
        t = normalize(t);
    }

    return t;
}

#define MAX_AREA_LIGHT_VERTICES_COUNT 8
vec3 ltcEvaluate(vec3 n, vec3 v, vec3 p, mat3 invM, vec3 light_verts[MAX_AREA_LIGHT_VERTICES_COUNT], uint n_vertices) {
    vec3 t1 = findTangent(n, v);
    vec3 t2 = cross(n, t1);
    invM = invM * transpose(mat3(t1, t2, n)); // includes a world space -> fragment local space transform

    vec3 formFactorVec = vec3(0.0f);
    
    vec3 light_dir = light_verts[0] - p;
    vec3 light_normal = cross(light_verts[1] - light_verts[0], light_verts[n_vertices - 1] - light_verts[0]);
    bool behind = (dot(light_dir, light_normal) >= 0.0);

    if (behind) {
        return vec3(0.0f);
    }

    // conceptually, these for loops, if-else will not break wavefronts
    for (uint v = 0; v < MAX_AREA_LIGHT_VERTICES_COUNT; ++v) {
        if (v >= n_vertices) {
            break;
        }
        light_verts[v] = normalize(invM * (light_verts[v] - p));
    }

    for (uint v = 0; v < MAX_AREA_LIGHT_VERTICES_COUNT; ++v) {
        if (v >= n_vertices) {
            break;
        }
        if (v == n_vertices - 1) { // last light vertex
            formFactorVec += integrateEdgeVec(light_verts[n_vertices - 1], light_verts[0]);
        } else {
            formFactorVec += integrateEdgeVec(light_verts[v], light_verts[v + 1]);
        }
    }

    formFactorVec = -formFactorVec; // since the winding direction of vertices is treated as the 'front' of the light, the sum will come out negative

    // proxy sphere projection
    float formFactor = length(formFactorVec);           // angular extent -- texture v
    float elevation = formFactorVec.z / formFactor;     // elevation angle cosine -- texture u

    vec2 uv = vec2(elevation * 0.5f + 0.5f, formFactor);
    uv = uv * LCT_LUT_SCALE + LCT_LUT_BIAS; // 0.0 -- first texel center.  1.0 -- last texel center

    // the scale that approximates clipping
    float scale = texture(samplerLTCParams[1], uv).w;
    formFactor *= scale;
    vec3 lo_i = vec3(formFactor);
    return lo_i;
}



// layout(set = 0, binding = 4) uniform sampler samplerShadowMap;      // linear
// 
// layout(set = 3, binding = 6) uniform texture2DArray texCascadedShadow;



// x: 0.0 -- in shadow, 1.0 -- not in shadow
// y: valid when x = 0.0, positive blocker distance
vec2 arrayShadowCompare(
    texture2DArray texShadow,
    sampler samp,
    uint layer,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir)
{
    vec4 clip = lightProject * lightSpaceCoord;
    vec3 ndc = clip.xyz / clip.w;

    vec2 shadowUV = ndc.xy * 0.5 + 0.5;
    float receiverDepth = ndc.z;

    float cosTheta = clamp(dot(normal, -lightDir), 0.0, 1.0);
    float bias = max(0.0005 * (1.0 - cosTheta), 0.0001);

    float sampleDepth =
        texture(sampler2DArray(texShadow, samp), vec3(shadowUV, layer)).r;

    if (sampleDepth < receiverDepth - bias)
        return vec2(0.0, sampleDepth); // blocker depth

    return vec2(1.0, 0.0);
}


float pcssShadowFactor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir) {
    /////////
    mat4 invLightProject = mat4(1.0);
    invLightProject[0][0] = 1.0 / lightProject[0][0];
    invLightProject[1][1] = 1.0 / lightProject[1][1];
    invLightProject[2][2] = 1.0 / lightProject[2][2];
    invLightProject[3][2] = -lightProject[3][2] / lightProject[2][2];
    /////////

    vec4 clip = lightProject * lightSpaceCoord;
    vec3 ndc = clip.xyz / clip.w;

    vec2 shadowUV = ndc.xy * 0.5 + 0.5;
    float receiverDepth = ndc.z;

    vec2 shadowMapSize = vec2(textureSize(sampler2DArray(texCascadedShadow, samplerShadowMap), 0).xy);
    float texel = 1.0 / shadowMapSize.x;

    float cosTheta = clamp(dot(normal, -lightDir), 0.0, 1.0);
    float bias = max(texel * (1.0 - cosTheta), texel * 0.3);

    uint nStrata = sUbo.shadowJitter.nStrataPerDim;
    vec2 nTiles = sUbo.shadowJitter.nTiles;

    uint nPairs = (nStrata * nStrata) / 2;
    float jitterStepW = 1.0 / float(nPairs);

    float searchRadiusUV = 4.0 * texel;
    // float minFilterRadiusUV = 1.0 * texel;
    // float maxFilterRadiusUV = 8.0 * texel;

    float viewWidthToUV = lightProject[0][0] * 0.5;
    // Tune this. Start small.
    float lightAngle = 0.526 * PI / 180.0;
    // float lightSizeUV = 0.01;

    // ---------- blocker search ----------

    vec3 jitterUVW = vec3(inUV * nTiles, 0.0);

    float blockerDepthSum = 0.0;
    uint blockerCount = 0;

    for (uint i = 0; i < nPairs; ++i)
    {
        vec4 jitter = texture(samplerShadowJitter, jitterUVW);
        jitterUVW.z += jitterStepW;

        vec2 uv0 = shadowUV + jitter.xy * searchRadiusUV;
        vec2 uv1 = shadowUV + jitter.zw * searchRadiusUV;

        float d0 = texture(
            sampler2DArray(texCascadedShadow, samplerShadowMap),
            vec3(uv0, targetCascade)).r;

        float d1 = texture(
            sampler2DArray(texCascadedShadow, samplerShadowMap),
            vec3(uv1, targetCascade)).r;

        if (d0 < receiverDepth - bias) {
            blockerDepthSum += ndcToView(vec4(uv0 * 2.0f - 1.0f, d0, 1.0f), invLightProject).z;
            // blockerDepthSum += d0;
            blockerCount++;
        }

        if (d1 < receiverDepth - bias) {
            blockerDepthSum += ndcToView(vec4(uv1 * 2.0f - 1.0f, d1, 1.0f), invLightProject).z;
            // blockerDepthSum += d1;
            blockerCount++;
        }
    }

    if (blockerCount == 0)
        return 1.0;


    float avgBlockerDepth = blockerDepthSum / float(blockerCount);

    // ---------- penumbra ----------

    // float blockerDistance = max(receiverDepth - avgBlockerDepth, 0.0);
    float blockerDistance = max(avgBlockerDepth - lightSpaceCoord.z, 0.0);

    // float filterRadiusUV = clamp(
    //      // blockerDistance * lightSizeUV,
    //      blockerDistance * lightAngle * viewWidthToUV,
    //      minFilterRadiusUV,
    //      maxFilterRadiusUV);
    blockerDistance = 5.0 * max(blockerDistance / sqrt(blockerDistance * blockerDistance + 2), 0.2);
    float filterRadiusUV = blockerDistance * lightAngle * 0.5 * viewWidthToUV;

    // ---------- PCF ----------

    jitterUVW = vec3(inUV * nTiles, 0.0);

    float visibility = 0.0;
    uint sampleCount = 0;

    for (uint i = 0; i < nPairs; ++i)
    {
        vec4 jitter = texture(samplerShadowJitter, jitterUVW);
        jitterUVW.z += jitterStepW;

        vec2 uv0 = shadowUV + jitter.xy * filterRadiusUV;
        vec2 uv1 = shadowUV + jitter.zw * filterRadiusUV;

        float d0 = texture(
            sampler2DArray(texCascadedShadow, samplerShadowMap),
            vec3(uv0, targetCascade)).r;

        float d1 = texture(
            sampler2DArray(texCascadedShadow, samplerShadowMap),
            vec3(uv1, targetCascade)).r;

        visibility += d0 < receiverDepth - bias ? 0.0 : 1.0;
        visibility += d1 < receiverDepth - bias ? 0.0 : 1.0;

        sampleCount += 2;
    }

    return visibility / float(sampleCount);
}


// Why is my implementation not correct
/*
float pcssShadowFactor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir) {

    uint nStrata = sUbo.shadowJitter.nStrataPerDim;
    vec2 nTiles = sUbo.shadowJitter.nTiles;
    float jitterRadius = sUbo.shadowJitter.radius;

    uint nJitterSample = (nStrata * nStrata) / 2;
    uint nTestJitterSample = nStrata / 2;

    float jitterStepW = 1.0 / float(nJitterSample);

    vec3 jitterUVW = vec3(inUV * nTiles, 0.0);
    float shadowFactor = 0.0;

    float blockerDistance = 0.0;
    float blockedCount = 0;

    // quick test to see if fully in light or shadow
    for (uint i = 0; i < nTestJitterSample; ++i) {
        vec4 jitter = texture(samplerShadowJitter, jitterUVW);
        jitterUVW.z += jitterStepW;

        vec2 f0 = arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.xy * 0.002, 0.0, 0.0), lightProject, normal, lightDir);
        if (f0.x < 0.0005) { // in shadow
            blockerDistance += f0.y;
            ++blockedCount;
        }
        vec2 f1 = arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.zw * 0.002, 0.0, 0.0), lightProject, normal, lightDir);
        if (f1.x < 0.0005) { // in shadow
            blockerDistance += f1.y;
            ++blockedCount;
        }
        shadowFactor += f0.x;
        shadowFactor += f1.x;
    }
    
    float testAvg = shadowFactor / float(nStrata);
    if (testAvg < 0.0005 || testAvg > 0.9995) {
        return testAvg; // fully in shadow or light
    }
    
    // Reset shadowFactor, reuse jitterUVW.z, and continue with full sampling
    shadowFactor = testAvg * float(nStrata);
    blockerDistance /= float(blockedCount);

    for (uint i = 0; i < nJitterSample; ++i) {
        vec4 jitter = texture(samplerShadowJitter, jitterUVW);
        jitterUVW.z += jitterStepW;

        shadowFactor += arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.xy * clamp(0.01 * blockerDistance / 40.0, 0.0005, 0.004), 0.0, 0.0), lightProject, normal, lightDir).x;
        shadowFactor += arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.zw * clamp(0.01 * blockerDistance / 40.0, 0.0005, 0.004), 0.0, 0.0), lightProject, normal, lightDir).x;
    }

    return shadowFactor / float(nStrata * nStrata);
}*/

/*
float pcfShadowFactor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir) {

    uint nStrata = sUbo.shadowJitter.nStrataPerDim;
    vec2 nTiles = sUbo.shadowJitter.nTiles;
    float jitterRadius = sUbo.shadowJitter.radius;

    uint nJitterSample = (nStrata * nStrata) / 2;
    uint nTestJitterSample = nStrata / 2;

    float jitterStepW = 1.0 / float(nJitterSample);

    vec3 jitterUVW = vec3(inUV * nTiles, 0.0);
    float shadowFactor = 0.0;

    // quick test to see if fully in light or shadow
    for (uint i = 0; i < nTestJitterSample; ++i) {
        vec4 jitter = texture(samplerShadowJitter, jitterUVW);
        jitterUVW.z += jitterStepW;
    
        shadowFactor += arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        shadowFactor += arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
    }
    
    float testAvg = shadowFactor / float(nStrata);
    if (testAvg < 0.0005 || testAvg > 0.9995) {
        return testAvg; // fully in shadow or light
    }
    
    // Reset shadowFactor, reuse jitterUVW.z, and continue with full sampling
    shadowFactor = testAvg * float(nStrata);

    for (uint i = 0; i < nJitterSample; ++i) {
        vec4 jitter = texture(samplerShadowJitter, jitterUVW);
        jitterUVW.z += jitterStepW;

        shadowFactor += arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        shadowFactor += arrayShadowCompare(texCascadedShadow, samplerShadowMap, targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
    }

    return shadowFactor / float(nStrata * nStrata);
}
*/

// 0.0 -- in shadow, 1.0 -- not in shadow
// TODO: color debug, return vec3
float cascadedShadowFactor(float zView, vec4 worldSpaceCoord, vec3 normal, vec3 lightDir){
    zView = abs(zView);
    uint targetCascade = 0;
    for (uint c = 0; c < MAX_SHADOW_CASCADES; ++c) { // unroll friendly
        if (c >= sUbo.cascadedShadow.nCascades) {
            break;
        }

        if (sUbo.cascadedShadow.cascades[c].zBegin < zView && sUbo.cascadedShadow.cascades[c].zEnd >= zView) {
            targetCascade = c;
            break;
        }
    }
    
    vec4 lightSpaceCoord = sUbo.cascadedShadow.cascades[targetCascade].lightSpaceView * worldSpaceCoord;

    // float shadowFactor = pcfShadowFactor(
    //                             targetCascade,
    //                             lightSpaceCoord,
    //                             sUbo.cascadedShadow.cascades[targetCascade].lightSpaceProject,
    //                             normal,
    //                             lightDir);
    float shadowFactor = pcssShadowFactor(
                                 targetCascade,
                                 lightSpaceCoord,
                                 sUbo.cascadedShadow.cascades[targetCascade].lightSpaceProject,
                                 normal,
                                 lightDir);

    return shadowFactor;
}

/*
// 0.0 -- in shadow, 1.0 -- not in shadow
// parameters should all be in view space
float contactShadowTrace(vec4 viewSpaceCoord, float maxDistance, uint steps, vec3 direction) {    
    float stepSize = maxDistance / float(steps);
    for (uint s = 0; s < steps; ++s) {
        vec3 viewSpaceRay = vec3(viewSpaceCoord) + direction * stepSize * (s + 1);
        vec4 clipSpaceRay = projMult(consts.projEncoded, vec4(viewSpaceRay, 1.0));
        vec3 ndcRay = clipSpaceRay.xyz / clipSpaceRay.w;
        // float depthP = ndcP.z;
        vec2 uvRay = (ndcRay.xy + vec2(1.0)) * vec2(0.5);
        if (any(lessThan(uvRay, vec2(0.0))) || any(greaterThan(uvRay, vec2(1.0)))) {
            return 1.0;
        }
        // float depthScene = texture(sampler2D(texDepth, samplerGBuffer), uv).r;
        vec4 ndcSceneP = vec4(ndcRay.xy, texture(sampler2D(texDepth, samplerGBuffer), uvRay).r, 1.0);
        vec4 viewSpaceSceneP = ndcToView(ndcSceneP, consts.projEncoded);
        // float depthScene = texture(sampler2D(texDepth, samplerGBuffer), uv).r;
        float depthDelta = viewSpaceSceneP.z - viewSpaceRay.z;
        if (depthDelta > 0.0 && depthDelta < 0.2) {
            return 0.0;
        }
    }
    return 1.0;
}
*/
 
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    // world position
    float depth = texelFetch(sampler2D(texDepth, samplerGBuffer), pixel, 0).r;
    vec4 ndc = vec4(inUV * 2.0f - 1.0f, depth, 1.0f);
    vec4 viewSpaceCoord = ndcToView(ndc, consts.projEncoded);
    vec4 worldSpaceCoord = viewInvMult(consts.viewBaseQuat, consts.camPos, viewSpaceCoord);

    vec3 viewDir = normalize(consts.camPos - worldSpaceCoord.xyz);

    // uint clusterId = findLightCluster(inUV, viewSpaceCoord.z);
    // outLit = vec4(lightAssign[clusterId].nLights, 0.0, 0.0, 1.0);
    // vec3 albedo = texelFetch(sampler2D(texAlbedo, samplerGBuffer), pixel, 0).xyz;
    // outLit = vec4(clusterCoordToColor(clusterId) * albedo, 1.0);



    ///////////////// temp: test light clustering
    // vec3 albedo = texelFetch(sampler2D(texAlbedo, samplerGBuffer), pixel, 0).xyz;
    // outLit = vec4(albedo, 1.0f);
    // // draw cluster
    // uint clusterId = findLightCluster(inUV, viewSpaceCoord.z);
    // for (uint i = 0; i < lightAssign[clusterId].nLights; ++i) {
    //     uint lightId = lightAssign[clusterId].lightIds[i];
    //     outLit *= vec4(lights[lightId].color, 1.0f);
    // }
    // 
    // for (uint i = 0; i < lightAssign[clusterId].nLights; ++i) {
    //     // draw actual light influence
    //     uint lightId = lightAssign[clusterId].lightIds[i];
    //     vec3 d  = worldSpaceCoord.xyz - lights[lightId].center;
    //     if (length(d) <= lights[lightId].influenceDistance) {
    //         if (lights[lightId].type == LIGHT_TYPE_AREA) {
    //             if (dot(d, lights[lightId].lightDirection) >= 0.0) {
    //                 outLit *= vec4(lights[lightId].color, 1.0f);
    //             }
    //         } else {
    //             outLit *= vec4(lights[lightId].color, 1.0f);
    //         }
    //     }
    // }
    
    vec3 normal = texelFetch(sampler2D(texNormal, samplerGBuffer), pixel, 0).xyz;
    normal = normalize(normal);

    /*
    float zView = -viewSpaceCoord.z;
    uint targetCascade = 0;
    for (uint i = 0; i < sUbo.cascadedShadow.nCascades; ++i) {
        if (sUbo.cascadedShadow.cascades[i].zBegin < zView && sUbo.cascadedShadow.cascades[i].zEnd >= zView) {
            targetCascade = i;
            break;
        }
    }

    vec4 lightSpaceCoord0 = sUbo.cascadedShadow.cascades[targetCascade].lightSpaceView * worldSpaceCoord;
    float shadowFactor0 = pcf_shadow_factor(
                            targetCascade,
                            lightSpaceCoord0,
                            sUbo.cascadedShadow.cascades[targetCascade].lightSpaceProject,
                            normal,
                            -normalize(lUbos[nonuniformEXT(sUbo.dLightId)].pose[0]));

    float shadowFactor = shadowFactor0;
    // check if cascade blending is required
    if (sUbo.cascadedShadow.cascades[targetCascade].zEnd - zView < sUbo.cascadedShadow.blendDepth &&
        targetCascade < sUbo.cascadedShadow.nCascades - 1) {

		vec4 lightSpaceCoord1 = sUbo.cascadedShadow.cascades[targetCascade + 1].lightSpaceView * worldSpaceCoord;
        float shadowFactor1 = pcf_shadow_factor(
                            targetCascade + 1,
                            lightSpaceCoord1,
                            sUbo.cascadedShadow.cascades[targetCascade + 1].lightSpaceProject,
                            normal,
                            -normalize(lUbos[nonuniformEXT(sUbo.dLightId)].pose[0]));
        float blendFactor = clamp(1.0f - (sUbo.cascadedShadow.cascades[targetCascade].zEnd - zView) / sUbo.cascadedShadow.blendDepth, 0.0f, 1.0f);
        shadowFactor = mix(shadowFactor0, shadowFactor1, smoothstep(0.0f, 1.0f, blendFactor));
    }

    // TODO: temp, tranparent shadow to mimic GI
    shadowFactor = clamp(shadowFactor, 0.05f, 1.0f);
    */


    vec3 albedo = texelFetch(sampler2D(texAlbedo, samplerGBuffer), pixel, 0).xyz;
    vec2 metallicRoughness = texelFetch(sampler2D(texMetallicRoughness, samplerGBuffer), pixel, 0).xy;
    float metallic = metallicRoughness.x;
    float perceptualRoughness = metallicRoughness.y;
    float alphaRoughness = perceptualRoughness * perceptualRoughness;
    vec3 emissive = texelFetch(sampler2D(texEmissive, samplerGBuffer), pixel, 0).xyz;
    uint materialFlags = texelFetch(usampler2D(texMatFlags, samplerGBuffer), pixel, 0).x;
    // bool unlit = ((materialFlags & 1u) == 1);

    vec3 litColor = vec3(0.0);

    // lighting in clusters
    uint clusterId = findLightCluster(inUV, viewSpaceCoord.z);
    for (uint i = 0; i < lightAssignments[clusterId].nLights; ++i) {
        uint lightId = lightAssignments[clusterId].lightIds[i];
        Light light = lights[lightId];
        if (light.type == LIGHT_TYPE_POINT){
            vec3 l = light.center - worldSpaceCoord.xyz;
            float dl2 = dot(l, l); // light distance ^ 2
            if (dl2 > light.influenceDistance * light.influenceDistance) {
                continue;
            }
            float dlInv = inversesqrt(max(dl2, 1E-10));
            float dl = dl2 * dlInv;
            l *= dlInv; // normalized light direction
            vec3 h = normalize(viewDir + l);
            vec3 F0 = vec3(0.04); 
            F0 = mix(F0, albedo, metallic);
            vec3 F = F(h, l, F0);
            vec3 BRDFSpec = F * BRDFGeometry(viewDir, l, h, normal, alphaRoughness);
            vec3 BRDFDiff = (1.0 - metallic)  * (vec3(1.0) - F) * albedo * invPI;
            float d = dl / light.influenceDistance;
            float attenuation = squareFalloffAttenuation(dl2, 1.0 / light.influenceDistance);
            vec3 radiance = light.intensity * light.color * attenuation;
            litColor += (BRDFSpec + BRDFDiff) * radiance * clamp(dot(l, normal), 0.0f, 1.0f);
        } else if (light.type == LIGHT_TYPE_DIR) {
            vec3 l = -normalize(light.direction);
            vec3 h = normalize(viewDir + l);
            vec3 F0 = vec3(0.04);
            F0 = mix(F0, albedo, metallic);
            vec3 F = F(h, l, F0);
            vec3 BRDFSpec = F * BRDFGeometry(viewDir, l, h, normal, alphaRoughness);
            vec3 BRDFDiff = (1.0 - metallic)  * (vec3(1.0) - F) * albedo * invPI;
            vec3 radiance = light.intensity * light.color;

            float shadow = cascadedShadowFactor(viewSpaceCoord.z, worldSpaceCoord, normal, light.direction);
            // if (shadow > 0.9f) {
            //     shadow *= contactShadowTrace(viewSpaceCoord, 0.2, 4, vec3(viewMult(consts.viewBaseQuat, consts.camPos, vec4(-light.lightDirection, 0.0))));
            // }
            // float shadow = contactShadowTrace(viewSpaceCoord, 0.2, 8, vec3(viewMult(consts.viewBaseQuat, consts.camPos, vec4(-light.lightDirection, 0.0))));
            litColor += (BRDFSpec + BRDFDiff) * radiance * clamp(dot(l, normal), 0.0f, 1.0f) * shadow;
        }

        // outLit *= vec4(lights[lightId].color, 1.0f);
    }
    
    vec3 ambient = vec3(0.03) * albedo;
    outLit = vec4((ambient + litColor), 1.0f);

    // for (uint i = 0; i < lightAssign[clusterId].nLights; ++i) {
    //     // draw actual light influence
    //     uint lightId = lightAssign[clusterId].lightIds[i];
    //     vec3 d  = worldSpaceCoord.xyz - lights[lightId].center;
    //     if (length(d) <= lights[lightId].influenceDistance) {
    //         if (lights[lightId].type == LIGHT_TYPE_AREA) {
    //             if (dot(d, lights[lightId].lightDirection) >= 0.0) {
    //                 outLit *= vec4(lights[lightId].color, 1.0f);
    //             }
    //         } else {
    //             outLit *= vec4(lights[lightId].color, 1.0f);
    //         }
    //     }
    // }
    


    ////////////////
    /*
    if (unlit) {
        outLit = vec4(albedo + emissive, 1.0f);
    }
    else {
        vec2 lctLutUV;
        if (consts.hasAreaLights) {
            float ndotv = clamp(dot(normal, viewDir), 0.0, 1.0);
            vec2 uv = vec2(roughness, sqrt(1.0 - ndotv));
            lctLutUV = uv * LCT_LUT_SCALE + LCT_LUT_BIAS;
        }

        vec3 F0 = vec3(0.04f);
        F0 = mix(F0, albedo, metallic);
        vec3 lightingColor = vec3(0.0f);


        for (uint i = 0; i < MAX_LIGHTS; ++i) {
            if (i >= consts.nLights) {
                break;
            }

            if (consts.hasAreaLights && lUbos[i].type == LIGHT_TYPE_AREA) {
                // TODO
                vec4 lctP0 = texture(samplerLTCParams[0], lctLutUV);
                vec4 lctP1 = texture(samplerLTCParams[1], lctLutUV);
                mat3 invM = mat3( // scaled inv. Not tr1ue inv. One determinant short
                    vec3(lctP0.x,   0,          lctP0.y),
                    vec3(0,         lctP0.z,    0),
                    vec3(lctP0.w,   0,          lctP1.x)
                );
                // refer to "LTC Fresnel Approximation" by Stephen Hill
                float nD = lctP1.y;
                float fD = lctP1.z;
                vec3 fresnel = F0 * nD + (1 - F0) * fD; // specular proportion of reflected light
                // specular term
                vec3 spec = ltcEvaluate(normal, viewDir, worldSpaceCoord.xyz, invM, lUbos[i].pose, lUbos[i].nVertices);
                spec *= fresnel;
                // diffuse term
                vec3 diff = ltcEvaluate(normal, viewDir, worldSpaceCoord.xyz, mat3(1.0f), lUbos[i].pose, lUbos[i].nVertices);
                diff *= (vec3(1.0f) - fresnel) * (1.0f - metallic) * albedo; // TODO: not correct. Try approximate with (1.0f - metallic) * albedo
                lightingColor += lUbos[i].intensity * lUbos[i].color * (spec + diff);
            }
        }
        
        outLit = vec4(lightingColor, 1.0f);
    }
    */
    //////////////////////////////


    
    // vec3 diffuse = albedo.xyz * fUbo.light.color * vec3(fUbo.light.intensity) * max(dot(normal, -fUbo.light.direction), 0.0f);
    // diffuse = diffuse * vec3(shadowFactor);
    // 
    // if (targetCascade == 0) {
    //     diffuse = diffuse * vec3(1.7f, 0.6f, 0.6f);
    // }
    // if (targetCascade == 1) {
    //     diffuse = diffuse * vec3(0.6f, 1.7f, 0.6f);
    // }
    // if (targetCascade == 2) {
    //     diffuse = diffuse * vec3(0.6f, 0.6f, 1.7f);
    // }

    // outLit = vec4(diffuse, 1.0f);
}