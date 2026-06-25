#include "shadow_mapping.h"

#include "common/math_utils.h"
#include "common/render_global_types.h"

using namespace otcv;

ShadowMapping::ShadowMapping(const PassConfig& cfg) {
	_res = cfg.res_context;

	std::map<uint32_t, uint32_t> vs_indexing_limits = {
		{pack(DescriptorSetRate::PerObject, 0), _res->scene_mgr->_renderable_metas.size()}
	};
	std::map<std::string, ShaderLoadHint> file_hints = {
		{"shadow_mapping.vert", {ShaderLoadHint::Hint::DescriptorIndexing, &vs_indexing_limits}}
	};
	_shader_blob = std::move(load_shaders_from_dir(cfg.shader_dir, file_hints));
	
	_pipeline = GraphicsPipelineBuilder()
		.pipline_rendering()
			.depth_stencil_attachment_format(cfg.depth_attachment_format)
		.end()
		.shader_vertex(_shader_blob["shadow_mapping.vert"])
		.cull_back_face()
		.depth_test()
		.vertex_state(_res->mesh_mgr->_vb->builder)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
		.build();

	_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_obj_desc_sets.resize(_res->scene_mgr->_per_frame_bos.size());
	for (uint32_t f = 0; f < _obj_desc_sets.size(); ++f) {
		_obj_desc_sets[f] = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerObject]);
		auto model_arr = _res->scene_mgr->_per_frame_bos[f].model_mats;
		_obj_desc_sets[f]->bind_buffer_array(0, model_arr->_buf, 0, model_arr->_stride, model_arr->_n_ubos);
	}
}

ShadowMapping::~ShadowMapping() {
	unload_shader_blob(_shader_blob);
	_pipeline->destroy();
}

void ShadowMapping::commands(CommandContext& ctx) {
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);

	ctx.cmd_buf->cmd_bind_vertex_buffer(_res->mesh_mgr->_vb);
	ctx.cmd_buf->cmd_bind_index_buffer(_res->mesh_mgr->_ib, VK_INDEX_TYPE_UINT16);

	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _obj_desc_sets[ctx.fg_frame_id], DescriptorSetRate::PerObject);

	glm::mat4 project_view = ctx.light_proj * ctx.light_view;
	ctx.cmd_buf->cmd_push_constant(_pipeline, "projectView", &project_view);

	ctx.cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
	ctx.cmd_buf->cmd_draw_indexed_indirect_count(
		ctx.fg_indirect_cmd,
		0, ctx.fg_indirect_count,
		0,
		_res->render_queue->_order.size(),
		ctx.indirect_cmd_stride);
}
