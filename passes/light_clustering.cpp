#include "light_clustering.h"
#include "common/math_utils.h"
#include "common/render_global_types.h"

using namespace otcv;

LightClustering::LightClustering(const PassConfig& cfg) {
	_res = cfg.res_context;
	_shader_blob = load_shaders_from_dir(cfg.shader_dir);
	_build_cluster_pipeline = ComputePipeline::create(_shader_blob.at("build_clusters.comp"));
	_cull_pipeline = ComputePipeline::create(_shader_blob.at("frustum_cull_lights.comp"));
	_assign_pipeline = ComputePipeline::create(_shader_blob.at("assign_lights.comp"));
	
	_desc_pool.reset(new NaiveExpandableDescriptorPool);

	generate_cluster_aabbs(cfg);

	_frame_desc_sets.resize(_res->scene_mgr->_per_frame_bos.size());
	for (uint32_t i = 0; i < _frame_desc_sets.size(); ++i) {
		FrameDescSetContext& fds = _frame_desc_sets[i];
		auto ssbo_lights = _res->scene_mgr->_per_frame_bos.at(i).lights;
		// light cull pass desc sets
		fds.cull_set = _desc_pool->allocate(_cull_pipeline->desc_set_layouts.at(DescriptorSetRate::ComputeRead));
		fds.cull_set->bind_buffer(0, ssbo_lights->_buf);
		// light assignment pass desc sets
		fds.assign_set = _desc_pool->allocate(_assign_pipeline->desc_set_layouts.at(DescriptorSetRate::ComputeRead));
		fds.assign_set->bind_buffer(0, _cluster_ssbo->_buf);
		fds.assign_set->bind_buffer(1, ssbo_lights->_buf);
	}
}

LightClustering::~LightClustering() {
	unload_shader_blob(_shader_blob);
	_build_cluster_pipeline->destroy();
	_cull_pipeline->destroy();
	_assign_pipeline->destroy();
}

void LightClustering::cull_commands(CullContext& ctx) {
	FrustumUtils::Frustum f = FrustumUtils::view_frustum_vertices(glm::inverse(ctx.proj), glm::inverse(ctx.view));
	glm::vec4 left = FrustumUtils::plane(f[0], f[4], f[5]);
	glm::vec4 right = FrustumUtils::plane(f[2], f[6], f[7]);
	glm::vec4 top = FrustumUtils::plane(f[3], f[7], f[4]);
	glm::vec4 bottom = FrustumUtils::plane(f[1], f[5], f[6]);
	glm::vec4 far = FrustumUtils::plane(f[5], f[4], f[7]);
	glm::vec4 near = FrustumUtils::plane(f[0], f[1], f[2]);
	std::array<glm::vec4, 6> planes = { left, right, top, bottom, far, near };
	// frustum cull lights
	ctx.cmd_buf->cmd_push_constant(_cull_pipeline, "frustum_faces", planes.data());
	ctx.cmd_buf->cmd_bind_compute_pipeline(_cull_pipeline);
	// light buffer copy and state transition
	auto ssbo_lights = _res->scene_mgr->_per_frame_bos.at(ctx.fg_frame_id).lights;
	ssbo_lights->push_staging_commands(
		*ctx.cmd_buf,
		ResourceState::FragSSBORead,
		ResourceState::ComputeSSBORead);
	ctx.cmd_buf->cmd_bind_descriptor_set(_cull_pipeline, _frame_desc_sets.at(ctx.fg_frame_id).cull_set, DescriptorSetRate::ComputeRead);
	ctx.cmd_buf->cmd_bind_descriptor_set(_cull_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_dispatch(calc_group_count(ssbo_lights->_n_ssbos, _cull_group_size), 1, 1); // number of lights
	ctx.cmd_buf->cmd_buffer_memory_barrier(ssbo_lights->_buf, ResourceState::ComputeSSBORead, ResourceState::FragSSBORead);
}

void LightClustering::assign_commands(AssignContext& ctx) {
	uint32_t n_clusters = _cluster_ssbo->_n_ssbos;
	ctx.cmd_buf->cmd_push_constant(_assign_pipeline, "nClusters", &n_clusters);
	ctx.cmd_buf->cmd_push_constant(_assign_pipeline, "view", &ctx.view);
	ctx.cmd_buf->cmd_bind_compute_pipeline(_assign_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_assign_pipeline, _frame_desc_sets.at(ctx.fg_frame_id).assign_set, DescriptorSetRate::ComputeRead);
	ctx.cmd_buf->cmd_bind_descriptor_set(_assign_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_dispatch(calc_group_count(n_clusters, _assign_group_size), 1, 1);
}

void LightClustering::generate_cluster_aabbs(const PassConfig& cfg) {
	_cluster_ssbo.reset(new SSBO<BuildClustersComp::ClusterAABBBuffer>(cfg.n_clusters.x * cfg.n_clusters.y * cfg.n_clusters.z));
	_cluster_desc_set = _desc_pool->allocate(_build_cluster_pipeline->desc_set_layouts[DescriptorSetRate::ComputeWrite]);
	_cluster_desc_set->bind_buffer(0, _cluster_ssbo->_buf);

	_build_cmd_buf = get_context().command_pool->allocate();
	_build_cmd_buf->begin(true);
	_build_cmd_buf->cmd_bind_compute_pipeline(_build_cluster_pipeline);
	_build_cmd_buf->cmd_bind_descriptor_set(_build_cluster_pipeline, _cluster_desc_set, DescriptorSetRate::ComputeWrite);
	_build_cmd_buf->cmd_push_constant(_build_cluster_pipeline, "nClusters", &cfg.n_clusters);
	glm::uvec2 screen_size = { cfg.width, cfg.height };
	_build_cmd_buf->cmd_push_constant(_build_cluster_pipeline, "screenSize", &screen_size);
	glm::vec2 z_range = { cfg.z_near_abs, cfg.z_far_abs };
	_build_cmd_buf->cmd_push_constant(_build_cluster_pipeline, "zRangeAbs", &z_range);
	_build_cmd_buf->cmd_push_constant(_build_cluster_pipeline, "projInv", &cfg.inv_proj);
	_build_cmd_buf->cmd_dispatch(
		calc_group_count(cfg.n_clusters.x, _build_group_size.x),
		calc_group_count(cfg.n_clusters.y, _build_group_size.y),
		calc_group_count(cfg.n_clusters.z, _build_group_size.z));
	_build_cmd_buf->cmd_buffer_memory_barrier(_cluster_ssbo->_buf, ResourceState::ComputeSSBOWrite, ResourceState::ComputeSSBORead);
	_build_cmd_buf->end();

	QueueSubmit submit;
	submit.batch()
		.add_command_buffer(_build_cmd_buf)
		.end();
	get_context().queue->submit(submit);
}
