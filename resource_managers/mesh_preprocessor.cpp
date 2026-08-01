#include "mesh_preprocessor.h"
#include "common/render_global_types.h"

using namespace otcv;

MeshPreprocessor::MeshPreprocessor(const std::string& shader_path) {
	_mesh_presprocess_blob = load_shaders_from_dir(shader_path);
	_aabb_pipeline = ComputePipeline::create(_mesh_presprocess_blob["aabb.comp"]);

	_mesh_preprocess_desc_pool.reset(new NaiveExpandableDescriptorPool);
	_aabb_in_desc_set = _mesh_preprocess_desc_pool->allocate(_aabb_pipeline->desc_set_layouts[DescriptorSetRate::ComputeRead]);
	_aabb_out_desc_set = _mesh_preprocess_desc_pool->allocate(_aabb_pipeline->desc_set_layouts[DescriptorSetRate::ComputeWrite]);

	_cmd_buf = get_context().command_pool->allocate();
}

MeshPreprocessor::~MeshPreprocessor() {
	_aabb_pipeline->destroy();
}

void MeshPreprocessor::generate_aabb(
	Buffer* positions,
	const std::vector<uint32_t>& vertex_offsets,
	const std::vector<uint32_t>& vertex_counts,
	ResourceState position_source_state,
	ResourceState position_target_state,
	ResourceState aabb_buffer_target_state) {

	assert(vertex_offsets.size() == vertex_counts.size());
	uint32_t n_obj = vertex_offsets.size();

	_aabb_in_desc_set->bind_buffer(0, positions);

	_mesh_info_ssbo.reset(new SSBO<AabbComp::MeshInfoBuffer>(n_obj));

	std::vector<AabbComp::MeshInfo> mesh_info_buf(n_obj);
	for (uint32_t i = 0; i < n_obj; ++i) {
		mesh_info_buf[i].firstVertex = vertex_offsets[i];
		mesh_info_buf[i].vertexCount = vertex_counts[i];
	}
	_mesh_info_ssbo->full_sync_write(mesh_info_buf);

	_aabb_in_desc_set->bind_buffer(1, _mesh_info_ssbo->_buf);

	_aabb_ssbo.reset(new SSBO<AabbComp::AABBBuffer>(n_obj));
	_aabb_out_desc_set->bind_buffer(0, _aabb_ssbo->_buf);

	_cmd_buf->begin(true);
	_cmd_buf->cmd_bind_compute_pipeline(_aabb_pipeline);
	_cmd_buf->cmd_bind_descriptor_set(_aabb_pipeline, _aabb_in_desc_set, DescriptorSetRate::ComputeRead);
	_cmd_buf->cmd_bind_descriptor_set(_aabb_pipeline, _aabb_out_desc_set, DescriptorSetRate::ComputeWrite);
	if (position_source_state != ResourceState::ComputeSSBORead) {
		_cmd_buf->cmd_buffer_memory_barrier(
			positions,
			position_source_state,
			ResourceState::ComputeSSBORead);
	}

	for (uint32_t i = 0; i < n_obj; ++i) {
		_cmd_buf->cmd_push_constant(_aabb_pipeline, "meshId", &i);
		_cmd_buf->cmd_dispatch(calc_group_count(vertex_counts[i], _compute_group_size), 1, 1);
	}

	_cmd_buf->cmd_buffer_memory_barrier(
		positions,
		ResourceState::ComputeSSBORead,
		position_target_state);
	_cmd_buf->cmd_buffer_memory_barrier(
		_aabb_ssbo->_buf,
		ResourceState::ComputeSSBOWrite,
		aabb_buffer_target_state);
	_cmd_buf->end();

	QueueSubmit submit;
	submit.batch()
		.add_command_buffer(_cmd_buf)
		.end();
	get_context().queue->submit(submit);
}

