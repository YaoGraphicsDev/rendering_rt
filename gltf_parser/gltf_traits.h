#pragma once

template<typename T>
struct GltfElementTraits;

template<>
struct GltfElementTraits<glm::vec2> {
	static constexpr int n_components = 2;
	static constexpr int gltf_type = TINYGLTF_TYPE_VEC2;
	static constexpr int gltf_component_type = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template<>
struct GltfElementTraits<glm::vec3> {
	static constexpr int n_components = 3;
	static constexpr int gltf_type = TINYGLTF_TYPE_VEC3;
	static constexpr int gltf_component_type = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template<>
struct GltfElementTraits<glm::vec4> {
	static constexpr int n_components = 4;
	static constexpr int gltf_type = TINYGLTF_TYPE_VEC4;
	static constexpr int gltf_component_type = TINYGLTF_COMPONENT_TYPE_FLOAT;
};

template<>
struct GltfElementTraits<uint32_t> {
	static constexpr int n_components = 1;
	static constexpr int gltf_type = TINYGLTF_TYPE_SCALAR;
	static constexpr int gltf_component_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT;
};

template<>
struct GltfElementTraits<uint16_t> {
	static constexpr int n_components = 1;
	static constexpr int gltf_type = TINYGLTF_TYPE_SCALAR;
	static constexpr int gltf_component_type = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
};

template<typename T>
struct TypeReflect;

template<>
struct TypeReflect<glm::vec2> {
	static constexpr const char* name = "vec2";
};

template<>
struct TypeReflect<glm::vec3> {
	static constexpr const char* name = "vec3";
};

template<>
struct TypeReflect<glm::vec4> {
	static constexpr const char* name = "vec4";
};

template<>
struct TypeReflect<uint32_t> {
	static constexpr const char* name = "uint32_t";
};

template<>
struct TypeReflect<uint16_t> {
	static constexpr const char* name = "uint16_t";
};