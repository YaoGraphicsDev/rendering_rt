#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"

#include "glsl_reflect/light_clustering/build_clusters.comp.hpp"

// Once initialized, cluster divisions stay fixed in view space
// Any change in screen size, camera parameter, a rebuild of this entire class is required
class LightClustering {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		glm::uvec3							n_clusters;
		float								width;
		float								height;
		float								z_near_abs; // positive values
		float								z_far_abs;
		glm::mat4							inv_proj;
	};
	LightClustering(const PassConfig& cfg);

	~LightClustering();

	struct CullContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		glm::mat4				proj = glm::mat4(1.0f);
		glm::mat4				view = glm::mat4(1.0f);
		otcv::DescriptorSet*	fg_set = nullptr;
		uint32_t				fg_frame_id;
	};
	void cull_commands(CullContext& ctx);

	struct AssignContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		glm::mat4				view = glm::mat4(1.0f);
		otcv::DescriptorSet*	fg_set = nullptr;
		uint32_t				fg_frame_id;
	};
	void assign_commands(AssignContext& ctx);

private:
	void generate_cluster_aabbs(const PassConfig& cfg);

	std::shared_ptr<ResourceContext>	_res;
	otcv::ShaderBlob					_shader_blob;
	otcv::ComputePipeline*				_build_cluster_pipeline;
	otcv::ComputePipeline*				_cull_pipeline;
	otcv::ComputePipeline*				_assign_pipeline;
	
	otcv::DescriptorSet*				_cluster_desc_set;
	std::shared_ptr<otcv::SSBO<BuildClustersComp::ClusterAABBBuffer>>	_cluster_ssbo;

	std::shared_ptr<otcv::NaiveExpandableDescriptorPool>	_desc_pool;
	struct FrameDescSetContext {
		otcv::DescriptorSet* cull_set;
		otcv::DescriptorSet* assign_set;
	};
	std::vector<FrameDescSetContext>		_frame_desc_sets;	// one for each frame-in-flight

	otcv::CommandBuffer*				_build_cmd_buf;

	const glm::uvec3	_build_group_size = { 1, 1, 1 };	// keep up with these values in shaders
	const uint32_t		_cull_group_size = 64;
	const uint32_t		_assign_group_size = 64;
};