#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"

#include "glsl_reflect/scene_culling/frustum_cull.comp.hpp"

class FrustumCulling {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		// Typical use cases:
		//	1. Followed by geometry and transparent pass -- true
		//	2. Followed by shadow pass -- false
		bool								pipelines_diff;
	};
	FrustumCulling(const PassConfig& cfg);

	~FrustumCulling();

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		glm::mat4				proj = glm::mat4(1.0f);
		glm::mat4				view = glm::mat4(1.0f);
		otcv::DescriptorSet*	fg_set = nullptr;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx);

private:
	bool _pipeline_diff;

	std::shared_ptr<ResourceContext>	_res;
	otcv::ShaderBlob					_shader_blob;
	otcv::ComputePipeline*				_pipeline;
	std::shared_ptr<otcv::NaiveExpandableDescriptorPool> _desc_pool;
	struct ObjectDescSetContext {
		otcv::DescriptorSet*	set;
		std::shared_ptr<otcv::SSBO<FrustumCullComp::ObjectIndexBuffer>>	ssbo_indices;
		std::shared_ptr<otcv::SSBO<FrustumCullComp::ObjectBuffer>>		ssbo_objects;
	};
	std::vector<ObjectDescSetContext>	_obj_desc_sets;	// one for each pipeline variant

	const uint32_t _compute_group_size = 64;
};