#include "ray_query_direct.h"
#include "common/render_global_types.h"

using namespace otcv;

RayQueryDirect::RayQueryDirect(const PassConfig& cfg) {
	_res = cfg.res_context;

	uint32_t n_renderables = _res->scene_mgr->_renderable_metas.size();
	uint32_t n_materials = _res->material_mgr->_mat_metas.size();
	uint32_t n_images = _res->material_mgr->_img_metas.size();
	uint32_t n_samplers = _res->material_mgr->_samp_metas.size();
	std::map<uint32_t, uint32_t> indexing_limits = {
		{pack(DescriptorSetRate::ComputeRead, 7), n_images},
		{pack(DescriptorSetRate::ComputeRead, 8), n_samplers}
	};
	_shader = load_shader(cfg.shader_dir + "trace_one_bounce.comp.spv", ShaderLoadHint::Hint::DescriptorIndexing, &indexing_limits);

	_pipeline = ComputePipeline::create(_shader);
	_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_sampler_ray = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).build();

	_readonly_desc_sets.resize(_res->scene_acc->_per_frame_objs.size());
	for (uint32_t i = 0; i < _readonly_desc_sets.size(); ++i) {
		_readonly_desc_sets[i] = _desc_pool->allocate(_pipeline->desc_set_layouts.at(DescriptorSetRate::ComputeRead));
		_readonly_desc_sets[i]->bind_buffer(TraceOneBounceComp::InstanceInfoBuffer::Binding,	_res->scene_acc->_per_frame_objs[i].insts->_buf);
		_readonly_desc_sets[i]->bind_buffer(TraceOneBounceComp::GeometryInfoBuffer::Binding,	_res->scene_acc->_per_frame_objs[i].geos->_buf);
		_readonly_desc_sets[i]->bind_buffer(TraceOneBounceComp::LightBuffer::Binding,			_res->scene_mgr->_per_frame_bos[i].lights->_buf);
		_readonly_desc_sets[i]->bind_buffer(TraceOneBounceComp::NormalBuffer::Binding,			_res->mesh_mgr->_vb->buffers[1]);
		_readonly_desc_sets[i]->bind_buffer(TraceOneBounceComp::UVBuffer::Binding,				_res->mesh_mgr->_vb->buffers[2]);
		_readonly_desc_sets[i]->bind_buffer(TraceOneBounceComp::IndexBuffer::Binding,			_res->mesh_mgr->_ib);
		_readonly_desc_sets[i]->bind_buffer(TraceOneBounceComp::MaterialBuffer::Binding,		_res->material_mgr->_mat_ssbo->_buf);
		std::vector<Image*>& imgs = _res->material_mgr->_imgs;
		if (!imgs.empty()) {
			_readonly_desc_sets[i]->bind_sampled_image(7, imgs.data(), 0, imgs.size());
		}
		std::vector<Sampler*>& samps = _res->material_mgr->_samps;
		if (!samps.empty()) {
			_readonly_desc_sets[i]->bind_sampler(8, samps.data(), 0, samps.size());
		}
		_readonly_desc_sets[i]->bind_acceleration_structure(9, _res->scene_acc->_per_frame_objs[i].tlas.get());
		_readonly_desc_sets[i]->bind_sampler(10, &_sampler_ray);
	}
}

void RayQueryDirect::commands(CommandContext& ctx) {
	// temp: find the only directional light
	std::vector<LightMeta>& light_metas = _res->scene_mgr->_light_metas;
	auto iter = std::find_if(light_metas.begin(), light_metas.end(), [&](LightMeta& lm) {return lm.type == LightMeta::Type::Directional; });
	assert(iter != light_metas.end());
	LightMeta& lm = *iter;

	TraceOneBounceComp::PushConstants pc;
	pc.rayTexSize = { ctx.width, ctx.height };

	ctx.cmd_buf->cmd_push_constant(_pipeline, pc);
	ctx.cmd_buf->cmd_bind_compute_pipeline(_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, _readonly_desc_sets[ctx.fg_frame_id], DescriptorSetRate::ComputeRead);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_dispatch(calc_group_count(ctx.width, _compute_group_size.x), calc_group_count(ctx.height, _compute_group_size.y), 1);
}

RayQueryDirect::~RayQueryDirect() {
	_sampler_ray->destroy();
	_pipeline->destroy();
	_shader->destroy();
}