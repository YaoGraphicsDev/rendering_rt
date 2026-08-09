#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) flat in int inMaterialId;

layout (push_constant) uniform PushConstants {
	mat4 projectView;
	int layerIndex;
} consts;

#include "types.glsl" //! #include "../common/types.glsl"
// #include "math_utils.glsl" //! #include "../common/math_utils.glsl"

layout(std430, set = 2, binding = 0) readonly buffer MaterialBuffer {
    MaterialInfo materials[];
};
layout(set = 2, binding = 1) uniform texture2D textures[];
layout(set = 2, binding = 2) uniform sampler samplers[];

void main() {
	if (inMaterialId >= 0) {
		// a valid material, check for alpha mask
		MaterialCfg cfg = materials[inMaterialId].materialCfg;
		if (cfg.alphaMode == ALPHA_MODE_MASK) {
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
			if (albedo.w < cfg.alphaCutoff) {
				discard;
			}
		}
	}
	// no material, just draw it out in shadow pass.
}