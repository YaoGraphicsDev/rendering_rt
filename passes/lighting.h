#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"
#include "common/math_utils.h"

class LightingPass {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		VkFormat							color_attachment_format;

		struct ShadowJitter {
			uint32_t		tile_size = 8;
			// glm::vec2		n_tiles; static_assert(false); // window size / jitter tile size. May change across frames
			uint32_t		n_strata_per_dim = 8;
			float			radius = 0.02f;
		};
		ShadowJitter jitter;

		struct CascadedShadow {
			float		blend_depth = 1.0f;
			uint32_t	n_max_cascades = 4;	// has to match that in the shader
			uint32_t	n_cascades = 3;
			uint32_t	resolution = 2048;
		};
		CascadedShadow cascaded_shadow; // only one set cascaded shadow allowed. Usually for global direcctional light
	};
	LightingPass(const PassConfig& cfg);

	~LightingPass();

	struct UpdateContext {
		glm::mat4 inv_view; // camera
		glm::mat4 inv_proj;

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

	void update_ubo(uint32_t frame_id, otcv::StaticUBOAccess& access, const void* value) {
		_frame_desc_sets.at(frame_id).ubo->set(access, value);
	}

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		otcv::DescriptorSet*	fg_set = nullptr;
		uint32_t				fg_frame_id;
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

	std::vector<otcv::Sampler*>				_samplers;
	struct FrameDescSetContext {
		otcv::DescriptorSet*				set;
		std::shared_ptr<otcv::StaticUBO>	ubo;
	};
	std::vector<FrameDescSetContext>		_frame_desc_sets;	// one for each frame-in-flight
	otcv::Image*							_pcf_noise;

	std::shared_ptr<otcv::StaticUBO> init_ubo(const PassConfig& cfg);
};