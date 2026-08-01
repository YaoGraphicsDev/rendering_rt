#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 outLocalPosition;
layout(location = 1) flat out uint outProbeID;

layout (push_constant) uniform PushConstants {
	mat4 projectView;
	vec3 probeStart;
	ivec3 probeCounts; // powers of two in all dimensions. x * y * z equal to the height of generated image
	vec3 probeStep;
	int probeSize;  
    ivec2 atlasSize; // width, height
	float probeScale;
	uint sampleAtlasIndex; // 0, 1
} consts;

//struct Transform {
//	vec3 scale;
//	vec3 translation;
//};
// layout(std430, set = 0, binding = 0) buffer readonly TransformBuffer { // Probes dont need to rotate at all
//     Transform transforms[];
// };

#include "probe_indexing.glsl" //! #include "../common/probe_indexing.glsl"

void main() {
	uint probeID = uint(gl_InstanceIndex);
	
	ivec3 gridCoord = probeIndexToGridCoord(int(probeID), consts.probeCounts);
	vec3 translation = gridCoordToPosition(gridCoord, consts.probeStart, consts.probeStep);

	gl_Position = consts.projectView * vec4(inPosition * vec3(consts.probeScale) + translation, 1.0f);
	outLocalPosition = inPosition;
	outProbeID = probeID;
}