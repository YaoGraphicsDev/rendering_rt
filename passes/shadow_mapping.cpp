#include "shadow_mapping.h"

#include "common/math_utils.h"
#include "common/render_global_types.h"

#include "glsl_reflect/shadows/shadow_mapping.vert.hpp"

using namespace otcv;

ShadowMapping::ShadowMapping(const PassConfig& cfg) {
	_res = cfg.res_context;

	uint32_t n_renderables = _res->scene_mgr->_renderable_metas.size();
	uint32_t n_materials = _res->material_mgr->_mat_metas.size();
	uint32_t n_images = _res->material_mgr->_img_metas.size();
	uint32_t n_samplers = _res->material_mgr->_samp_metas.size();
	std::map<uint32_t, uint32_t> vs_indexing_limits = {
		{pack(DescriptorSetRate::PerObject, 0), n_renderables},
		{pack(DescriptorSetRate::PerObject, 1), n_renderables}
	};
	std::map<uint32_t, uint32_t> fs_indexing_limits = {
		{pack(DescriptorSetRate::PerMaterial, 0), n_materials}, // This hint is not necessary since materials buffer is now an SSBO
		{pack(DescriptorSetRate::PerMaterial, 1), n_images},
		{pack(DescriptorSetRate::PerMaterial, 2), n_samplers}
	};
	std::map<std::string, ShaderLoadHint> file_hints = {
		{"shadow_mapping.vert", {ShaderLoadHint::Hint::DescriptorIndexing, &vs_indexing_limits}},
		{"shadow_mapping.frag", {ShaderLoadHint::Hint::DescriptorIndexing, &fs_indexing_limits}}
	};
	_shader_blob = load_shaders_from_dir(cfg.shader_dir, file_hints);
	
	_pipeline = GraphicsPipelineBuilder()
		.pipline_rendering()
			.depth_stencil_attachment_format(cfg.depth_attachment_format)
		.end()
		.shader_vertex(_shader_blob["shadow_mapping.vert"])
		.shader_fragment(_shader_blob["shadow_mapping.frag"])
		.cull_back_face()
		.depth_test()
		.vertex_state(_res->mesh_mgr->_vb->builder)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
		.build();

	_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_obj_desc_sets.resize(_res->scene_mgr->_per_frame_bos.size());


	_obj_desc_sets.resize(_res->scene_mgr->_per_frame_bos.size());
	for (uint32_t f = 0; f < _obj_desc_sets.size(); ++f) {
		_obj_desc_sets[f] = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerObject]);
		auto model_arr = _res->scene_mgr->_per_frame_bos[f].model_mats;
		auto mat_id_arr = _res->scene_mgr->_per_frame_bos[f].mat_ids;
		_obj_desc_sets[f]->bind_buffer_array(0, model_arr->_buf, 0, model_arr->_stride, model_arr->_n_ubos);
		_obj_desc_sets[f]->bind_buffer_array(1, mat_id_arr->_buf, 0, mat_id_arr->_stride, mat_id_arr->_n_ubos);
	}
	// bind material descriptor set
	{
		_material_desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts[DescriptorSetRate::PerMaterial]);
		// auto ubo_arr = _res->material_mgr->_mat_ubos;
		auto mat_ssbo = _res->material_mgr->_mat_ssbo;
		// _material_desc_set->bind_buffer_array(0, ubo_arr->_buf, 0, ubo_arr->_stride, ubo_arr->_n_ubos);
		_material_desc_set->bind_buffer(0, mat_ssbo->_buf);
		std::vector<Image*>& imgs = _res->material_mgr->_imgs;
		if (!imgs.empty()) {
			_material_desc_set->bind_sampled_image(1, imgs.data(), 0, imgs.size());
		}
		std::vector<Sampler*>& samps = _res->material_mgr->_samps;
		if (!samps.empty()) {
			_material_desc_set->bind_sampler(2, samps.data(), 0, samps.size());
		}
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
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _material_desc_set, DescriptorSetRate::PerMaterial);

	ShadowMappingVert::PushConstants pc;
	pc.projectView = mat4_to_array(ctx.light_proj * ctx.light_view);
	pc.layerIndex = ctx.layer_id;
	ctx.cmd_buf->cmd_push_constant(_pipeline, pc);

	ctx.cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
	ctx.cmd_buf->cmd_draw_indexed_indirect_count(
		ctx.fg_indirect_cmd,
		0, ctx.fg_indirect_count,
		0,
		_res->render_queue->_order.size(),
		ctx.indirect_cmd_stride);
}
