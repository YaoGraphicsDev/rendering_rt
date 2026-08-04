#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"

class ToneMapping {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		VkFormat							color_attachment_format;
	};
	ToneMapping(const PassConfig& cfg);

	~ToneMapping();

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		otcv::DescriptorSet*	fg_set = nullptr;
		float width = 0.0f;
		float height = 0.0f;
	};
	void command(CommandContext& ctx);

private:
	std::shared_ptr<ResourceContext>		_res;
	otcv::ShaderBlob						_shader_blob;
	otcv::VertexBuffer*						_screen_quad_vb;
	otcv::GraphicsPipeline*					_pipeline;
	std::shared_ptr<otcv::NaiveExpandableDescriptorPool> _desc_pool = nullptr;
};