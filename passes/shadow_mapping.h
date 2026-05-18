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
		otcv::Buffer*			fg_indirect_cmd = nullptr;
		otcv::SSBOLayout		indirect_cmd_layout;
		otcv::Buffer*			fg_indirect_count = nullptr;
		otcv::SSBOLayout		indirect_count_layout;

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
	otcv::DescriptorSet*									_obj_desc_set = nullptr;
};
