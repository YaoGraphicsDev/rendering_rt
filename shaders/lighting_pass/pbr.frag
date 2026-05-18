#version 450

#define MAX_CASCADE_COUNT 4

layout(location = 0) in vec2 inUV;

layout(location = 0) out vec4 outLit;

struct DirectionalLight {
    float intensity;
    vec3 color;
    vec3 direction;
};

struct Cascade {
    float zBegin;
    float zEnd;
    mat4 lightSpaceView;
    mat4 lightSpaceProject;
};

struct ShadowJitter {
    vec2 nTiles;
    uint nStrataPerDim;
    float radius;
};

struct CascadedShadow {
    float blendDepth;
    uint nCascades;
    uint resolution;
    Cascade cascades[MAX_CASCADE_COUNT];
};

layout(set = 0, binding = 0) uniform FrameUBO {
	mat4 projectInv;
    mat4 viewInv;
    DirectionalLight light; // allow only 1 directional light

    ShadowJitter shadowJitter;
    CascadedShadow cascadedShadow;
} fUbo;

layout(set = 0, binding = 1) uniform sampler samplerGBuffer;    // nearest
layout(set = 0, binding = 2) uniform sampler samplerShadowMap;     // linear
layout(set = 0, binding = 3) uniform sampler3D samplerShadowJitter;     // nearest, address repeat

// Set 3: Managed by framegraph
//  binding 0 - 15:     textures
layout(set = 3, binding = 0) uniform texture2D texDepth;
layout(set = 3, binding = 1) uniform texture2D texAlbedo;
layout(set = 3, binding = 2) uniform texture2D texNormal;
layout(set = 3, binding = 3) uniform texture2D texMetallicRoughness;
layout(set = 3, binding = 4) uniform texture2DArray texCascadedShadow;

// 0.0 -- in shadow, 1.0 -- not in shadow
float shadow_factor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir) {

    vec4 lightClipSpaceCoord = lightProject * lightSpaceCoord;
    vec4 lightSpaceNDC = lightClipSpaceCoord * vec4(1.0f / lightClipSpaceCoord.w);
    vec2 shadowUV = (lightSpaceNDC.xy + vec2(1.0f)) * vec2(0.5f);
    float lightSpaceNDCZ = lightSpaceNDC.z;
	
    float cosTheta = dot(normal, lightDir);
    if (cosTheta <= 0.0f) {
        return 0.0f;
    }

    float shadowBias = max(0.0005 * (1.0 - cosTheta), 0.0001);
    float lightSpaceShadowNDCZ = texture(sampler2DArray(texCascadedShadow, samplerShadowMap), vec3(shadowUV, targetCascade)).r;
    
	if (lightSpaceNDCZ - lightSpaceShadowNDCZ - shadowBias > 0.0f) {
		return 0.0f;
	} else {
		return 1.0f;
	}
}

vec4 ndc_to_view_space(vec4 ndc, mat4 projectInv) {
    vec4 viewSpaceCoord = projectInv * ndc;
    return viewSpaceCoord * vec4(1.0f / viewSpaceCoord.w);
}

float pcf_shadow_factor(
    uint targetCascade,
    vec4 lightSpaceCoord,
    mat4 lightProject,
    vec3 normal,
    vec3 lightDir) {

    uint nStrata = fUbo.shadowJitter.nStrataPerDim;
    vec2 nTiles = fUbo.shadowJitter.nTiles;
    float jitterRadius = fUbo.shadowJitter.radius;

    uint nJitterSample = (nStrata * nStrata) / 2;
    uint nTestJitterSample = nStrata / 2;

    float jitterStepW = 1.0 / float(nJitterSample);

    vec3 jitterUVW = vec3(inUV * nTiles, 0.0);
    float shadowFactor = 0.0;

    // quick test to see if fully in light or shadow
    for (uint i = 0; i < nTestJitterSample; ++i) {
        vec4 jitter = texture(samplerShadowJitter, jitterUVW);
        jitterUVW.z += jitterStepW;
    
        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
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

        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.xy * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
        shadowFactor += shadow_factor(targetCascade, lightSpaceCoord + vec4(jitter.zw * jitterRadius, 0.0, 0.0), lightProject, normal, lightDir);
    }

    return shadowFactor / float(nStrata * nStrata);
}


void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    // world position
    float depth = texelFetch(sampler2D(texDepth, samplerGBuffer), pixel, 0).r;
    vec4 ndc = vec4(inUV * 2.0f - 1.0f, depth, 1.0f);
    vec4 viewSpaceCoord = ndc_to_view_space(ndc, fUbo.projectInv); // fUbo.projectInv * ndc;
    vec4 worldSpaceCoord = fUbo.viewInv * viewSpaceCoord;
	
	vec4 albedo = texelFetch(sampler2D(texAlbedo, samplerGBuffer), pixel, 0);
    vec3 normal = texelFetch(sampler2D(texNormal, samplerGBuffer), pixel, 0).xyz;
    normal = normalize(normal);

    float zView = -viewSpaceCoord.z;
    uint targetCascade = 0;
    for (uint i = 0; i < fUbo.cascadedShadow.nCascades; ++i) {
        if (fUbo.cascadedShadow.cascades[i].zBegin < zView && fUbo.cascadedShadow.cascades[i].zEnd >= zView) {
            targetCascade = i;
            break;
        }
    }

    vec4 lightSpaceCoord0 = fUbo.cascadedShadow.cascades[targetCascade].lightSpaceView * worldSpaceCoord;
    float shadowFactor0 = pcf_shadow_factor(
                            targetCascade,
                            lightSpaceCoord0,
                            fUbo.cascadedShadow.cascades[targetCascade].lightSpaceProject,
                            normal,
                            -normalize(fUbo.light.direction));

    float shadowFactor = shadowFactor0;
    // check if cascade blending is required
    if (fUbo.cascadedShadow.cascades[targetCascade].zEnd - zView < fUbo.cascadedShadow.blendDepth &&
        targetCascade < fUbo.cascadedShadow.nCascades - 1) {

		vec4 lightSpaceCoord1 = fUbo.cascadedShadow.cascades[targetCascade + 1].lightSpaceView * worldSpaceCoord;
        float shadowFactor1 = pcf_shadow_factor(
                            targetCascade + 1,
                            lightSpaceCoord1,
                            fUbo.cascadedShadow.cascades[targetCascade + 1].lightSpaceProject,
                            normal,
                            -normalize(fUbo.light.direction));
        float blendFactor = clamp(1.0f - (fUbo.cascadedShadow.cascades[targetCascade].zEnd - zView) / fUbo.cascadedShadow.blendDepth, 0.0f, 1.0f);
        shadowFactor = mix(shadowFactor0, shadowFactor1, smoothstep(0.0f, 1.0f, blendFactor));
    }

    // TODO: temp, tranparent shadow to mimic GI
    shadowFactor = clamp(shadowFactor, 0.05f, 1.0f);

    
    vec3 diffuse = albedo.xyz * fUbo.light.color * vec3(fUbo.light.intensity) * max(dot(normal, -fUbo.light.direction), 0.0f);
    diffuse = diffuse * vec3(shadowFactor);
    
    if (targetCascade == 0) {
        diffuse = diffuse * vec3(1.7f, 0.6f, 0.6f);
    }
    if (targetCascade == 1) {
        diffuse = diffuse * vec3(0.6f, 1.7f, 0.6f);
    }
    if (targetCascade == 2) {
        diffuse = diffuse * vec3(0.6f, 0.6f, 1.7f);
    }

    outLit = vec4(diffuse, 1.0f);
}