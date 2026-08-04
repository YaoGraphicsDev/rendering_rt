#include "irradiance_fields.h"
#include "common/render_global_types.h"

#include "glsl_reflect/irradiance_field/update_probes.comp.hpp"
#include "glsl_reflect/irradiance_field/visualize_probes.vert.hpp"
#include "glsl_reflect/irradiance_field/visualize_probes.frag.hpp"
#include "glsl_reflect/irradiance_field/sample_field.frag.hpp"

#include "common/trivial_mesh.hpp"

using namespace otcv;

IrradianceFields::IrradianceFields(const PassConfig& cfg) {
	auto is_power_of_two = [](uint32_t n) -> bool {
		return n > 0 && (n & (n - 1)) == 0;
	};
	assert(glm::all(glm::greaterThanEqual(cfg.probe_counts, glm::uvec3(2))));
	assert(is_power_of_two(cfg.probe_counts.x));
	assert(is_power_of_two(cfg.probe_counts.y));
	assert(is_power_of_two(cfg.probe_counts.z));
	assert(glm::all(glm::greaterThan(cfg.probe_step, glm::vec3(0.0f))));

	_cfg = cfg;

	_atlas_probes_counts = glm::uvec2(cfg.probe_counts.x * cfg.probe_counts.y, cfg.probe_counts.z);
	// width of 1 border surrounding each probe, 1 pixel border for the whole atlas
	_irrad_atlas_size = _atlas_probes_counts * (glm::uvec2(cfg.probe_size_irrad) + glm::uvec2(2)) + glm::uvec2(2);
	_depth_atlas_size = _atlas_probes_counts * (glm::uvec2(cfg.probe_size_depth) + glm::uvec2(2)) + glm::uvec2(2);

	_shader_blob = load_shaders_from_dir(cfg.shader_dir);
	_screen_quad_vb = screen_quad_ndc();

	_update_probes_pipeline = ComputePipeline::create(_shader_blob.at("update_probes.comp"));

	if (cfg.visualize_probes) {
		_icosphere_vb = TrivialMesh::icosphere();
		_visualize_probes_pipeline = GraphicsPipelineBuilder()
			.pipline_rendering()
				.add_color_attachment_format(cfg.probe_visualize_color_format)
				.depth_stencil_attachment_format(cfg.probe_visualize_depth_format)
			.end()
			.shader_vertex(_shader_blob.at("visualize_probes.vert"))
			.shader_fragment(_shader_blob.at("visualize_probes.frag"))
			.vertex_state(_icosphere_vb->builder)
			.depth_test()
			.cull_back_face()
			.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
			.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
			.build();
		_sampler_atlas = SamplerBuilder().build();
		_desc_pool.reset(new NaiveExpandableDescriptorPool);
		_sampler_desc_set = _desc_pool->allocate(_visualize_probes_pipeline->desc_set_layouts.at(DescriptorSetRate::PerFrame));
		_sampler_desc_set->bind_sampler(0, &_sampler_atlas);
	}

	_sample_fields_pipeline = GraphicsPipelineBuilder()
		.pipline_rendering()
			.add_color_attachment_format(cfg.direct_lit_format)
		.end()
		.blend_attachment(0)
			.color_blend(VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_ONE, VK_BLEND_OP_ADD)
		.end()
		.shader_vertex(_shader_blob.at("screen_quad.vert"))
		.shader_fragment(_shader_blob.at("sample_field.frag"))
		.vertex_state(_screen_quad_vb->builder)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.build();
}

IrradianceFields::~IrradianceFields() {
	unload_shader_blob(_shader_blob);
	_update_probes_pipeline->destroy();
	_visualize_probes_pipeline->destroy();
	_sample_fields_pipeline->destroy();

	_sampler_atlas->destroy();

	_screen_quad_vb->destroy();
	_icosphere_vb->destroy();
}

void IrradianceFields::update_probes_commands(UpdateProbesContext& ctx) {
	assert(ctx.src_atlas_index == 0 || ctx.src_atlas_index == 1);
	ctx.cmd_buf->cmd_bind_compute_pipeline(_update_probes_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_update_probes_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	UpdateProbesComp::PushConstants pc;
	pc.probeCount = _atlas_probes_counts.x * _atlas_probes_counts.y;
	pc.raysPerProbe = _cfg.rays_per_probe;
	pc.probeMaxDistance = glm::length(_cfg.probe_step) * 1.5f;
	pc.depthSharpness = _cfg.depth_sharpness;
	pc.hysteresis = _cfg.hysteresis;
	pc.updateType = uint32_t(ctx.atlas_type);
	pc.srcAtlasIndex = ctx.src_atlas_index;
	if (ctx.atlas_type == AtlasType::Irradiance) {
		pc.probeSize = _cfg.probe_size_irrad;
		pc.atlasSize = { (int)_irrad_atlas_size.x, (int)_irrad_atlas_size.y };
	}
	else if (ctx.atlas_type == AtlasType::Depth) {
		pc.probeSize = _cfg.probe_size_depth;
		pc.atlasSize = { (int)_depth_atlas_size.x, (int)_depth_atlas_size.y };
	}
	else {
		assert(false);
	}
	ctx.cmd_buf->cmd_push_constant(_update_probes_pipeline, pc);
	ctx.cmd_buf->cmd_dispatch(pc.probeCount, 1, 1);
}

void IrradianceFields::visualize_probes_commands(VisualizeProbesContext& ctx) {
	if (!_cfg.visualize_probes) {
		return;
	}
	assert(ctx.sample_atlas_index == 0 || ctx.sample_atlas_index == 1);
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_bind_vertex_buffer(_icosphere_vb);
	ctx.cmd_buf->cmd_bind_graphics_pipeline(_visualize_probes_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_visualize_probes_pipeline, _sampler_desc_set, DescriptorSetRate::PerFrame);
	ctx.cmd_buf->cmd_bind_descriptor_set(_visualize_probes_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	VisualizeProbesVert::PushConstants pc;
	pc.projectView = mat4_to_array(ctx.proj_view);
	pc.probeStart = vec3_to_array(_cfg.probe_start);
	pc.probeCounts = vec3_to_array(glm::ivec3(_cfg.probe_counts));
	pc.probeStep = vec3_to_array(_cfg.probe_step);
	if (ctx.visualize_type == AtlasType::Irradiance) {
		pc.probeSize = _cfg.probe_size_irrad;
		pc.atlasSize = vec2_to_array(glm::ivec2(_irrad_atlas_size));
	}
	else if (ctx.visualize_type == AtlasType::Depth) {
		pc.probeSize = _cfg.probe_size_depth;
		pc.atlasSize = vec2_to_array(glm::ivec2(_depth_atlas_size));
	}
	else {
		assert(false);
	}
	pc.probeScale = ctx.probe_radius;
	pc.sampleAtlasIndex = ctx.sample_atlas_index;
	ctx.cmd_buf->cmd_push_constant(_visualize_probes_pipeline, pc);
	ctx.cmd_buf->cmd_draw(
		_icosphere_vb->builder._buffer_builders.at(0)._info.size / (sizeof(float) * 3),
		_cfg.probe_counts.x * _cfg.probe_counts.y * _cfg.probe_counts.z);
}

void IrradianceFields::sample_fields_commands(SampleFieldsContext& ctx) {
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_bind_vertex_buffer(_screen_quad_vb);
	ctx.cmd_buf->cmd_bind_graphics_pipeline(_sample_fields_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_sample_fields_pipeline, _sampler_desc_set, DescriptorSetRate::PerFrame);
	ctx.cmd_buf->cmd_bind_descriptor_set(_sample_fields_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	SampleFieldFrag::PushConstants pc;
	pc.projEncoded = vec4_to_array(ctx.cam->proj_enc);
	pc.viewBaseQuat = vec4_to_array(ctx.cam->view_base_quat);
	pc.camPos = vec3_to_array(ctx.cam->eye);
	pc.ddgi_params.probeStart = vec3_to_array(_cfg.probe_start);
	pc.ddgi_params.probeStep = vec3_to_array(_cfg.probe_step);
	pc.ddgi_params.irradAtlasSize = vec2_to_array(glm::ivec2(_irrad_atlas_size));
	pc.ddgi_params.irradProbeSize = _cfg.probe_size_irrad;
	pc.ddgi_params.depthAtlasSize = vec2_to_array(glm::ivec2(_depth_atlas_size));
	pc.ddgi_params.depthProbeSize = _cfg.probe_size_depth;
	pc.ddgi_params.probeCounts = vec3_to_array(glm::ivec3(_cfg.probe_counts));
	pc.normalBias = ctx.normal_bias;
	pc.sampleAtlasIndex = ctx.sample_atlas_index;
	pc.viewType = uint32_t(ctx.view_type);
	ctx.cmd_buf->cmd_push_constant(_sample_fields_pipeline, pc, VK_SHADER_STAGE_FRAGMENT_BIT);
	ctx.cmd_buf->cmd_draw(3, 1, 0, 0); // screen quad convention
}