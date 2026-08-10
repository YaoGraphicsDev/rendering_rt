#include "lighting.h"
#include "common/render_global_types.h"
#include "common/pcf_shadow_noise.h"
#include "common/external_texture_loader.h"

#include "glsl_reflect/lighting_pass/lighting.frag.hpp"

using namespace otcv;

LightingPass::LightingPass(const PassConfig& cfg) {
	_res = cfg.res_context;
    _shader_blob = std::move(load_shaders_from_dir(cfg.shader_dir));
    _screen_quad_vb = screen_quad_ndc();
	_pipeline = GraphicsPipelineBuilder()
		.pipline_rendering()
			.add_color_attachment_format(cfg.color_attachment_format)
		.end()
		.shader_vertex(_shader_blob["screen_quad.vert"])
		.shader_fragment(_shader_blob["lighting.frag"])
		.vertex_state(_screen_quad_vb->builder)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
		.build();

	_desc_pool.reset(new NaiveExpandableDescriptorPool);

	_lct_luts[0] = cfg.lct_luts_paths[0].empty() ? load_dummy_image() : load_dds_rgba16f(cfg.lct_luts_paths[0]);
	_lct_luts[1] = cfg.lct_luts_paths[1].empty() ? load_dummy_image() : load_dds_rgba16f(cfg.lct_luts_paths[1]);

	_sampler_lct_luts[0] = SamplerBuilder().build();
	_sampler_lct_luts[1] = SamplerBuilder().build();
	_sampler_g_buffer = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).build();

	_frame_desc_set.resize(_res->scene_mgr->_per_frame_bos.size());
	for (uint32_t i = 0; i < _frame_desc_set.size(); ++i) {
		otcv::DescriptorSet*& fds = _frame_desc_set[i];
		fds = _desc_pool->allocate(_pipeline->desc_set_layouts.at(DescriptorSetRate::PerFrame));
		auto ssbo_lights = _res->scene_mgr->_per_frame_bos.at(i).lights;
		if (ssbo_lights && ssbo_lights->_n_ssbos > 0) {
			fds->bind_buffer(0, ssbo_lights->_buf);
		}
		fds->bind_buffer(1, cfg.shadow_sys->_shadow_ubos.at(i)._buf);
		fds->bind_image_sampler(2, _lct_luts.data(), _sampler_lct_luts.data(), 0, 2);
		fds->bind_sampler(3, &_sampler_g_buffer);
		fds->bind_sampler(4, &cfg.shadow_sys->_sampler_shadowmap);
		fds->bind_image_sampler(5, &cfg.shadow_sys->_pcf_noise, &cfg.shadow_sys->_sampler_shadow_jitter);
	}
}

LightingPass::~LightingPass() {
	unload_shader_blob(_shader_blob);
	_screen_quad_vb->destroy();
	_pipeline->destroy();

	_sampler_lct_luts[0]->destroy();
	_sampler_lct_luts[1]->destroy();
	_sampler_g_buffer->destroy();

	_lct_luts[0]->destroy();
	_lct_luts[1]->destroy();
}

void LightingPass::commands(CommandContext& ctx) {
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_bind_vertex_buffer(_screen_quad_vb);
	ctx.cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
	LightingFrag::PushConstants pc;
	pc.projEncoded = vec4_to_array(ctx.cam->proj_enc);
	pc.viewBaseQuat = vec4_to_array(ctx.cam->view_base_quat);
	pc.camPos = vec3_to_array(ctx.cam->eye);
	uint32_t n_lights = _res->scene_mgr->_light_metas.size();
	pc.nLights = n_lights;
	if (n_lights > _max_light_count) {
		assert(false);
		std::cout << "LightingPass: light count exceeds max light count = " << _max_light_count << std::endl;
	}
	pc.nClusters = vec3_to_array(ctx.n_clusters);
	glm::vec2 z_range(ctx.cam->near, ctx.cam->far);
	pc.zRangeAbs = vec2_to_array(glm::abs(z_range));
	ctx.cmd_buf->cmd_push_constant(_pipeline, pc, VK_SHADER_STAGE_FRAGMENT_BIT);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _frame_desc_set[ctx.fg_frame_id], DescriptorSetRate::PerFrame);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_draw(3, 1, 0, 0); // screen quad convention
}
