#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"

class RayQueryDirect {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
	};
	RayQueryDirect(const PassConfig& cfg);

	~RayQueryDirect();

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		otcv::DescriptorSet*	fg_set = nullptr;
		uint32_t				fg_frame_id;

		uint32_t width = 0;
		uint32_t height = 0;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx);

private:
	std::shared_ptr<ResourceContext>						_res;
	otcv::ShaderModule*										_shader;
	otcv::ComputePipeline*									_pipeline = nullptr;
	std::shared_ptr<otcv::NaiveExpandableDescriptorPool>	_desc_pool = nullptr;
	otcv::Sampler*											_sampler_ray = nullptr;

	struct FrameDescSetContext {
		otcv::DescriptorSet*			set;
		std::shared_ptr<otcv::Buffer>	ray_buf;
	};
	std::vector<otcv::DescriptorSet*>						_readonly_desc_sets; // one per frame-in-flight

	const glm::uvec2 _compute_group_size = glm::uvec2(8, 8);
};