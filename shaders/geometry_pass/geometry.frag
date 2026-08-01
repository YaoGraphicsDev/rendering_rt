#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_samplerless_texture_functions : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in vec2 inUV;				// only one set of UV for now
layout(location = 2) in vec4 inWorldTangent;
layout(location = 3) flat in int inMaterialId;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMetallicRoughness;
layout(location = 3) out vec4 outEmissive;
layout(location = 4) out uint outMatFlags;

#include "types.glsl" //! #include "../common/types.glsl"

layout(std430, set = 2, binding = 0) readonly buffer MaterialBuffer {
    MaterialInfo materials[];
};

layout(set = 2, binding = 1) uniform texture2D textures[];
layout(set = 2, binding = 2) uniform sampler samplers[];

void main() {
	if (inMaterialId < 0) {
		// not a valid material
		discard;
	}

    MaterialCfg cfg = materials[inMaterialId].materialCfg;
    TextureIds texIds = materials[inMaterialId].textureIds;
	SamplerIds samplerIds = materials[inMaterialId].samplerIds;

    vec4 albedo = vec4(1.0f);
    if (texIds.baseColorId >= 0 && samplerIds.baseColorId >= 0) {
		// vec4 color = texture(sampler2D(u_textures[i], u_sampler), uv);
        albedo = texture(sampler2D(
			textures[texIds.baseColorId],
			samplers[samplerIds.baseColorId]),
			inUV);
    }
	albedo = albedo * cfg.baseColorFactor;
	if (cfg.alphaMode == ALPHA_MODE_MASK && albedo.w < cfg.alphaCutoff) {
		discard;
	}
	outAlbedo = albedo;

	float metallicFactor = cfg.mrnoFactor.x;
	float roughnessFactor = cfg.mrnoFactor.y;
	float normalScale = cfg.mrnoFactor.z;
	
    vec3 worldNormal = vec3(0.0f);
    if (texIds.normalId >= 0 && samplerIds.normalId >= 0) {
	    vec3 n = normalize(inWorldNormal);
	    vec3 t = normalize(inWorldTangent.xyz);
	    vec3 b = cross(n, t) * inWorldTangent.w;
	    mat3 tbn = mat3(t, b, n);
	    vec3 tbnCoord = texture(sampler2D(
			textures[texIds.normalId],
			samplers[samplerIds.normalId]),
			inUV).xyz * 2.0 - 1.0;
	    tbnCoord.xy *= vec2(normalScale);
	    tbnCoord = normalize(tbnCoord);
	    worldNormal = normalize(tbn * tbnCoord);
    } else {
		worldNormal = normalize(inWorldNormal);
    }
	if (cfg.flipNormal == 1 && !gl_FrontFacing) {
		worldNormal = -worldNormal;
	}
	outNormal = vec4(worldNormal, 1.0f);

    if (texIds.metallicRoughnessId >= 0 && samplerIds.metallicRoughnessId >= 0) {
	    outMetallicRoughness = texture(sampler2D(
			textures[texIds.metallicRoughnessId],
			samplers[samplerIds.metallicRoughnessId]),
			inUV) * vec4(metallicFactor, roughnessFactor, 0.0f, 1.0f);
    } else {
		outMetallicRoughness = vec4(metallicFactor, roughnessFactor, 0.0f, 1.0f);
    }

	if (texIds.emissiveId >= 0 && samplerIds.emissiveId >= 0) {
		outEmissive = texture(sampler2D(
			textures[texIds.emissiveId],
			samplers[samplerIds.emissiveId]),
			inUV) * vec4(cfg.emissiveColorStrength.xyz, 1.0f);
	} else {
		outEmissive = vec4(cfg.emissiveColorStrength.xyz, 1.0f);
	}

	outMatFlags = cfg.unlit;
}