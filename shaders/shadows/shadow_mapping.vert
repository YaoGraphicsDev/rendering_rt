#version 460 // to support gl_BaseInstance https://www.khronos.org/opengl/wiki/Vertex_Shader/Defined_Inputs
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_ARB_shader_viewport_layer_array : require

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec2 outUV;
layout(location = 2) flat out int outMaterialId;

layout (push_constant) uniform PushConstants {
	mat4 projectView;
	bool useRadialDepth;
	float maxRadialDepth;
	vec3 lightPos;
	int layerIndex;
} consts;

layout(set = 1, binding = 0) uniform ModelMatUBO {
    mat4 model;
} mmUbos[];
layout(set = 1, binding = 1) uniform MatIdUBO {
    int matId;
} miUbos[];

void main() {
	uint objId = gl_BaseInstance;

	mat4 objModelMat = mmUbos[objId].model;
	vec4 worldPosition = objModelMat * vec4(inPosition, 1.0f);
	gl_Position = consts.projectView * worldPosition;
	outWorldPosition = vec3(worldPosition);
	outUV = inUV;
	outMaterialId = miUbos[objId].matId;
	gl_Layer = consts.layerIndex;
}
