#include "gltf_parser.h"
#include "otcv.h"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"
#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"
#include "gltf_traits.h"
#include "polygon_boundary.hpp"

#include <iostream>

namespace tg = tinygltf;

static tg::Model model;
static tg::TinyGLTF loader;
static std::string err;
static std::string warn;

static std::shared_ptr<SceneManager>	g_scene_mngr = nullptr;
static std::shared_ptr<MaterialManager>	g_mat_mngr = nullptr;
static std::shared_ptr<MeshManager>		g_mesh_mngr = nullptr;

struct ImageCacheKey {
	int image_id;
	uint16_t usage;

	bool operator<(const ImageCacheKey& rhs) const {
		return std::tie(image_id, usage) < std::tie(rhs.image_id, rhs.usage);
	}
};
static std::map<ImageCacheKey, ImageHandle>	g_img_cache;
static std::map<int, SamplerHandle>			g_samp_cache;

template<typename T>
static bool load_accessor(int accessor_id, std::vector<T>& buffer) {
	tg::Accessor& acc = model.accessors.at(accessor_id);
	tg::BufferView& bv = model.bufferViews.at(acc.bufferView);
	tg::Buffer& buf = model.buffers.at(bv.buffer);

	// type check
	if (GltfElementTraits<T>::gltf_type != acc.type ||
		GltfElementTraits<T>::gltf_component_type != acc.componentType) {
		std::cout << "data type mismatch. accessor.type = " << acc.type
			<< ", accessor.componentType = " << acc.componentType
			<< ", target type = " << TypeReflect<T>::name << std::endl;
		return false;
	}

	const unsigned char* ptr = buf.data.data() + bv.byteOffset + acc.byteOffset;
	size_t count = acc.count;

	size_t element_size = sizeof(T);
	size_t stride = bv.byteStride ? bv.byteStride : element_size;

	buffer.resize(count);
	for (size_t i = 0; i < count; ++i) {
		const void* srcPtr = ptr + stride * i;
		std::memcpy(&buffer[i], srcPtr, sizeof(T));
	}

	return true;
}

template<typename T>
static bool load_attribute(
	const tinygltf::Primitive& prim,
	const std::string& name,
	std::vector<T>& buffer)
{
	auto iter = prim.attributes.find(name);
	if (iter != prim.attributes.end()) {
		if (!load_accessor(iter->second, buffer)) {
			std::cout << "error parsing attribute " << name << std::endl;
			return false;
		}
	}
	return true;
}

static bool load_sampler(int sampler_id, SamplerHandle& sh) {
	if (g_samp_cache.count(sampler_id) == 1) {
		sh = g_samp_cache.at(sampler_id); // assuming usages stay the same
		return true;
	}

	// doesnt take default samplers. Throw invalid sampler id as exceptions
	tg::Sampler& gltf_sampler = model.samplers.at(sampler_id);
	SamplerMeta s;
	// magFilter
	switch (gltf_sampler.magFilter) {
	case TINYGLTF_TEXTURE_FILTER_LINEAR:
		s.mag_filter = SamplerMeta::Filter::Linear;
		break;
	case TINYGLTF_TEXTURE_FILTER_NEAREST:
		s.mag_filter = SamplerMeta::Filter::Nearest;
		break;
	case -1:
		break;
	default:
		std::cout << "Illegal magFilter value = " << gltf_sampler.magFilter << std::endl;
		assert(false);
		return false;
		break;
	}
	// minFilter
	switch (gltf_sampler.minFilter) {
	case TINYGLTF_TEXTURE_FILTER_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
		s.min_filter = SamplerMeta::Filter::Linear;
		break;
	case TINYGLTF_TEXTURE_FILTER_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
		s.min_filter = SamplerMeta::Filter::Nearest;
		break;
	case -1:
		break;
	default:
		std::cout << "Illegal minFilter value = " << gltf_sampler.minFilter << std::endl;
		assert(false);
		return false;
	}

	// mipmap 
	switch (gltf_sampler.minFilter) {
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
		s.mipmap_mode = SamplerMeta::Filter::Linear;
		break;
	case TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
	case TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
		s.mipmap_mode = SamplerMeta::Filter::Nearest;
		break;
	case -1:
		break;
	default:
		s.mipmap_mode = SamplerMeta::Filter::Linear; // gltf did not specify mipmap. Default to linear
		break;
	}

	// wrap
	switch (gltf_sampler.wrapS) {
	case TINYGLTF_TEXTURE_WRAP_REPEAT:
		s.wrap_u = SamplerMeta::Wrap::Repeat;
		break;
	case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
		s.wrap_u = SamplerMeta::Wrap::ClampToEdge;
		break;
	case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
		s.wrap_u = SamplerMeta::Wrap::MirroredRepeat;
		break;
	case -1:
		break;
	default:
		std::cout << "Illegal wrapS value = " << gltf_sampler.wrapS << std::endl;
		assert(false);
		return false;
	}

	switch (gltf_sampler.wrapT) {
	case TINYGLTF_TEXTURE_WRAP_REPEAT:
		s.wrap_v = SamplerMeta::Wrap::Repeat;
		break;
	case TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE:
		s.wrap_v = SamplerMeta::Wrap::ClampToEdge;
		break;
	case TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT:
		s.wrap_v = SamplerMeta::Wrap::MirroredRepeat;
		break;
	case -1:
		break;
	default:
		std::cout << "Illegal wrapT value = " << gltf_sampler.wrapT << std::endl;
		assert(false);
		return false;
	}
	s.wrap_w = SamplerMeta::Wrap::Repeat;

	sh = g_mat_mngr->add_sampler(s);
	g_samp_cache[sampler_id] = sh;
	return true;
}

enum class ImageUsage {
	MetallicRoughnessTexture,
	AlbedoTexture,
	Others
};

static bool load_image(int image_id, ImageHandle& ih, ImageUsage img_usage = ImageUsage::Others) {
	ImageCacheKey key{ image_id, (uint16_t)img_usage };

	if (g_img_cache.count(key) == 1) {
		ih = g_img_cache.at(key);
		return true;
	}

	tg::Image& gltf_image = model.images.at(image_id);
	ImageMeta i;
	i.uri = gltf_image.uri;
	i.width = gltf_image.width;
	i.height = gltf_image.height;
	i.channels = gltf_image.component;
	i.bits_per_channel = gltf_image.bits;
	if (img_usage == ImageUsage::MetallicRoughnessTexture) {
		// the reason why swizzling is necessary:
		// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#:~:text=material.pbrMetallicRoughness.metallicRoughnessTexture
		i.swizzle = ImageMeta::SwizzleType::BGR;
	}
	if (img_usage == ImageUsage::AlbedoTexture) {
		i.color_encoding = ImageMeta::ColorEncoding::SRGB;
	}

	ih = g_mat_mngr->add_image(i, gltf_image.image.data());
	g_img_cache[key] = ih;
	return true;
}

static bool load_texture(int texture_id, TextureHandle& th, ImageUsage img_usage = ImageUsage::Others) {
	if (texture_id < 0) {
		th = { INVALID_MANAGER_HANDLE_ID };
		return true;
	}
	
	tg::Texture& gltf_texture = model.textures.at(texture_id);
	TextureMeta t;
	bool res = true;
	res &= load_image(gltf_texture.source, t.image, img_usage); // we'll have to assume texture.source >= 0. Otherwise why would this texture even exist
	res &= load_sampler(gltf_texture.sampler, t.sampler);

	if (!res) {
		std::cout << "failed to load texture. Texture id = " << texture_id << std::endl;
		assert(false);
		return false;
	}
	
	th = g_mat_mngr->add_texture(t);
	return true;
}

static bool load_material(int material_id, MaterialHandle& mh) {
	if (material_id < 0) {
		mh = { INVALID_MANAGER_HANDLE_ID };
		return true;
	}

	const tg::Material& gltf_material = model.materials.at(material_id);
	MaterialMeta m;
	m.name = gltf_material.name;
	m.base_color_factor[0] = gltf_material.pbrMetallicRoughness.baseColorFactor[0];
	m.base_color_factor[1] = gltf_material.pbrMetallicRoughness.baseColorFactor[1];
	m.base_color_factor[2] = gltf_material.pbrMetallicRoughness.baseColorFactor[2];
	m.base_color_factor[3] = gltf_material.pbrMetallicRoughness.baseColorFactor[3];
	m.metallic_factor = gltf_material.pbrMetallicRoughness.metallicFactor;
	m.roughness_factor = gltf_material.pbrMetallicRoughness.roughnessFactor;
	m.normal_scale = gltf_material.normalTexture.scale;
	m.occlusion_strength = gltf_material.occlusionTexture.strength;
	m.emissive_factor[0] = gltf_material.emissiveFactor[0];
	m.emissive_factor[1] = gltf_material.emissiveFactor[1];
	m.emissive_factor[2] = gltf_material.emissiveFactor[2];
	auto iter = gltf_material.extensions.find("KHR_materials_emissive_strength");
	if (iter != gltf_material.extensions.end() && iter->second.Has("emissiveStrength")) {
		m.emissive_strength = (float)iter->second.Get("emissiveStrength").GetNumberAsDouble();
	}

	if (gltf_material.alphaMode == "OPAQUE") {
		m.alpha_mode = MaterialMeta::AlphaMode::Opaque;
	}
	else if (gltf_material.alphaMode == "MASK") {
		m.alpha_mode = MaterialMeta::AlphaMode::Mask;
	}
	else if (gltf_material.alphaMode == "BLEND") {
		m.alpha_mode = MaterialMeta::AlphaMode::Blend;
	}
	else {
		std::cout << "Unrecognized alphaMode = " << gltf_material.alphaMode << ", material id = " << material_id << std::endl;
		assert(false);
		return false;
	}
	m.alpha_cutoff = gltf_material.alphaCutoff;
	m.double_sided = gltf_material.doubleSided;

	bool res = true;
	res &= load_texture(gltf_material.pbrMetallicRoughness.baseColorTexture.index, m.base_color, ImageUsage::AlbedoTexture);
	res &= load_texture(gltf_material.pbrMetallicRoughness.metallicRoughnessTexture.index, m.metallic_roughness, ImageUsage::MetallicRoughnessTexture);
	res &= load_texture(gltf_material.normalTexture.index, m.normal);
	res &= load_texture(gltf_material.occlusionTexture.index, m.occlusion);
	res &= load_texture(gltf_material.emissiveTexture.index, m.emissive);

	if (!res) {
		std::cout << "failed to load material. Material id = " << material_id << std::endl;
		assert(false);
		return false;
	}
	
	mh = g_mat_mngr->add_material(m);
	return true;
}

// a gltf mesh primitive corresponds to a renderable
static bool load_primitive(int mesh_id, int prim_id, RenderableHandle& rh) {
	const tg::Primitive& prim = model.meshes.at(mesh_id).primitives.at(prim_id);

	// load geometry
	if (prim.mode != TINYGLTF_MODE_TRIANGLES) {
		std::cout << "cannot process primitive mode = " << prim.mode << std::endl;
		return false;
	}

	MeshMeta mm;
	// indices
	std::vector<uint16_t> indices;
	if (!load_accessor(prim.indices, indices)) {
		std::cout << "error parsing indices. indices_id = " << prim.indices << std::endl;
		assert(false);
		return false;
	}
	BufferMeta ibm;
	ibm.comp_per_ele = 1;
	ibm.comp_type = BufferMeta::ComponentType::UInt16;
	ibm.ele_count = indices.size();
	mm.ib = g_mesh_mngr->add_buffer(ibm, (uint8_t*)indices.data());

	// vertices
	VertexBufferMeta vbm;
	// position
	{
		std::vector<glm::vec3> positions;
		if (!load_attribute(prim, "POSITION", positions)) {
			std::cout << "error parsing position, mesh_id = " << mesh_id << ", prim_id = " << prim_id << std::endl;
			assert(false);
			return false;
		}
		if (positions.empty()) {
			std::cout << "position empty, mesh_id = " << mesh_id << ", prim_id = " << prim_id << std::endl;
			assert(false);
			return false;
		}
		BufferMeta bm;
		bm.comp_per_ele = 3;
		bm.comp_type = BufferMeta::ComponentType::Float;
		bm.ele_count = positions.size();
		vbm.position = g_mesh_mngr->add_buffer(bm, (uint8_t*)positions.data());
	}
	// normal
	{
		std::vector<glm::vec3> normals;
		if (!load_attribute(prim, "NORMAL", normals)) {
			std::cout << "error parsing normal, mesh_id = " << mesh_id << ", prim_id = " << prim_id << std::endl;
			assert(false);
			return false;
		}
		BufferMeta bm;
		bm.comp_per_ele = 3;
		bm.comp_type = BufferMeta::ComponentType::Float;
		bm.ele_count = normals.size();
		vbm.normal = g_mesh_mngr->add_buffer(bm, (uint8_t*)normals.data());
	}
	// tangent
	{
		std::vector<glm::vec4> tangents;
		if (!load_attribute(prim, "TANGENT", tangents)) {
			std::cout << "error parsing tangent, mesh_id = " << mesh_id << ", prim_id = " << prim_id << std::endl;
			assert(false);
			return false;
		}
		BufferMeta bm;
		bm.comp_per_ele = 4;
		bm.comp_type = BufferMeta::ComponentType::Float;
		bm.ele_count = tangents.size();
		vbm.tangent = g_mesh_mngr->add_buffer(bm, (uint8_t*)tangents.data());
	}
	// uv0
	{
		std::vector<glm::vec2> uv0;
		if (!load_attribute(prim, "TEXCOORD_0", uv0)) {
			std::cout << "error parsing uv0, mesh_id = " << mesh_id << ", prim_id = " << prim_id << std::endl;
			assert(false);
			return false;
		}
		BufferMeta bm;
		bm.comp_per_ele = 2;
		bm.comp_type = BufferMeta::ComponentType::Float;
		bm.ele_count = uv0.size();
		vbm.uv0 = g_mesh_mngr->add_buffer(bm, (uint8_t*)uv0.data());
	}
	mm.vb = g_mesh_mngr->add_vertex_buffer(vbm);

	MeshHandle mh = g_mesh_mngr->add_mesh(mm);
	RenderableMeta rm;
	rm.mesh = mh;
	if (!load_material(prim.material, rm.mat)) {
		std::cout << "error parsing material, mesh_id = " << mesh_id << ", prim_id = " << prim_id << std::endl;
		assert(false);
		return false;
	}

	rh = g_scene_mngr->add_renderable(rm);
	return true;
}

static glm::mat4 parse_matrix(const tg::Node& node) {
	if (!node.matrix.empty()) {
		glm::mat4 mat(1.0f);
		for (int i = 0; i < 16; i++) {
			reinterpret_cast<float*>(&mat)[i] = static_cast<float>(node.matrix[i]);
		}
		return mat;
	}

	glm::vec3 t(0.0f);
	glm::quat r(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 s(1.0f);

	if (!node.translation.empty()) {
		t.x = node.translation[0];
		t.y = node.translation[1];
		t.z = node.translation[2];
	}

	if (!node.rotation.empty()) {
		r.x = node.rotation[0];
		r.y = node.rotation[1];
		r.z = node.rotation[2];
		r.w = node.rotation[3];
	}

	if (!node.scale.empty()) {
		s.x = node.scale[0];
		s.y = node.scale[1];
		s.z = node.scale[2];
	}

	glm::mat4 T = glm::translate(glm::mat4(1.0f), t);
	glm::mat4 R = glm::toMat4(r);
	glm::mat4 S = glm::scale(glm::mat4(1.0f), s);

	return T * R * S;
}

static bool load_punctual_light(int light_id, LightHandle& lh) {
	auto ext_it = model.extensions.find("KHR_lights_punctual");
	if (ext_it == model.extensions.end()) {
		std::cout << "extension KHR_lights_punctual doesnt exist" << std::endl;
		assert(false);
		return false;
	}

	const tinygltf::Value& ext = ext_it->second;
	if (!ext.Has("lights")) {
		std::cout << "no light array in extension KHR_lights_punctual" << std::endl;
		assert(false);
		return false;
	}

	const tinygltf::Value& light_arr = ext.Get("lights");
	if (light_id >= light_arr.ArrayLen()) {
		std::cout << "invalid punctual light_id = " << light_id << std::endl;
		assert(false);
		return false;
	}
	
	const tinygltf::Value& gltf_light = light_arr.Get(light_id);
	LightMeta lm;

	if (gltf_light.Has("type")) {
		std::string type_str = gltf_light.Get("type").Get<std::string>();
		if (type_str == "directional") {
			lm.type = LightMeta::Type::Directional;
		}
		else if (type_str == "point") {
			lm.type = LightMeta::Type::Point;
		}
		else {
			std::cout << "unrecognized punctual light type = " << type_str << std::endl;
			assert(false);
			return false;
		}
	}
	if (gltf_light.Has("color")) {
		const auto& c = gltf_light.Get("color");
		lm.color = glm::vec3(
			static_cast<float>(c.Get(0).GetNumberAsDouble()),
			static_cast<float>(c.Get(1).GetNumberAsDouble()),
			static_cast<float>(c.Get(2).GetNumberAsDouble())
		);
	}
	if (gltf_light.Has("intensity")) {
		lm.intensity = static_cast<float>(gltf_light.Get("intensity").GetNumberAsDouble());
	}

	lm.center = glm::vec3(0.0f);
	lm.direction = glm::vec3(0.0f, 0.0f, -1.0f); // Blender gLTF exporter convention https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md#adding-light-instances-to-nodes
	if (lm.type == LightMeta::Type::Directional) {
		lm.influence_radius = std::numeric_limits<float>::max();
	}
	else {
		if(!gltf_light.Has("range")) {
			std::cout << "punctual light influence range unspecified. Take 1.0f by default. light_id = " << light_id << std::endl;
			assert(false);
			lm.influence_radius = 1.0f;
		}
		else {
			lm.influence_radius = static_cast<float>(gltf_light.Get("range").GetNumberAsDouble());
		}
	}

	lh = g_scene_mngr->add_light(lm);
	return true;
}

static bool load_area_light(int light_id, LightHandle& lh) {
	auto ext_it = model.extensions.find("EXT_lights_area");
	if (ext_it == model.extensions.end()) {
		std::cout << "extension EXT_lights_area doesnt exist" << std::endl;
		assert(false);
		return false;
	}

	const tinygltf::Value& ext = ext_it->second;
	if (!ext.Has("lights")) {
		std::cout << "no light array in extension EXT_lights_area" << std::endl;
		assert(false);
		return false;
	}

	const tinygltf::Value& light_arr = ext.Get("lights");
	if (light_id >= light_arr.ArrayLen()) {
		std::cout << "invalid area light_id = " << light_id << std::endl;
		assert(false);
		return false;
	}

	const tinygltf::Value& gltf_light = light_arr.Get(light_id);
	LightMeta lm;

	if (gltf_light.Has("type")) {
		std::string type_str = gltf_light.Get("type").Get<std::string>();
		if (type_str == "rect") {
			lm.type = LightMeta::Type::Area;
		}
		else if (type_str == "square") {
			lm.type = LightMeta::Type::Area;
		}
		else {
			std::cout << "unrecognized area light type = " << type_str << std::endl;
			assert(false);
			return false;
		}
	}
	if (gltf_light.Has("color")) {
		const auto& c = gltf_light.Get("color");
		lm.color = glm::vec3(
			static_cast<float>(c.Get(0).GetNumberAsDouble()),
			static_cast<float>(c.Get(1).GetNumberAsDouble()),
			static_cast<float>(c.Get(2).GetNumberAsDouble())
		);
	}
	if (gltf_light.Has("intensity")) {
		lm.intensity = static_cast<float>(gltf_light.Get("intensity").GetNumberAsDouble());
	}

	lm.center = glm::vec3(0.0f);
	lm.direction = glm::vec3(0.0f, 0.0f, -1.0f); // Blender gLTF exporter convention https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md#adding-light-instances-to-nodes
	if (!gltf_light.Has("range")) {
		std::cout << "area light influence range unspecified. Take 1.0f by default. light_id = " << light_id << std::endl;
		lm.influence_radius = 1.0f;
	}
	else {
		lm.influence_radius = static_cast<float>(gltf_light.Get("range").GetNumberAsDouble());
	}

	lm.plane_base[0] = glm::vec3(1.0f, 0.0f, 0.0f); // exporter convention
	lm.plane_base[1] = glm::vec3(0.0f, 1.0f, 0.0f);
	if (gltf_light.Has("width")) {
		lm.half_dims[0] = static_cast<float>(gltf_light.Get("width").GetNumberAsDouble()) * 0.5f;
	}
	if (gltf_light.Has("height")) {
		lm.half_dims[1] = static_cast<float>(gltf_light.Get("height").GetNumberAsDouble()) * 0.5f;
	}
	
	lh = g_scene_mngr->add_light(lm);
	return true;
}

static bool try_load_light(const tg::Node& node, LightHandle& lh) {
	// check for punctual lights
	auto p_light_iter = node.extensions.find("KHR_lights_punctual");
	if (p_light_iter != node.extensions.end() && p_light_iter->second.Has("light")) {
		if (node.mesh != -1) {
			std::cout << "punctual light node cannot have a mesh" << std::endl;
			assert(false);
			return false;
		}
		if (!load_punctual_light(p_light_iter->second.Get("light").Get<int>(), lh)) {
			std::cout << "error loading punctual light" << std::endl;
			assert(false);
			return false;
		}
	}

	// check for area lights
	auto a_light_iter = node.extensions.find("EXT_lights_area");
	if (a_light_iter != node.extensions.end() && a_light_iter->second.Has("light")) {
		if (node.mesh != -1) {
			std::cout << "area light node cannot have a mesh" << std::endl;
			assert(false);
			return false;
		}
		if (!load_area_light(a_light_iter->second.Get("light").Get<int>(), lh)) {
			std::cout << "error loading area light" << std::endl;
			assert(false);
			return false;
		}
	}

	if (lh.id != INVALID_MANAGER_HANDLE_ID) {
		g_scene_mngr->_light_metas.at(lh.id).name = node.name;
	}

	// this node has no light. Still valid. Return true;
	return true;
}

static std::vector<glm::vec3> polygon_boundary(MeshHandle mh, float colinear_threshould = 1E-4f) {
	MeshMeta mm = g_mesh_mngr->_mesh_metas.at(mh.id);
	BufferHandle pos_bf = g_mesh_mngr->_vb_metas.at(mm.vb.id).position;
	BufferMeta pos_bm = g_mesh_mngr->_buf_metas.at(pos_bf.id);
	BufferHandle indices_bf = mm.ib;
	BufferMeta indices_bm = g_mesh_mngr->_buf_metas.at(indices_bf.id);
	// check if index is of uint16_t type
	assert(indices_bm.comp_type == BufferMeta::ComponentType::UInt16);
	assert(indices_bm.comp_per_ele == 1);
	std::vector<uint8_t>& indices_raw = g_mesh_mngr->_buf_contents.at(indices_bf.id);
	std::vector<uint16_t> boundary_indices = PolygonBoundaryExtractor::extract(indices_raw.data(), indices_bm.ele_count);
	// check if position is of vec3 type
	assert(pos_bm.comp_type == BufferMeta::ComponentType::Float);
	assert(pos_bm.comp_per_ele == 3);
	std::vector<uint8_t>& pos_raw = g_mesh_mngr->_buf_contents.at(pos_bf.id);
	std::vector<glm::vec3> boundary_verts;
	for (uint16_t i : boundary_indices) {
		uint32_t start = i * sizeof(glm::vec3);
		boundary_verts.push_back(glm::vec3(
			*(float*)&pos_raw.at(start),
			*(float*)&pos_raw.at(start + sizeof(float)),
			*(float*)&pos_raw.at(start + sizeof(float) * 2)
		));
	}
	std::vector<glm::vec3> boundary_verts_clean = std::move(PolygonBoundaryExtractor::remove_collinear_vertices(boundary_verts));
	return boundary_verts_clean;
}

//static bool load_area_light(RenderableHandle rh, LightHandle& lh) {
//	RenderableMeta rm = g_scene_mngr->_renderable_metas.at(rh.id);
//	MaterialMeta mm = g_mat_mngr->_mat_metas.at(rm.mat.id);
//	LightMeta lm;
//	std::vector<glm::vec3> boundary = std::move(polygon_boundary(rm.mesh));
//	if (!PolygonBoundaryExtractor::is_planar_rectangle(boundary)) {
//		std::cout << "area light boundary vertices do not consititute a planar rectangle" << std::endl;
//		assert(false);
//		return false;
//	}
//	
//	/*
//	* a -- d
//	* |    |
//	* b -- c
//	*/
//	glm::vec3 a = boundary[0];
//	glm::vec3 b = boundary[1];
//	glm::vec3 c = boundary[2];
//	glm::vec3 d = boundary[3];
//
//	lm.type = LightMeta::Type::Area;
//	lm.color = mm.emissive_factor;
//	lm.intensity = mm.emissive_strength;
//	lm.center = (a + b + c + d) / 4.0f;
//	lm.direction = glm::normalize(glm::cross(c - a, d - b));
//	lm.radius = ;
//	lm.plane_base = glm::mat2x3(glm::normalize(b - a), glm::normalize(d - a));
//	lm.half_len = glm::vec2((b - a) / 2.0f, (d - a) / 2.0f);
//	lh = g_scene_mngr->add_light(lm);
//}

// check for area light
//bool should_use_node_as_area_light(const tinygltf::Node& node)
//{
//	if (!node.extras.IsObject()) {
//		return false;
//	}
//
//	const auto& extras = node.extras.Get<tinygltf::Value::Object>();
//
//	auto it = extras.find("useAsLight");
//	if (it == extras.end())
//		return false;
//
//	if (!it->second.IsBool())
//		return false;
//
//	return it->second.Get<bool>();
//}

static bool load_node(int node_id, SceneNodeHandle parent_snh) {
	const tg::Node& node = model.nodes.at(node_id);

	SceneNodeMeta snm;
	snm.parent = parent_snh;
	snm.name = node.name;
	snm.local_transform = parse_matrix(node);

	// parse primitives
	if (node.mesh >= 0) {
		for (int prim_id = 0; prim_id < model.meshes.at(node.mesh).primitives.size(); ++prim_id) {
			// TODO: a mesh may be referenced by multiple scene nodes. Loading scene nodes this way may produce duplicated mesh data 
			snm.renderables.emplace_back();
			if (!load_primitive(node.mesh, prim_id, snm.renderables.back())) {
				std::cout << "error loading primitive. node id = " << node_id << std::endl;
				assert(false);
				return false;
			}
		}
	}

	// load light
	if (!try_load_light(node, snm.light)) {
		std::cout << "error loading light. node_id = " << node_id << std::endl;
		assert(false);
		return false;
	}

	SceneNodeHandle snh = g_scene_mngr->add_scene_node(snm);

	// recursively parse children
	for (int node_id : node.children) {
		if (!load_node(node_id, snh)) {
			std::cout << "error loading node. node_id = " << node_id << std::endl;
			assert(false);
			return false;
		}
	}

	return true;
}

bool load_gltf(
	const std::string& filename,
	std::shared_ptr<SceneManager> scene_mngr,
	std::shared_ptr<MaterialManager> mat_mngr,
	std::shared_ptr<MeshManager> mesh_mngr) {
	bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
	if (!err.empty()) {
		std::cout << "Error loading file " << filename << ": " << err << std::endl;
		assert(false);
		return false;
	}
	if (!warn.empty()) {
		std::cout << "Warning loading file " << filename << ": " << warn << std::endl;
		assert(false);
	}

	if (model.scenes.size() != 1) {
		std::cout << "Support gltf file with 1 scene only" << std::endl;
		assert(false);
		return false;
	}

	g_scene_mngr = scene_mngr;
	g_mat_mngr = mat_mngr;
	g_mesh_mngr = mesh_mngr;

	auto cleanup = [&]() {
		g_img_cache.clear();
		g_samp_cache.clear();

		g_scene_mngr = nullptr;
		g_mat_mngr = nullptr;
		g_mesh_mngr = nullptr;

		model = {};
		loader = {};
		err.clear();
		warn.clear();
	};

	for (int node_id : model.scenes[0].nodes) {
		SceneNodeHandle snh;
		if (!load_node(node_id, { INVALID_MANAGER_HANDLE_ID })) {
			std::cout << "error loading node. node_id = " << node_id << std::endl;
			assert(false);
			cleanup();
			return false;
		}
	}

	scene_mngr = g_scene_mngr;
	mat_mngr = g_mat_mngr;
	mesh_mngr = g_mesh_mngr;

	cleanup();
	return true;
}