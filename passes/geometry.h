#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"

class GeometryPass {
public:
	struct PassConfig {
		std::shared_ptr<ResourceContext>	res_context;
		std::string							shader_dir;
		std::vector<VkFormat>				color_attachment_formats;
		VkFormat							depth_attachment_format;
	};
	GeometryPass(const PassConfig& cfg);

	~GeometryPass();

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf				= nullptr;
		glm::mat4				proj				= glm::mat4(1.0f);
		glm::mat4				view				= glm::mat4(1.0f);
		otcv::Buffer*			fg_indirect_cmd		= nullptr;
		uint32_t				indirect_cmd_stride;
		otcv::Buffer*			fg_indirect_count	= nullptr;
		uint32_t				indirect_count_stride;
		uint32_t				fg_frame_id;

		float width		= 0.0f;
		float height	= 0.0f;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx);
	
private:
	std::shared_ptr<ResourceContext>										_res;
	otcv::ShaderBlob														_shader_blob;
	std::map<RenderQueue::PipelineVariant, otcv::GraphicsPipeline*>			_pipeline_map;
	otcv::PipelineLayout*													_pipeline_layout = nullptr;
	std::shared_ptr<otcv::NaiveExpandableDescriptorPool>					_desc_pool = nullptr;
	std::vector<otcv::DescriptorSet*>										_obj_desc_sets; // one per frame-in-flight
	otcv::DescriptorSet*													_material_desc_set = nullptr; // materials are immutable
};