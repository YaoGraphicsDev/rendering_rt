#pragma once

#include "otcv.h"
#include "mesh_preprocessor.h"
#include "manager_handles.h"

#include <vector>
#include <string>
#include <memory>
#include <iostream>

struct BufferMeta {
	uint32_t			comp_per_ele = 1;
	enum class ComponentType {
		UInt16,
		Float,
	};
	ComponentType		comp_type = ComponentType::UInt16;
	uint32_t			ele_count;
};

struct VertexBufferMeta {
	BufferHandle position;
	BufferHandle normal;
	BufferHandle tangent;
	BufferHandle uv0;
	BufferHandle uv1;
};

struct MeshMeta {
	VertexBufferHandle	vb;
	BufferHandle		ib;
};

class MeshManager {
public:
	MeshManager() {};

	~MeshManager();

	BufferHandle add_buffer(BufferMeta bm, const uint8_t* content);

	VertexBufferHandle add_vertex_buffer(VertexBufferMeta vbm) {
		_vb_metas.push_back(vbm);
		return { (int)_vb_metas.size() - 1 };
	}

	MeshHandle add_mesh(MeshMeta mm) {
		if (_buf_metas.at(mm.ib.id).comp_type != BufferMeta::ComponentType::UInt16 || _buf_metas.at(mm.ib.id).comp_per_ele != 1) {
			std::cout << "MeshManager::add_mesh() error: Supports index type UInt16 only" << std::endl;
			assert(false);
			return { INVALID_MANAGER_HANDLE_ID };
		}
		_mesh_metas.push_back(mm);
		return { (int)_mesh_metas.size() - 1 };
	}

	void bindless_build();

	// CPU objects
	std::vector<BufferMeta>				_buf_metas;
	std::vector<std::vector<uint8_t>>	_buf_contents;
	std::vector<VertexBufferMeta>		_vb_metas;
	std::vector<MeshMeta>				_mesh_metas;
	struct MeshDataSegment {
		uint32_t index_start;
		uint32_t index_count;
		uint32_t vertex_start;
		uint32_t vertex_count;
	};
	std::vector<MeshDataSegment>		_mesh_segments;

	// GPU objects
	otcv::Buffer*		_ib = nullptr;
	otcv::VertexBuffer* _vb = nullptr;

	const std::map<uint32_t, VkFormat> format_lut = {
		{otcv::pack(2, (uint8_t)BufferMeta::ComponentType::Float),	VK_FORMAT_R32G32_SFLOAT},
		{otcv::pack(3, (uint8_t)BufferMeta::ComponentType::Float),	VK_FORMAT_R32G32B32_SFLOAT},
		{otcv::pack(4, (uint8_t)BufferMeta::ComponentType::Float),	VK_FORMAT_R32G32B32A32_SFLOAT},
	};

	const std::map<uint32_t, uint32_t> component_size = {
		{(uint32_t)BufferMeta::ComponentType::Float,	4},
		{(uint32_t)BufferMeta::ComponentType::UInt16,	2},
	};

	// in bytes
	uint32_t element_size(const BufferMeta& bm) {
		return bm.comp_per_ele * component_size.at((uint32_t)bm.comp_type);
	}

	// in bytes
	uint32_t full_size(const BufferMeta& bm) {
		return element_size(bm) * bm.ele_count;
	}
};