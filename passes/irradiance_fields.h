#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "glm/glm.hpp"
#include "common/camera.h"

class IrradianceFields {
public:
	struct PassConfig {
		std::string	shader_dir;
		glm::uvec3	probe_counts;			// number of probes in x, y, z directions, should be power of 2
		uint32_t	probe_size_irrad;		// size of an individual probe texture, on atlas. 
		uint32_t	probe_size_depth;		// size of an individual probe texture, on atlas.
		glm::vec3	probe_start;			// starting position of a corner probe;
		glm::vec3	probe_step;				// distance between probes, in x, y, z directions
		uint32_t	rays_per_probe;
		float		depth_sharpness;
		float		hysteresis;
		VkFormat	direct_lit_format;		// indirect light will be added to direct lit image

		bool visualize_probes = false;
		//  Ignore these if probe visualization is not required
		VkFormat	probe_visualize_color_format;
		VkFormat	probe_visualize_depth_format;
	};
	IrradianceFields(const PassConfig& cfg);

	~IrradianceFields();

	enum class AtlasType : uint32_t {
		Irradiance = 0,
		Depth
	};

	struct UpdateProbesContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		otcv::DescriptorSet*	fg_set = nullptr;
		AtlasType				atlas_type;
		uint32_t				src_atlas_index;
	};
	void update_probes_commands(UpdateProbesContext& ctx);

	struct VisualizeProbesContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		glm::mat4				proj_view = glm::mat4(1.0f);
		otcv::DescriptorSet*	fg_set = nullptr;
		AtlasType				visualize_type;
		float					probe_radius;
		uint32_t				sample_atlas_index;

		float width;
		float height;
	};
	void visualize_probes_commands(VisualizeProbesContext& ctx);

	struct SampleFieldsContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		otcv::DescriptorSet*	fg_set = nullptr;
		std::shared_ptr<PerspectiveCamera> cam;
		float					normal_bias = 0.05f;
		uint32_t				sample_atlas_index;

		float width;
		float height;
	};
	void sample_fields_commands(SampleFieldsContext& ctx);


	//glm::uvec2 get_atlas_size(AtlasType type) {
	//	if (type == AtlasType::Irradiance) {
	//		return _irrad_atlas.size;
	//	}
	//	else if (type == AtlasType::Depth) {
	//		return _depth_atlas.size;
	//	}
	//	else {
	//		assert(false);
	//	}
	//}

private:
	otcv::ShaderBlob		_shader_blob;

	glm::uvec2				_irrad_atlas_size;
	glm::uvec2				_depth_atlas_size;

	otcv::ComputePipeline*	_update_probes_pipeline;
	otcv::GraphicsPipeline* _visualize_probes_pipeline;
	otcv::GraphicsPipeline* _sample_fields_pipeline;

	otcv::Sampler* _sampler_atlas;
	std::shared_ptr<otcv::NaiveExpandableDescriptorPool>	_desc_pool;
	otcv::DescriptorSet* _sampler_desc_set;

	PassConfig	_cfg;
	glm::uvec2	_atlas_probes_counts;	// number of probes on each side of the atlas

	otcv::VertexBuffer* _screen_quad_vb;
	otcv::VertexBuffer* _icosphere_vb;
};