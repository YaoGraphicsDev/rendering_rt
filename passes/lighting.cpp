#include "lighting.h"
#include "common/render_global_types.h"
#include "common/pcf_shadow_noise.h"

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

	// gbuffer sampler
	_samplers.emplace_back(SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).build());
	// shadow sampler
	_samplers.emplace_back(SamplerBuilder().build());
	// shadow jitter sampler
	_pcf_noise = ShadowNoiseTexture::disk_noise_texture(cfg.jitter.tile_size, cfg.jitter.n_strata_per_dim);
	_samplers.emplace_back(SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).address_mode(VK_SAMPLER_ADDRESS_MODE_REPEAT).build());

	_frame_desc_sets.resize(get_context().swapchain->images.size());
	for (FrameDescSetContext& fds : _frame_desc_sets) {
		fds.set = _desc_pool->allocate(_pipeline->desc_set_layouts.at(DescriptorSetRate::PerFrame));
		fds.ubo = init_ubo(cfg);
		fds.set->bind_buffer(0, fds.ubo->_buf);
		fds.set->bind_sampler(1, &_samplers.at(0));
		fds.set->bind_sampler(2, &_samplers.at(1));
		fds.set->bind_image_sampler(3, &_pcf_noise, &_samplers.at(2));
	}
}

LightingPass::~LightingPass() {
	unload_shader_blob(_shader_blob);
	_screen_quad_vb->destroy();
	_pipeline->destroy();

	for (otcv::Sampler* sampler : _samplers) {
		sampler->destroy();
	}

	_pcf_noise->destroy();
}

void LightingPass::update(uint32_t frame_id, UpdateContext& ctx) {
	auto ubo = _frame_desc_sets.at(frame_id).ubo;
	ubo->set(StaticUBOAccess()["projectInv"], &ctx.inv_proj);
	ubo->set(StaticUBOAccess()["viewInv"], &ctx.inv_view);
	for (uint32_t i = 0; i < ctx.shadow.cascades.size(); ++i) {
		ubo->set(StaticUBOAccess()["cascadedShadow"]["cascades"][i]["zBegin"],				&ctx.shadow.cascades[i].z_begin);
		ubo->set(StaticUBOAccess()["cascadedShadow"]["cascades"][i]["zEnd"],				&ctx.shadow.cascades[i].z_end);
		ubo->set(StaticUBOAccess()["cascadedShadow"]["cascades"][i]["lightSpaceView"],		&ctx.shadow.cascades[i].light_view);
		ubo->set(StaticUBOAccess()["cascadedShadow"]["cascades"][i]["lightSpaceProject"],	&ctx.shadow.cascades[i].light_proj);
	}
	uint32_t tile_size = _pcf_noise->builder._image_info.extent.width;
	glm::vec2 n_tiles((float)ctx.width / tile_size, (float)ctx.height / tile_size);
	ubo->set(StaticUBOAccess()["shadowJitter"]["nTiles"], &n_tiles);
}

void LightingPass::commands(CommandContext& ctx) {
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_bind_vertex_buffer(_screen_quad_vb);
	ctx.cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _frame_desc_sets[ctx.fg_frame_id].set, DescriptorSetRate::PerFrame);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_draw(3, 1, 0, 0); // screen quad convention
}

std::shared_ptr<StaticUBO> LightingPass::init_ubo(const PassConfig& cfg) {
	Std140AlignmentType DirectionalLight;
	DirectionalLight.add(Std140AlignmentType::InlineType::Float, "intensity");
	DirectionalLight.add(Std140AlignmentType::InlineType::Vec3, "color");
	DirectionalLight.add(Std140AlignmentType::InlineType::Vec3, "direction");
	// static_assert(false); // TODO: light not set up yet. Might need a light data manager for that. Or do this in update()
	Std140AlignmentType Cascade;
	Cascade.add(Std140AlignmentType::InlineType::Float, "zBegin");
	Cascade.add(Std140AlignmentType::InlineType::Float, "zEnd");
	Cascade.add(Std140AlignmentType::InlineType::Mat4, "lightSpaceView");
	Cascade.add(Std140AlignmentType::InlineType::Mat4, "lightSpaceProject");
	Std140AlignmentType ShadowJitter;
	ShadowJitter.add(Std140AlignmentType::InlineType::Vec2, "nTiles");
	ShadowJitter.add(Std140AlignmentType::InlineType::Uint, "nStrataPerDim");
	ShadowJitter.add(Std140AlignmentType::InlineType::Float, "radius");
	Std140AlignmentType CascadedShadow;
	CascadedShadow.add(Std140AlignmentType::InlineType::Float, "blendDepth");
	CascadedShadow.add(Std140AlignmentType::InlineType::Uint, "nCascades");
	CascadedShadow.add(Std140AlignmentType::InlineType::Uint, "resolution");
	CascadedShadow.add(Cascade, "cascades", cfg.cascaded_shadow.n_max_cascades);

	Std140AlignmentType FrameUBO;
	FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "projectInv");
	FrameUBO.add(Std140AlignmentType::InlineType::Mat4, "viewInv");
	FrameUBO.add(DirectionalLight, "light");
	FrameUBO.add(ShadowJitter, "shadowJitter");
	FrameUBO.add(CascadedShadow, "cascadedShadow");

	// set up values that do not change across frames
	std::shared_ptr<StaticUBO> ubo = std::make_shared<StaticUBO>(FrameUBO);
	ubo->set(StaticUBOAccess()["shadowJitter"]["nStrataPerDim"], &cfg.jitter.n_strata_per_dim);
	ubo->set(StaticUBOAccess()["shadowJitter"]["radius"], &cfg.jitter.radius);
	ubo->set(StaticUBOAccess()["cascadedShadow"]["blendDepth"], &cfg.cascaded_shadow.blend_depth);
	ubo->set(StaticUBOAccess()["cascadedShadow"]["nCascades"], &cfg.cascaded_shadow.n_cascades);
	ubo->set(StaticUBOAccess()["cascadedShadow"]["resolution"], &cfg.cascaded_shadow.resolution);

	// temp: fixed directional light
	glm::vec3 light_direction(2.0f, -7.0f, 1.0f);
	glm::vec3 color(1.0f);
	float intensity = 2.0f;
	ubo->set(StaticUBOAccess()["light"]["intensity"], &intensity);
	ubo->set(StaticUBOAccess()["light"]["color"], &color);
	ubo->set(StaticUBOAccess()["light"]["direction"], &light_direction);

	return ubo;
}

