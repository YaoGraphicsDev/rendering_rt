#include "lighting.h"
#include "common/render_global_types.h"
#include "common/pcf_shadow_noise.h"
#include "common/external_texture_loader.h"

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
		.shader_fragment(_shader_blob["pbr.frag"])
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
	_sampler_shadowmap = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).build();
	_pcf_noise = ShadowNoiseTexture::disk_noise_texture(cfg.jitter.tile_size, cfg.jitter.n_strata_per_dim);
	_sampler_shadow_jitter = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).address_mode(VK_SAMPLER_ADDRESS_MODE_REPEAT).build();

	_frame_desc_sets.resize(_res->scene_mgr->_per_frame_bos.size());
	for (uint32_t i = 0; i < _frame_desc_sets.size(); ++i) {
		FrameDescSetContext& fds = _frame_desc_sets[i];
		fds.set = _desc_pool->allocate(_pipeline->desc_set_layouts.at(DescriptorSetRate::PerFrame));
		fds.ubo_shadow = init_shadow_ubo(cfg);
		auto ssbo_lights = _res->scene_mgr->_per_frame_bos.at(i).lights;
		if (ssbo_lights && ssbo_lights->_n_ssbos > 0) {
			fds.set->bind_buffer(0, ssbo_lights->_buf);
		}
		fds.set->bind_buffer(1, fds.ubo_shadow->_buf);
		fds.set->bind_image_sampler(2, _lct_luts.data(), _sampler_lct_luts.data(), 0, 2);
		fds.set->bind_sampler(3, &_sampler_g_buffer);
		fds.set->bind_sampler(4, &_sampler_shadowmap);
		fds.set->bind_image_sampler(5, &_pcf_noise, &_sampler_shadow_jitter);
	}
}

LightingPass::~LightingPass() {
	unload_shader_blob(_shader_blob);
	_screen_quad_vb->destroy();
	_pipeline->destroy();

	_sampler_lct_luts[0]->destroy();
	_sampler_lct_luts[1]->destroy();
	_sampler_g_buffer->destroy();
	_sampler_shadowmap->destroy();
	_sampler_shadow_jitter->destroy();

	_lct_luts[0]->destroy();
	_lct_luts[1]->destroy();
	_pcf_noise->destroy();
}

void LightingPass::update(uint32_t frame_id, UpdateContext& ctx) {
	// set shadow ubo
	auto s_ubo = _frame_desc_sets.at(frame_id).ubo_shadow;
	for (uint32_t i = 0; i < ctx.shadow.cascades.size(); ++i) {
		s_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, cascadedShadow.cascades[i].zBegin), &ctx.shadow.cascades[i].z_begin);
		s_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, cascadedShadow.cascades[i].zEnd), &ctx.shadow.cascades[i].z_end);
		s_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, cascadedShadow.cascades[i].lightSpaceView), &ctx.shadow.cascades[i].light_view);
		s_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, cascadedShadow.cascades[i].lightSpaceProject), &ctx.shadow.cascades[i].light_proj);
	}
	uint32_t tile_size = _pcf_noise->builder._image_info.extent.width;
	glm::vec2 n_tiles((float)ctx.width / tile_size, (float)ctx.height / tile_size);

	s_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, shadowJitter.nTiles), &n_tiles);
}

void LightingPass::commands(CommandContext& ctx) {
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_bind_vertex_buffer(_screen_quad_vb);
	ctx.cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
	ctx.cmd_buf->cmd_push_constant(_pipeline, "projEncoded", &ctx.cam->proj_enc);
	ctx.cmd_buf->cmd_push_constant(_pipeline, "viewBaseQuat", &ctx.cam->view_base_quat);
	ctx.cmd_buf->cmd_push_constant(_pipeline, "camPos", &ctx.cam->eye);
	uint32_t n_lights = _res->scene_mgr->_light_metas.size();
	ctx.cmd_buf->cmd_push_constant(_pipeline, "nLights", &n_lights);
	if (n_lights > _max_light_count) {
		assert(false);
		std::cout << "LightingPass: light count exceeds max light count = " << _max_light_count << std::endl;
	}
	ctx.cmd_buf->cmd_push_constant(_pipeline, "nClusters", &ctx.n_clusters);
	glm::vec2 z_range_abs(ctx.cam->near, ctx.cam->far);
	z_range_abs = glm::abs(z_range_abs);
	ctx.cmd_buf->cmd_push_constant(_pipeline, "zRangeAbs", &z_range_abs);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _frame_desc_sets[ctx.fg_frame_id].set, DescriptorSetRate::PerFrame);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_draw(3, 1, 0, 0); // screen quad convention
}

std::shared_ptr<otcv::StaticUBO<PbrFrag::ShadowUBO>> LightingPass::init_shadow_ubo(const PassConfig& cfg) {
	// set up values that do not change across frames
	std::shared_ptr<StaticUBO<PbrFrag::ShadowUBO>> shadow_ubo = std::make_shared<StaticUBO<PbrFrag::ShadowUBO>>();
	shadow_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, shadowJitter.nStrataPerDim), &cfg.jitter.n_strata_per_dim);
	shadow_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, shadowJitter.radius), &cfg.jitter.radius);
	shadow_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, cascadedShadow.blendDepth), &cfg.cascaded_shadow.blend_depth);
	shadow_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, cascadedShadow.nCascades), &cfg.cascaded_shadow.n_cascades);
	shadow_ubo->set(FIELD_RANGE(PbrFrag::ShadowUBO, cascadedShadow.resolution), &cfg.cascaded_shadow.resolution);

	return shadow_ubo;
}

