#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"
#include "common/math_utils.h"
#include "common/camera.h"

#include "subsystems/shadow_system.h"

class LightingPass {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::shared_ptr<ShadowMapSystem>	shadow_sys;
		std::string							shader_dir;
		std::array<std::string, 2>			lct_luts_paths; // optional. Dont care if there's no area light in the scene
		VkFormat							color_attachment_format;
	};
	LightingPass(const PassConfig& cfg);

	~LightingPass();

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		otcv::DescriptorSet*	fg_set = nullptr;
		uint32_t				fg_frame_id;

		std::shared_ptr<PerspectiveCamera> cam;

		// glm::mat4 inv_view; // camera
		// glm::mat4 inv_proj;
		// glm::vec3						camera_pos;
		// float zNear;
		// float zFar;

		glm::uvec3 n_clusters; // light cluster dimensions
		float width = 0.0f;
		float height = 0.0f;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx);

private:
	std::shared_ptr<ResourceContext>		_res;
	otcv::ShaderBlob						_shader_blob;
	otcv::VertexBuffer*						_screen_quad_vb;
	otcv::GraphicsPipeline*					_pipeline;
	std::shared_ptr<otcv::NaiveExpandableDescriptorPool> _desc_pool = nullptr;

	std::array<otcv::Sampler*, 2>			_sampler_lct_luts;
	otcv::Sampler*							_sampler_g_buffer;

	std::vector<otcv::DescriptorSet*>		_frame_desc_set;	// one for each frame-in-flight
	std::array<otcv::Image*, 2>				_lct_luts = { nullptr, nullptr };

	// ubo field capacities. Has to match those in the shaders
	const uint32_t _max_light_count = 64;
};