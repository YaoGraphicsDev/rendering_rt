#include "mesh_manager.hpp"

#include <iostream>
#include <cassert>
#include <glm/glm.hpp>

MeshManager::~MeshManager() {
	delete _ib;
	delete _vb;
}

BufferHandle MeshManager::add_buffer(BufferMeta bm, const uint8_t* content) {
	_buf_metas.push_back(bm);
	_buf_contents.emplace_back();
	size_t content_size = full_size(bm);
	if (content_size == 0) {
		return { INVALID_MANAGER_HANDLE_ID };
	}
	_buf_contents.back().resize(content_size);
	std::memcpy(_buf_contents.back().data(), content, content_size);
	assert(_buf_metas.size() == _buf_contents.size());
	return { (int)_buf_metas.size() - 1 };
}

void MeshManager::bindless_build() {
	// build index buffer
	size_t n_indices_total = 0;
	std::vector<uint32_t> mesh_index_offsets;
	std::vector<uint32_t> mesh_index_counts;

	for (const MeshMeta& mm : _mesh_metas) {
		mesh_index_offsets.push_back(n_indices_total);
		mesh_index_counts.push_back(_buf_metas.at(mm.ib.id).ele_count);
		n_indices_total += mesh_index_counts.back();

		if (n_indices_total > std::numeric_limits<uint32_t>::max()) {
			std::cout << "MeshManager::bindless_build() error: total index count exceed uint32_t limit" << std::endl;
			assert(false);
			return;
		}
	}

	// index type uint16_t was guaranteed by add_mesh()
	std::vector<uint16_t> indices;
	indices.reserve(n_indices_total);
	// append
	for (const MeshMeta& mm : _mesh_metas) {
		const std::vector<uint8_t>& ib_content = _buf_contents.at(mm.ib.id);
		assert(ib_content.size() % sizeof(uint16_t) == 0);
		size_t old_size = indices.size();
		indices.resize(old_size + ib_content.size() / sizeof(uint16_t));
		std::memcpy(indices.data() + old_size,
			ib_content.data(),
			ib_content.size());
	}
	// upload
	{
		otcv::BufferBuilder ibb;
		ibb.size(indices.size() * sizeof(uint16_t))
			.usage(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) // ray tracing in compute shader 
			.host_access(otcv::BufferBuilder::Access::Invisible);
		_ib = new otcv::Buffer(ibb);
		_ib->populate_async(indices.data(), otcv::Buffer::SyncType::GPUBarrier, otcv::ResourceState::IndexRead, otcv::ResourceState::Created);
	}


	// build vertex buffer
	uint32_t n_vertices_total = 0;
	std::vector<uint32_t> mesh_vertex_offsets;
	std::vector<uint32_t> mesh_vertex_counts;

	for (const MeshMeta& mm : _mesh_metas) {
		const VertexBufferMeta& vbm = _vb_metas.at(mm.vb.id);

		// sanity checks
		const std::vector<uint8_t>& pos = _buf_contents.at(vbm.position.id);
		assert(!pos.empty());
		if (vbm.normal.id >= 0) {
			assert(_buf_metas[vbm.position.id].ele_count == _buf_metas[vbm.normal.id].ele_count);
		}
		if (vbm.tangent.id >= 0) {
			assert(_buf_metas[vbm.position.id].ele_count == _buf_metas[vbm.tangent.id].ele_count);
		}
		if (vbm.uv0.id >= 0) {
			assert(_buf_metas[vbm.position.id].ele_count == _buf_metas[vbm.uv0.id].ele_count);
		}
		assert(vbm.uv1.id < 0);

		mesh_vertex_offsets.push_back(n_vertices_total);
		mesh_vertex_counts.push_back(_buf_metas.at(vbm.position.id).ele_count);
		n_vertices_total += mesh_vertex_counts.back();

		if (n_vertices_total > std::numeric_limits<uint32_t>::max()) {
			std::cout << "MeshManager::bindless_build() error: total vertex count exceed uint32_t limit" << std::endl;
			assert(false);
			return;
		}
	}

	std::vector<glm::vec3> positions;
	positions.reserve(n_vertices_total);
	std::vector<glm::vec3> normals;
	normals.reserve(n_vertices_total);
	std::vector<glm::vec2> uv0s;
	uv0s.reserve(n_vertices_total);
	std::vector<glm::vec4> tangents;
	tangents.reserve(n_vertices_total);
	// append
	for (const MeshMeta& mm : _mesh_metas) {
		const VertexBufferMeta& vbm = _vb_metas.at(mm.vb.id);
		uint32_t n_vertices = _buf_metas.at(vbm.position.id).ele_count;
		// positions
		{
			const std::vector<uint8_t>& pos_content = _buf_contents.at(vbm.position.id);
			assert(pos_content.size() % sizeof(glm::vec3) == 0); // put all of these checks in add_mesh()
			size_t old_size = positions.size();
			positions.resize(old_size + pos_content.size() / sizeof(glm::vec3));
			std::memcpy(positions.data() + old_size,
				pos_content.data(),
				pos_content.size());
		}
		// normals
		if (vbm.normal.id >= 0) {
			const std::vector<uint8_t>& normal_content = _buf_contents.at(vbm.normal.id);
			assert(normal_content.size() % sizeof(glm::vec3) == 0);
			size_t old_size = normals.size();
			normals.resize(old_size + normal_content.size() / sizeof(glm::vec3));
			std::memcpy(normals.data() + old_size,
				normal_content.data(),
				normal_content.size());
		}
		else {
			normals.insert(normals.end(), n_vertices, glm::vec3(0.0f, 0.0f, 0.0f));
		}
		// tangents
		if (vbm.tangent.id >= 0) {
			const std::vector<uint8_t>& tangent_content = _buf_contents.at(vbm.tangent.id);
			assert(tangent_content.size() % sizeof(glm::vec4) == 0);
			size_t old_size = tangents.size();
			tangents.resize(old_size + tangent_content.size() / sizeof(glm::vec4));
			std::memcpy(tangents.data() + old_size,
				tangent_content.data(),
				tangent_content.size());
		}
		else {
			tangents.insert(tangents.end(), n_vertices, glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
		}
		// uv0s
		if (vbm.uv0.id >= 0) {
			const std::vector<uint8_t>& uv0_content = _buf_contents.at(vbm.uv0.id);
			assert(uv0_content.size() % sizeof(glm::vec2) == 0);
			size_t old_size = uv0s.size();
			uv0s.resize(old_size + uv0_content.size() / sizeof(glm::vec2));
			std::memcpy(uv0s.data() + old_size,
				uv0_content.data(),
				uv0_content.size());
		}
		else {
			uv0s.insert(uv0s.end(), n_vertices, glm::vec2(0.0f, 0.0f));
		}
	}
	// upload
	{
		otcv::VertexBufferBuilder vb_builder;
		{
			// position
			otcv::BufferBuilder b_builder;
			b_builder
				.size(positions.size() * sizeof(glm::vec3))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) // to generate AABB
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder);
			vb_builder.add_attribute(0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
		}
		{
			// normal
			otcv::BufferBuilder b_builder;
			b_builder
				.size(normals.size() * sizeof(glm::vec3))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) // ray tracing in compute shader 
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder);
			vb_builder.add_attribute(1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(glm::vec3));
		}
		{
			// uv0
			otcv::BufferBuilder b_builder;
			b_builder
				.size(uv0s.size() * sizeof(glm::vec2))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) // ray tracing in compute shader 
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder);
			vb_builder.add_attribute(2, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2));
		}
		{
			otcv::BufferBuilder b_builder;
			b_builder
				.size(tangents.size() * sizeof(glm::vec4))
				.usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT)
				.host_access(otcv::BufferBuilder::Access::Invisible);
			vb_builder.add_binding(b_builder);
			vb_builder.add_attribute(3, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(glm::vec4));
		}
		_vb = new otcv::VertexBuffer(vb_builder);
		_vb->buffers[0]->populate_async(positions.data(), otcv::Buffer::SyncType::GPUBarrier, otcv::ResourceState::VertexRead, otcv::ResourceState::Created);
		_vb->buffers[1]->populate_async(normals.data(), otcv::Buffer::SyncType::GPUBarrier, otcv::ResourceState::VertexRead, otcv::ResourceState::Created);
		_vb->buffers[2]->populate_async(uv0s.data(), otcv::Buffer::SyncType::GPUBarrier, otcv::ResourceState::VertexRead, otcv::ResourceState::Created);
		_vb->buffers[3]->populate_async(tangents.data(), otcv::Buffer::SyncType::GPUBarrier, otcv::ResourceState::VertexRead, otcv::ResourceState::Created);
	}

	// Build data segment
	assert(mesh_index_counts.size() == mesh_vertex_offsets.size());
	assert(mesh_index_offsets.size() == mesh_vertex_offsets.size());
	for (uint32_t i = 0; i < mesh_index_counts.size(); ++i) {
		MeshDataSegment segment;
		segment.index_start = mesh_index_offsets[i];
		segment.index_count = mesh_index_counts[i];
		segment.vertex_start = mesh_vertex_offsets[i];
		segment.vertex_count = mesh_vertex_counts[i];
		_mesh_segments.push_back(segment);
	}
}
