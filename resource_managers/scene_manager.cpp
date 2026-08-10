#include "scene_manager.hpp"

#include <iostream>
#include <queue>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "glsl_reflect/lighting_pass/lighting.frag.hpp"

using namespace otcv;

SceneNodeHandle SceneManager::add_scene_node(SceneNodeMeta snm) {
	if (snm.parent.id == INVALID_MANAGER_HANDLE_ID) {
		snm.world_transform = snm.local_transform;
	}
	else {
		snm.world_transform = _node_metas.at(snm.parent.id).world_transform * snm.local_transform;
	}
	_node_metas.push_back(snm);
	SceneNodeHandle snh = { (int)_node_metas.size() - 1 };
	for (RenderableHandle& rh : snm.renderables) {
		_renderable_metas.at(rh.id).node = snh;
	}
	if (snm.light.id != INVALID_MANAGER_HANDLE_ID) {
		_light_metas.at(snm.light.id).node = snh;
	}
	return snh;
}

static void update_all_children_transforms(std::vector<SceneNodeMeta>& node_metas, int parent_id) {
	SceneNodeMeta& p = node_metas.at(parent_id);
	for (SceneNodeHandle c_snh : node_metas.at(parent_id).children) {
		SceneNodeMeta& c = node_metas.at(c_snh.id);
		c.world_transform = p.world_transform * c.local_transform;
		update_all_children_transforms(node_metas, c_snh.id);
	}
}

void SceneManager::move_node_in_world(SceneNodeHandle snh, const glm::mat4& transform) {
	SceneNodeMeta& node = _node_metas.at(snh.id);
	node.world_transform = transform * node.world_transform;
	if (node.parent.id != INVALID_MANAGER_HANDLE_ID) {
		SceneNodeMeta& parent = _node_metas.at(node.parent.id);
		node.local_transform = glm::inverse(parent.world_transform) * node.world_transform;
	}
	else {
		node.local_transform = node.world_transform;
	}
	update_all_children_transforms(_node_metas, snh.id);
}

static void decompose_trs(const glm::mat4& mat, glm::vec3& trans, glm::mat3& rot, glm::vec3& scale) {
	trans = glm::vec3(mat[3]);
	glm::mat3 mat3 = glm::mat3(mat);
	scale[0] = glm::length(mat3[0]);
	scale[1] = glm::length(mat3[1]);
	scale[2] = glm::length(mat3[2]);
	rot[0] = mat3[0] / scale[0];
	rot[1] = mat3[1] / scale[1];
	rot[2] = mat3[2] / scale[2];
}

void SceneManager::move_node_local(SceneNodeHandle snh, glm::vec3 delta_trans, glm::mat3 delta_rot, glm::vec3 delta_scale) {
	SceneNodeMeta& node = _node_metas.at(snh.id);

	glm::vec3 local_trans;
	glm::mat3 local_rot;
	glm::vec3 local_scale;
	decompose_trs(node.local_transform, local_trans, local_rot, local_scale);

	glm::mat4 local_trans44 = glm::translate(glm::mat4(1.0f), local_trans + local_rot * delta_trans);
	glm::mat4 local_rot44(1.0f);
	local_rot44 = glm::mat4(local_rot * delta_rot);
	glm::mat4 local_scale44 = glm::scale(glm::mat4(1.0f), local_scale * delta_scale);

	node.local_transform = local_trans44 * local_rot44 * local_scale44;
	if (node.parent.id != INVALID_MANAGER_HANDLE_ID) {
		SceneNodeMeta& parent = _node_metas.at(node.parent.id);
		node.world_transform = parent.world_transform * node.local_transform;
	}
	else {
		node.world_transform = node.local_transform;
	}
	update_all_children_transforms(_node_metas, snh.id);
}

void SceneManager::bindless_build() {
	// point children to the right nodes
	for (uint32_t i = 0; i < _node_metas.size(); ++i) {
		SceneNodeHandle parent_snh = _node_metas.at(i).parent;
		if (parent_snh.id != INVALID_MANAGER_HANDLE_ID) {
			_node_metas.at(parent_snh.id).children.push_back({ (int)i });
		}
	}

	// check limits and acquire ubo alignment
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(otcv::get_context().physical_device->vk_physical_device, &device_properties);
	VkPhysicalDeviceLimits limits = device_properties.limits;
	if (limits.maxPerStageDescriptorUniformBuffers < _renderable_metas.size()) {
		assert(false);
		std::cout << "MaterialManager::bindless_build() error: number of images = " << _node_metas.size() <<
			" exceeds maxPerStageDescriptorUniformBuffers = " << limits.maxPerStageDescriptorUniformBuffers << std::endl;
		return;
	}

	// build and upload object ubos 
	{
		uint32_t n_renderables = _renderable_metas.size();

		_per_frame_bos.resize(get_context().swapchain->mock_images.size());
		for (PerFrameBOs& f_bo : _per_frame_bos) {
			f_bo.model_mats.reset(new StaticUBOArray<GeometryVert::ModelMatUBO>(n_renderables));
			f_bo.mat_ids.reset(new StaticUBOArray<GeometryVert::MatIdUBO>(n_renderables));
			for (uint32_t i = 0; i < n_renderables; ++i) {
				const RenderableMeta& rm = _renderable_metas[i];
				SceneNodeMeta& snm = _node_metas.at(rm.node.id);
				
				f_bo.model_mats->set(i, FIELD_RANGE(GeometryVert::ModelMatUBO, model), &snm.world_transform);
				f_bo.mat_ids->set(i, FIELD_RANGE(GeometryVert::MatIdUBO, matId), &rm.mat.id);
			}
		}
	}

	// build and upload light buffer
	{
		uint32_t n_lights = _light_metas.size();
		for (PerFrameBOs& f_bo : _per_frame_bos) {
			f_bo.lights.reset(new SSBO<LightingFrag::LightBuffer>(n_lights));
			for (uint32_t i = 0; i < n_lights; ++i) {
				const LightMeta& lm = _light_metas[i];
				SceneNodeMeta& snm = _node_metas.at(lm.node.id);
				LightingFrag::Light light;
				light.type = (uint32_t)lm.type;
				light.intensity = lm.intensity;
				light.color = vec3_to_array(lm.color);
				light.center = vec3_to_array(glm::vec3(snm.world_transform * glm::vec4(lm.center, 1.0f)));
				light.direction =	vec3_to_array(glm::vec3(glm::normalize(snm.world_transform * glm::vec4(lm.direction, 0.0f))));
				light.planeBasisX = vec3_to_array(glm::vec3(glm::normalize(snm.world_transform * glm::vec4(lm.plane_base[0], 0.0f))));
				light.planeBasisY = vec3_to_array(glm::vec3(glm::normalize(snm.world_transform * glm::vec4(lm.plane_base[1], 0.0f))));
				light.halfDims = vec2_to_array(lm.half_dims);
				light.influenceDistance = lm.influence_radius;
				
				f_bo.lights->set(i, light);
			}
		}
	}
}

void SceneManager::update(uint32_t frame_id) {
	PerFrameBOs& f_bo = _per_frame_bos[frame_id];
	
	// update model transform
	uint32_t n_renderables = _renderable_metas.size();
	for (uint32_t i = 0; i < n_renderables; ++i) {
		const RenderableMeta& rm = _renderable_metas[i];
		SceneNodeMeta& snm = _node_metas.at(rm.node.id);
		f_bo.model_mats->set(i, FIELD_RANGE(GeometryVert::ModelMatUBO, model), &snm.world_transform);
	}

	// update lights
	uint32_t n_lights = _light_metas.size();
	for (uint32_t i = 0; i < n_lights; ++i) {
		const LightMeta& lm = _light_metas[i];
		SceneNodeMeta& snm = _node_metas.at(lm.node.id);

		glm::vec3 c = snm.world_transform * glm::vec4(lm.center, 1.0f);
		f_bo.lights->set(i, FIELD_RANGE(LightingFrag::LightBuffer::Element, center), &c);
		glm::vec3 dir = snm.world_transform * glm::vec4(lm.direction, 0.0f);
		f_bo.lights->set(i, FIELD_RANGE(LightingFrag::LightBuffer::Element, direction), &dir);
		glm::vec3 basis_x = glm::normalize(snm.world_transform * glm::vec4(lm.plane_base[0], 0.0f));
		f_bo.lights->set(i, FIELD_RANGE(LightingFrag::LightBuffer::Element, planeBasisX), &basis_x);
		glm::vec3 basis_y = glm::normalize(snm.world_transform * glm::vec4(lm.plane_base[1], 0.0f));
		f_bo.lights->set(i, FIELD_RANGE(LightingFrag::LightBuffer::Element, planeBasisY), &basis_y);
	}
}
