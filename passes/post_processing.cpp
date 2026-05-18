#include "post_processing.h"
#include "common/render_global_types.h"

using namespace otcv;

ToneMapping::ToneMapping(const PassConfig& cfg) {
	_res = cfg.res_context;
	_shader_blob = std::move(load_shaders_from_dir(cfg.shader_dir));
	_screen_quad_vb = screen_quad_ndc();
	_pipeline = GraphicsPipelineBuilder()
		.pipline_rendering()
		.add_color_attachment_format(cfg.color_attachment_format)
		.end()
		.shader_vertex(_shader_blob["screen_quad.vert"])
		.shader_fragment(_shader_blob["tone_mapping.frag"])
		.vertex_state(_screen_quad_vb->builder)
		.add_dynamic_state(VK_DYNAMIC_STATE_VIEWPORT)
		.add_dynamic_state(VK_DYNAMIC_STATE_SCISSOR)
		.build();
	_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_sampler_hdr = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).build();
	_desc_set = _desc_pool->allocate(_pipeline->desc_set_layouts.at(DescriptorSetRate::PerFrame));
	_desc_set->bind_sampler(0, &_sampler_hdr);
}

ToneMapping::~ToneMapping() {
	unload_shader_blob(_shader_blob);
	_screen_quad_vb->destroy();
	_pipeline->destroy();
	_sampler_hdr->destroy();
}

void ToneMapping::command(CommandContext& ctx) {
	ctx.cmd_buf->cmd_set_viewport(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_set_scissor(ctx.width, ctx.height);
	ctx.cmd_buf->cmd_bind_vertex_buffer(_screen_quad_vb);
	ctx.cmd_buf->cmd_bind_graphics_pipeline(_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _desc_set, DescriptorSetRate::PerFrame);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_draw(3, 1, 0, 0); // screen quad convention
}