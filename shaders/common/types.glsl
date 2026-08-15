#ifndef TYPES_GLSL
#define TYPES_GLSL

#define ALPHA_MODE_OPQAUE	0
#define ALPHA_MODE_MASK		1
struct MaterialCfg {
	vec4 baseColorFactor;
	vec4 mrnoFactor; // 4 factors: metallic - roughness - normal scale - occlusion strength
	vec3 emissiveColorStrength; // rgb color * strength
	uint alphaMode; // 0 - opaque, 1 - mask, 2 - blend
	float alphaCutoff;
	uint flipNormal; // 0 - dont need to flip, 1 - need to flip
	uint unlit; // 0 -- lit, 1 -- unlit
};

struct TextureIds {
    int baseColorId;
    int normalId;
    int metallicRoughnessId;
	int emissiveId;
};

struct SamplerIds {
	int baseColorId;
	int normalId;
	int metallicRoughnessId;
	int emissiveId;
};

struct MaterialInfo {
    MaterialCfg materialCfg;
    TextureIds textureIds;
	SamplerIds samplerIds;
};

const uint LIGHT_TYPE_POINT = 0;
const uint LIGHT_TYPE_DIR   = 1;
const uint LIGHT_TYPE_AREA  = 2;

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
    
    uint cubeShadowId;
};

#endif
