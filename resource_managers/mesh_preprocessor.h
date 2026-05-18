#pragma once
#include "otcv.h"
#include "otcv_utils.h"

class MeshPreprocessor {
public:
	MeshPreprocessor(const std::string& shader_path);
	
	~MeshPreprocessor();

	void generate_aabb(
		otcv::Buffer* positions,
		const std::vector<uint32_t>& vertex_offsets,
		const std::vector<uint32_t>& vertex_counts,
		otcv::ResourceState position_source_state,
		otcv::ResourceState position_target_state,
		otcv::ResourceState aabb_buffer_target_state);

	std::shared_ptr<otcv::SSBO> AABB_SSBO() { return _aabb_ssbo; }

private:
	otcv::ShaderBlob _mesh_presprocess_blob;
	otcv::ComputePipeline* _aabb_pipeline;

	std::shared_ptr<otcv::NaiveExpandableDescriptorPool> _mesh_preprocess_desc_pool;

	otcv::DescriptorSet* _aabb_in_desc_set;
	otcv::DescriptorSet* _aabb_out_desc_set;
	std::shared_ptr<otcv::SSBO> _mesh_info_ssbo;
	std::shared_ptr<otcv::SSBO> _aabb_ssbo;

	otcv::CommandBuffer* _cmd_buf;

	const uint32_t _compute_group_size = 64;
};