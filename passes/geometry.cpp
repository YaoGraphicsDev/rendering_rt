#include "geometry.h"
#include "common/render_global_types.h"

using namespace otcv;

GeometryPass::GeometryPass(const PassConfig& cfg) {
	_res = cfg.res_context;

	uint32_t n_renderables = _res->scene_mgr->_renderable_metas.size();
	uint32_t n_materials = _res->material_mgr->_mat_metas.size();
	uint32_t n_images = _res->material_mgr->_img_metas.size();
	uint32_t n_samplers = _res->material_mgr->_samp_metas.size();
	std::map<uint32_t, uint32_t> vs_indexing_limits = {
		{pack(DescriptorSetRate::PerObject, 0), n_renderables}
	};
	std::map<uint32_t, uint32_t> fs_indexing_limits = {
		{pack(DescriptorSetRate::PerMaterial, 0), n_materials},
		{pack(DescriptorSetRate::PerMaterial, 1), n_images},
		{pack(DescriptorSetRate::PerMaterial, 2), n_samplers}
	};
	std::map<std::string, ShaderLoadHint> file_hints = {
		{"geometry.vert", {ShaderLoadHint::Hint::DescriptorIndexing, &vs_indexing_limits}},
		{"geometry.frag", {ShaderLoadHint::Hint::DescriptorIndexing, &fs_indexing_limits}}
	};
	_shader_blob = load_shaders_from_dir(cfg.shader_dir, file_hints);

	GraphicsPipelineBuilder builder;
	builder
		.shader_vertex(_shader_blob["geometry.vert"])
		.shader_fragment(_shader_blob["geometry.frag"])
		.vertex_state(_res->mesh_mgr->_vb->builder)
		.depth_test()
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR);

	GraphicsPipelineBuilder::PipelineRendering& pr = builder.pipline_rendering();
	for (VkFormat cf : cfg.color_attachment_formats) {
		pr.add_color_attachment_format(cf);
	}
	pr.depth_stencil_attachment_format(cfg.depth_attachment_format);
	pr.end();

	GraphicsPipelineBuilder ds_builder = builder;
	_pipeline_map[RenderQueue::PipelineVariant::DoubleSided] = ds_builder.build();

	builder.cull_back_face();
	GraphicsPipelineBuilder bfc_builder = builder;
	_pipeline_map[RenderQueue::PipelineVariant::BackFaceCulled] = bfc_builder.build();

	_desc_pool.reset(new NaiveExpandableDescriptorPool);

	// since all variants are built on the same descriptor sets, just grab one for the layout
	const std::vector<DescriptorSetLayout*>& set_layouts = _pipeline_map.begin()->second->desc_set_layouts;
	_pipeline_layout = _pipeline_map.begin()->second->pipeline_layout;
	// bind object descriptor set
	{
		_obj_desc_set = _desc_pool->allocate(set_layouts[DescriptorSetRate::PerObject]);
		std::shared_ptr<StaticUBOArray> ubo_arr = _res->scene_mgr->_renderable_ubos;
		_obj_desc_set->bind_buffer_array(0, ubo_arr->_buf, 0, ubo_arr->_stride, ubo_arr->_n_ubos);
	}
	// bind material descriptor set
	{
		_material_desc_set = _desc_pool->allocate(set_layouts[DescriptorSetRate::PerMaterial]);
		std::shared_ptr<StaticUBOArray> ubo_arr = _res->material_mgr->_mat_ubos;
		_material_desc_set->bind_buffer_array(0, ubo_arr->_buf, 0, ubo_arr->_stride, ubo_arr->_n_ubos);
		std::vector<Image*>& imgs = _res->material_mgr->_imgs;
		_material_desc_set->bind_sampled_image(1, imgs.data(), 0, imgs.size());
		std::vector<Sampler*>& samps = _res->material_mgr->_samps;
		_material_desc_set->bind_sampler(2, samps.data(), 0, samps.size());
	}
}

GeometryPass::~GeometryPass() {
	unload_shader_blob(_shader_blob);
	for (auto& entry : _pipeline_map) {
		entry.second->destroy();
	}
}

void GeometryPass::commands(CommandContext& ctx) {
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);

	ctx.cmd_buf->cmd_bind_vertex_buffer(_res->mesh_mgr->_vb);
	ctx.cmd_buf->cmd_bind_index_buffer(_res->mesh_mgr->_ib, VK_INDEX_TYPE_UINT16);

	ctx.cmd_buf->cmd_bind_graphics_descriptor_set(_pipeline_layout, _obj_desc_set, DescriptorSetRate::PerObject);
	ctx.cmd_buf->cmd_bind_graphics_descriptor_set(_pipeline_layout, _material_desc_set, DescriptorSetRate::PerMaterial);

	glm::mat4 project_view = ctx.proj * ctx.view;
	ctx.cmd_buf->cmd_push_constant(_pipeline_layout, "projectView", &project_view);

	for (auto& p : _pipeline_map) {
		GraphicsPipeline* gp = p.second;
		ctx.cmd_buf->cmd_bind_graphics_pipeline(gp);

		RenderQueue::PipelineVariant pv = p.first;
		RenderQueue::OrderRange renderable_range = _res->render_queue->range_of(RenderQueue::PassType::Opaque, pv); // the start index of this specific type of renderable
		Std430AlignmentType::Range command_range = ctx.indirect_cmd_layout.range_of(renderable_range.start, SSBOAccess());
		uint32_t count_index = _res->render_queue->range_index_of(RenderQueue::PassType::Opaque, pv);
		Std430AlignmentType::Range count_range = ctx.indirect_count_layout.range_of(count_index, SSBOAccess());
		ctx.cmd_buf->cmd_draw_indexed_indirect_count(
			ctx.fg_indirect_cmd,
			command_range.offset,
			ctx.fg_indirect_count,
			count_range.offset,
			renderable_range.count,
			command_range.stride);
	}
}