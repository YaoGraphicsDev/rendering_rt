#version 460 // to support gl_BaseInstance https://www.khronos.org/opengl/wiki/Vertex_Shader/Defined_Inputs
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inPosition;

layout (push_constant) uniform PushConstants {
	mat4 projectView;
} consts;

layout(set = 1, binding = 0) uniform ModelMatUBO {
    mat4 model;
} mmUbos[];

void main() {
	uint objId = gl_BaseInstance;
	mat4 objModelMat = mmUbos[objId].model;
	gl_Position = consts.projectView * objModelMat * vec4(inPosition, 1.0f);
}