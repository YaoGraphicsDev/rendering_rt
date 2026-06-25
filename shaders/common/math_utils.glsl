#ifndef MATH_UTILS_GLSL
#define MATH_UTILS_GLSL

#define PI      3.14159265358979323
#define invPI   0.31830988618379067

mat3 quatToMat3(vec4 q) {
    mat3 B = mat3(1.0);
    float qxx = q.x * q.x;
	float qyy = q.y * q.y;
	float qzz = q.z * q.z;
	float qxz = q.x * q.z;
	float qxy = q.x * q.y;
	float qyz = q.y * q.z;
	float qwx = q.w * q.x;
	float qwy = q.w * q.y;
	float qwz = q.w * q.z;

	B[0][0] = 1.0 - 2.0 * (qyy +  qzz);
	B[0][1] = 2.0 * (qxy + qwz);
	B[0][2] = 2.0 * (qxz - qwy);

    B[1][0] = 2.0 * (qxy - qwz);
	B[1][1] = 1.0 - 2.0 * (qxx +  qzz);
	B[1][2] = 2.0 * (qyz + qwx);

	B[2][0] = 2.0 * (qxz + qwy);
	B[2][1] = 2.0 * (qyz - qwx);
	B[2][2] = 1.0 - 2.0 * (qxx +  qyy);
    return B;
}

vec4 viewInvMult(vec4 viewEncodedQ, vec3 viewEncodedT, vec4 v) {
    vec3 X = v.xyz;
    float w = v.w;
    mat3 B = quatToMat3(viewEncodedQ);
    return vec4(B*X + viewEncodedT*w, w);
}

vec4 projMult(vec4 projEncoded, vec4 v) {
    float A = projEncoded.x;
    float B = projEncoded.y;
    float C = projEncoded.z;
    float D = projEncoded.w;
    return vec4(A*v.x, -B*v.y, C*(v.z/D + v.w), -v.z);
}

vec4 projInvMult(vec4 projEncoded, vec4 v) {
    float A = projEncoded.x;
    float B = projEncoded.y;
    float C = projEncoded.z;
    float D = projEncoded.w;
    return vec4(v.x/A, -v.y/B, -v.w, v.z/C + v.w/D);
}

vec4 viewMult(vec4 viewEncodedQ, vec3 viewEncodedT, vec4 v) {
    vec3 X = v.xyz;
    float w = v.w;
    mat3 B = quatToMat3(viewEncodedQ);
    mat3 BT = transpose(B);
    return vec4(BT*X - BT*viewEncodedT*w, w);
}

vec4 ndcToView(vec4 ndc, vec4 projEncoded) {
    vec4 viewSpaceCoord = projInvMult(projEncoded, ndc);
    return viewSpaceCoord * vec4(1.0f / viewSpaceCoord.w);
}

vec4 ndcToView(vec4 ndc, mat4 projectInv) {
    vec4 viewSpaceCoord = projectInv * ndc;
    return viewSpaceCoord * vec4(1.0f / viewSpaceCoord.w);
}

#endif