#include "common/math_utils.h"
#include "frustum_culling.h"
#include "common/render_global_types.h"

#include <numeric>

using namespace otcv;

FrustumCulling::FrustumCulling(const PassConfig& cfg) {

	_pipeline_diff = cfg.pipelines_diff;

	_res = cfg.res_context;
	_shader_blob = load_shaders_from_dir(cfg.shader_dir);
	_pipeline = ComputePipeline::create(_shader_blob["frustum_cull.comp"]);
	// check push constant limit
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(get_context().physical_device->vk_physical_device, &device_properties);
	VkPhysicalDeviceLimits limits = device_properties.limits;
	VkDeviceSize max_pc_size = limits.maxPushConstantsSize;
	for (auto& pc : _pipeline->pipeline_layout->push_consts) {
		if (pc.second.offset + pc.second.size > max_pc_size) {
			std::cout << "FrustumCulling::FrustumCulling() error: push constant exceed limit. constant name = "
				<< pc.first << ", offset = " << pc.second.offset << ", size = " << pc.second.size << std::endl;
			assert(false);
			return;
		}
	}
	_desc_pool.reset(new NaiveExpandableDescriptorPool);

	// set objects SSBO
	std::shared_ptr<SSBO<FrustumCullComp::ObjectBuffer>> ssbo_objects = nullptr;
	{
		uint32_t n_renderables = _res->scene_mgr->_renderable_metas.size();
		ssbo_objects = std::make_shared<SSBO<FrustumCullComp::ObjectBuffer>>(n_renderables);
		std::vector<FrustumCullComp::ObjectData> object_data_buf(n_renderables);
		for (uint32_t i = 0; i < n_renderables; ++i) {
			RenderableMeta rm = _res->scene_mgr->_renderable_metas.at(i);
			const SceneNodeMeta& snm = _res->scene_mgr->_node_metas.at(rm.node.id);
			MeshManager::MeshDataSegment& segment = _res->mesh_mgr->_mesh_segments.at(rm.mesh.id);
			object_data_buf[i].model = mat4_to_array(snm.world_transform);
			object_data_buf[i].indexCount = segment.index_count;
			object_data_buf[i].firstIndex = segment.index_start;
			object_data_buf[i].vertexOffset = segment.vertex_start;
		}
		ssbo_objects->full_sync_write(object_data_buf);
	}

	// set indices SSBOs
	const RenderQueue& rq = *_res->render_queue;
	std::vector<std::shared_ptr<SSBO<FrustumCullComp::ObjectIndexBuffer>>> ssbos_indices;

	if (_pipeline_diff) {
		for (const RenderQueue::OrderRange& order_range : rq._order_ranges) {
			ssbos_indices.push_back(std::make_shared<SSBO<FrustumCullComp::ObjectIndexBuffer>>(order_range.count));

			std::vector<uint32_t> ioi(order_range.count); // indices of interest
			for (uint32_t i = 0; i < order_range.count; ++i) {
				ioi[i] = rq._order.at(order_range.start + i).id;
			}

			std::vector<FrustumCullComp::Index> indices_buf(ioi.size());
			for (uint32_t i = 0; i < ioi.size(); ++i) {
				indices_buf[i].value = ioi[i];
			}
			ssbos_indices.back()->full_sync_write(indices_buf);
		}
	}
	else {
		ssbos_indices.push_back(std::make_shared<SSBO<FrustumCullComp::ObjectIndexBuffer>>(rq._order.size()));

		std::vector<uint32_t> ioi(rq._order.size()); // indices of interest. Covers full range of _order
		assert(rq._order_ranges.front().start == 0);
		assert(rq._order_ranges.back().start + rq._order_ranges.back().count == rq._order.size());
		for (uint32_t i = 0; i < rq._order.size(); ++i) {
			ioi[i] = rq._order.at(i).id;
		}

		std::vector<FrustumCullComp::Index> indices_buf(ioi.size());
		for (uint32_t i = 0; i < ioi.size(); ++i) {
			indices_buf[i].value = ioi[i];
		}
		ssbos_indices.back()->full_sync_write(indices_buf);
	}

	// set descriptor sets
	if (_pipeline_diff) {
		_obj_desc_sets.resize(rq._order_ranges.size());
	}
	else {
		_obj_desc_sets.resize(1);
	}
	for (uint32_t i = 0; i < _obj_desc_sets.size(); ++i) {
		ObjectDescSetContext& set_ctx = _obj_desc_sets[i];
		set_ctx.set = _desc_pool->allocate(_pipeline->desc_set_layouts.at(DescriptorSetRate::ComputeRead));
		set_ctx.ssbo_indices = ssbos_indices[i];
		set_ctx.set->bind_buffer(0, set_ctx.ssbo_indices->_buf);
		set_ctx.ssbo_objects = ssbo_objects;
		set_ctx.set->bind_buffer(1, set_ctx.ssbo_objects->_buf);
		set_ctx.set->bind_buffer(2, rq._mesh_prep->AABB_SSBO()->_buf);
	}
}

FrustumCulling::~FrustumCulling() {
	unload_shader_blob(_shader_blob);
	_pipeline->destroy();
}

void FrustumCulling::commands(CommandContext& ctx) {
	FrustumUtils::Frustum f = FrustumUtils::view_frustum_vertices(glm::inverse(ctx.proj), glm::inverse(ctx.view));
	glm::vec4 left = FrustumUtils::plane(f[0], f[4], f[5]);
	glm::vec4 right = FrustumUtils::plane(f[2], f[6], f[7]);
	glm::vec4 top = FrustumUtils::plane(f[3], f[7], f[4]);
	glm::vec4 bottom = FrustumUtils::plane(f[1], f[5], f[6]);
	glm::vec4 far = FrustumUtils::plane(f[5], f[4], f[7]);
	glm::vec4 near = FrustumUtils::plane(f[0], f[1], f[2]);
	std::array<glm::vec4, 6> planes = { left, right, top, bottom, far, near };
	ctx.cmd_buf->cmd_push_constant(_pipeline, "frustum_faces", planes.data());
	ctx.cmd_buf->cmd_bind_compute_pipeline(_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	
	if (_pipeline_diff) {
		assert(_obj_desc_sets.size() == _res->render_queue->_order_ranges.size());
		for (uint32_t i = 0; i < _obj_desc_sets.size(); ++i) {
			RenderQueue::OrderRange& order_range = _res->render_queue->_order_ranges[i];
			if (order_range.count == 0) {
				continue;
			}
			ctx.cmd_buf->cmd_push_constant(_pipeline, "command_offset", &order_range.start);
			ctx.cmd_buf->cmd_push_constant(_pipeline, "count_offset", &i);
			ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _obj_desc_sets[i].set, DescriptorSetRate::ComputeRead);
			ctx.cmd_buf->cmd_dispatch(calc_group_count(order_range.count, _compute_group_size), 1, 1);
		}
	}
	else {
		assert(_obj_desc_sets.size() == 1);
		uint32_t zero = 0;
		ctx.cmd_buf->cmd_push_constant(_pipeline, "command_offset", &zero);
		ctx.cmd_buf->cmd_push_constant(_pipeline, "count_offset", &zero);
		ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _obj_desc_sets[0].set, DescriptorSetRate::ComputeRead);
		ctx.cmd_buf->cmd_dispatch(calc_group_count(_res->render_queue->_order.size(), _compute_group_size), 1, 1);
	}
}
