#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"

class ShadowMapping {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		VkFormat							depth_attachment_format;
	};
	ShadowMapping(const PassConfig& cfg);

	~ShadowMapping();

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		glm::mat4				light_proj = glm::mat4(1.0f);
		glm::mat4				light_view = glm::mat4(1.0f);
		int						layer_id = 0;
		struct CubeFace {
			bool		enabled = false;
			float		max_radial_depth;
			glm::vec3	light_pos;
		};
		CubeFace				cube_face = {};
		otcv::Buffer*			fg_indirect_cmd = nullptr;
		uint32_t				indirect_cmd_stride;
		otcv::Buffer*			fg_indirect_count = nullptr;
		uint32_t				fg_frame_id;

		float width = 0.0f;
		float height = 0.0f;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx);

private:
	std::shared_ptr<ResourceContext>						_res;
	otcv::ShaderBlob										_shader_blob;
	otcv::GraphicsPipeline*									_pipeline;
	std::shared_ptr<otcv::NaiveExpandableDescriptorPool>	_desc_pool = nullptr;
	std::vector<otcv::DescriptorSet*>						_obj_desc_sets; // one per frame-in-flight
	otcv::DescriptorSet*									_material_desc_set = nullptr; // materials are immutable
};
