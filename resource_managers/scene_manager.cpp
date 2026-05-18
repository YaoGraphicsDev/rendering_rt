#include "scene_manager.hpp"

#include <iostream>

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
	return snh;
}

void SceneManager::bindless_build() {
	// check limits and acquire ubo alignment
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(otcv::get_context().physical_device->vk_physical_device, &device_properties);
	VkPhysicalDeviceLimits limits = device_properties.limits;
	VkDeviceSize ubo_alignment = limits.minUniformBufferOffsetAlignment;
	if (limits.maxPerStageDescriptorUniformBuffers < _renderable_metas.size()) {
		assert(false);
		std::cout << "MaterialManager::bindless_build() error: number of images = " << _node_metas.size() <<
			" exceeds maxPerStageDescriptorUniformBuffers = " << limits.maxPerStageDescriptorUniformBuffers << std::endl;
		return;
	}

	// build object ubos
	otcv::Std140AlignmentType ObjectUBO;
	ObjectUBO.add(otcv::Std140AlignmentType::InlineType::Mat4, "model");
	ObjectUBO.add(otcv::Std140AlignmentType::InlineType::Int, "matId");
	_renderable_ubos.reset(new otcv::StaticUBOArray(ObjectUBO, _renderable_metas.size(), ubo_alignment));


	// upload material data to ubo
	for (uint32_t i = 0; i < _renderable_metas.size(); ++i) {
		const RenderableMeta& rm = _renderable_metas[i];
		SceneNodeMeta& snm = _node_metas.at(rm.node.id);
		_renderable_ubos->set(i, otcv::StaticUBOAccess()["model"], &snm.world_transform);
		_renderable_ubos->set(i, otcv::StaticUBOAccess()["matId"], &rm.mat.id);
	}
}

