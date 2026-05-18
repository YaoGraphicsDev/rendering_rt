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
		.cull_back_face(VK_FRONT_FACE_CLOCKWISE)
		.depth_test()
		.vertex_state(_res->mesh_mgr->_vb->builder)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
		.build();

	_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_obj_desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerObject]);
	std::shared_ptr<StaticUBOArray> ubo_arr = _res->scene_mgr->_renderable_ubos;
	_obj_desc_set->bind_buffer_array(0, ubo_arr->_buf, 0, ubo_arr->_stride, ubo_arr->_n_ubos);
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

	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _obj_desc_set, DescriptorSetRate::PerObject);

	glm::mat4 project_view = ctx.light_proj * ctx.light_view;
	ctx.cmd_buf->cmd_push_constant(_pipeline, "projectView", &project_view);

	ctx.cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
	Std430AlignmentType::Range command_range = ctx.indirect_cmd_layout.range_of(0, SSBOAccess());
	// ctx.cmd_buf->cmd_draw_indexed_indirect(ctx.fg_indirect_cmd, 0, _res->render_queue->_order.size(), command_range.stride);
	ctx.cmd_buf->cmd_draw_indexed_indirect_count(
		ctx.fg_indirect_cmd,
		0, ctx.fg_indirect_count,
		0,
		_res->render_queue->_order.size(),
		command_range.stride);
}
