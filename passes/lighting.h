#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"
#include "common/math_utils.h"
#include "common/camera.h"

#include "glsl_reflect/lighting_pass/pbr.frag.hpp"

class LightingPass {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		std::array<std::string, 2>			lct_luts_paths; // optional. Dont care if there's no area light in the scene
		VkFormat							color_attachment_format;

		struct ShadowJitter {
			uint32_t		tile_size = 8;
			// glm::vec2		n_tiles; static_assert(false); // window size / jitter tile size. May change across frames
			uint32_t		n_strata_per_dim = 8;
			float			radius = 0.01f;
		};
		ShadowJitter jitter;

		struct CascadedShadow {
			float		blend_depth = 1.0f;
			// uint32_t	n_max_cascades = 4;	// has to match that in the shader
			uint32_t	n_cascades = 3;
			uint32_t	resolution = 2048;
		};
		CascadedShadow cascaded_shadow; // only one set cascaded shadow allowed. Usually for global direcctional light
	};
	LightingPass(const PassConfig& cfg);

	~LightingPass();

	struct UpdateContext {
		struct Shadow {
			std::vector<CSMUtils::CascadeContext>	cascades;
			// other types of shadows
		};
		Shadow shadow;

		// lights or something goes here

		float width;  // color attachment dimensions
		float height;
	};
	void update(uint32_t frame_id, UpdateContext& ctx);

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
	otcv::Sampler*							_sampler_shadowmap;
	otcv::Sampler*							_sampler_shadow_jitter;

	struct FrameDescSetContext {
		otcv::DescriptorSet*										set;
		std::shared_ptr<otcv::StaticUBO<PbrFrag::ShadowUBO>>		ubo_shadow;
	};
	std::vector<FrameDescSetContext>		_frame_desc_sets;	// one for each frame-in-flight
	std::array<otcv::Image*, 2>				_lct_luts = { nullptr, nullptr };
	otcv::Image*							_pcf_noise;

	std::shared_ptr<otcv::StaticUBO<PbrFrag::ShadowUBO>> init_shadow_ubo(const PassConfig& cfg);

	// ubo field capacities. Has to match those in the shaders
	const uint32_t _max_cascades = 4;
	const uint32_t _max_light_count = 64;
};