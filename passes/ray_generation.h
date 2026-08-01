#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"
#include "common/camera.h"

class RayGeneration {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		enum class GenType {
			FullScreen = 0,
			SphericalFibonacci,
		};
		GenType gen_type = GenType::FullScreen;
	};
	RayGeneration(const PassConfig& cfg);

	~RayGeneration();

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		otcv::DescriptorSet*	fg_set = nullptr;

		// exclusive to GenType::SphericalFibonacci
		glm::vec3 probe_start;
		glm::vec3 probe_step;
		glm::mat3 probe_orientation;
		int rays_per_probe; // equal to the width of generated image
		glm::ivec3 probe_counts; // x * y * z equal to the height of generated image. powers of two in all dimensions. 

		// exclusive to GenType::FullScreen
		std::shared_ptr<PerspectiveCamera> cam;
		uint32_t width = 0;
		uint32_t height = 0;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx);

private:
	std::shared_ptr<ResourceContext>						_res;
	PassConfig::GenType										_type;
	otcv::ShaderModule*										_shader;
	otcv::ComputePipeline*									_pipeline = nullptr;

	const glm::uvec2 _compute_group_size = glm::uvec2(8, 8);
};