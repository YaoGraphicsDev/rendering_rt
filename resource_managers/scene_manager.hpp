#pragma once

#include <vector>
#include <map>

#include "mesh_manager.hpp"
#include "material_manager.hpp"

#include "glsl_reflect/geometry_pass/geometry.vert.hpp"
#include "glsl_reflect/lighting_pass/Lighting.frag.hpp"

struct LightMeta {
	std::string name;
	enum class Type : uint32_t {
		Point = 0,
		Directional,
		Area,
		Null
	};
	Type type = Type::Null;
	glm::vec3 color = glm::vec3(1.0f);
	/* Intensity as photometric measurement.
	*				photometric					radiometric equivalence			unit
	* Point:		luminous intensity			intensity						lm/sr = cd
	* Directional	irradiance					illuminance						lm/m^2 = lx
	* Area			radiance					luminance						cd/m^2 = nit
	* https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Radiometry#table:radiometric-photometric
	*/
	float intensity = 1.0f;
														//	Point	Directional		Area	Spot
	glm::vec3 center = glm::vec3(0.0f);					//		O		X			O		O
	glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);	//		X		O			O		O
	float influence_radius = 1.0f;						//		O		max_float	O		O

	// rectangular area light exclusive
	glm::mat2x3 plane_base = glm::mat2x3(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::vec2 half_dims = glm::vec2(1.0f);

	SceneNodeHandle	node;

	struct ShadowSettings {
		bool casts_shadows = true;
		uint32_t resolution = 2048;
		// cascaded exclusive
		float blend_overlap = 0.5f; // we dont really need to blend between cascades here. Just a buffer zone
		float z_near = 0.1f;
		float z_far = 40.0f;
		uint32_t n_cascades = 4;
	};
	ShadowSettings shadow_settings;
};

struct RenderableMeta {
	MeshHandle		mesh;
	MaterialHandle	mat;
	SceneNodeHandle	node;
};


struct SceneNodeMeta {
	SceneNodeHandle parent = { INVALID_MANAGER_HANDLE_ID };
	std::vector<SceneNodeHandle> children; // get sorted out by build()
	std::string name;
	glm::mat4 local_transform = glm::mat4(1.0f);
	glm::mat4 world_transform = glm::mat4(1.0f); // recalculated when added
	std::vector<RenderableHandle> renderables;
	LightHandle light = { INVALID_MANAGER_HANDLE_ID };
};

class SceneManager {
public:
	RenderableHandle add_renderable(RenderableMeta rm) {
		_renderable_metas.push_back(rm);
		return { (int)_renderable_metas.size() - 1 };
	}

	LightHandle add_light(LightMeta lm) {
		_light_metas.push_back(lm);
		return { (int)_light_metas.size() - 1 };
	}

	SceneNodeHandle add_scene_node(SceneNodeMeta snm);

	void bindless_build();

	SceneNodeHandle get_node_handle(const std::string& name) {
		auto iter = std::find_if(_node_metas.begin(), _node_metas.end(), [&](SceneNodeMeta& snm) { return snm.name == name; });
		if (iter == _node_metas.end()) {
			return { INVALID_MANAGER_HANDLE_ID };
		}
		else {
			return { (int)(iter - _node_metas.begin()) };
		}
	}

	SceneNodeMeta get_node(SceneNodeHandle snh) {
		return _node_metas.at(snh.id);
	}

	LightMeta& get_light(SceneNodeHandle snh) {
		return _light_metas.at(get_node(snh).light.id);
	}

	template<typename Pred>
	std::pair<SceneNodeHandle, SceneNodeMeta> find_node_if(Pred pred) {
		auto iter = std::find_if(_node_metas.begin(), _node_metas.end(), pred);
		if (iter == _node_metas.end()) {
			return { {INVALID_MANAGER_HANDLE_ID}, SceneNodeMeta() };
		}
		else {
			return { {static_cast<int>(std::distance(_node_metas.begin(), iter))}, *iter };
		}
	}

	template<typename Pred>
	std::pair<LightHandle, LightMeta> find_light_if(Pred pred) {
		auto iter = std::find_if(_light_metas.begin(), _light_metas.end(), pred);
		if (iter == _light_metas.end()) {
			return { {INVALID_MANAGER_HANDLE_ID}, LightMeta() };
		}
		else {
			return { {static_cast<int>(std::distance(_light_metas.begin(), iter))}, *iter };
		}
	}

	void move_node_in_world(SceneNodeHandle snh, const glm::mat4& transform);

	void move_node_local(SceneNodeHandle snh, glm::vec3 trans, glm::mat3 rot, glm::vec3 scale);

	void update(uint32_t frame_id);

	std::vector<RenderableMeta>		_renderable_metas;
	std::vector<LightMeta>			_light_metas;
	std::vector<SceneNodeMeta>		_node_metas;

	struct PerFrameBOs {
		/*
		* The reason why I picked UBO arrays for model matrices and materials is that 1 indirect draw call (invocation group) will only access 1 model matrix and 1 material,
		* there is no divergent access, indices are dynamically uniform. Drivers can handle UBO access of that nature pretty well.
		* In comparison material buffer for ray tracing pipeline is constructed as SSBO, because accesses are completely random
		* Also UBO is visible to host, which makes it straightforward to update so why not.
		* 
		* Lights are different. Deferred lighting pass draws every texel on screen in one draw call. The indices of lights that a texel accesses may vary across different texels.
		* That is not dynamically uniform access. So use SSBO instead.
		* 
		* What is "dynamically uniform":
		* https://docs.vulkan.org/samples/latest/samples/extensions/descriptor_indexing/README.html#:~:text=uniform%20indexing%20qualifier-,Dynamically%20uniform,-is%20a%20somewhat
		*/
		// TODO: Put model matrices and material ids in one UBO
		std::shared_ptr<otcv::StaticUBOArray<GeometryVert::ModelMatUBO>>	model_mats = nullptr;
		std::shared_ptr<otcv::StaticUBOArray<GeometryVert::MatIdUBO>>		mat_ids = nullptr;
		std::shared_ptr<otcv::SSBO<LightingFrag::LightBuffer>>	lights = nullptr;			
	};
	std::vector<PerFrameBOs>	_per_frame_bos;	// one for each frame-in-flight
};