#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "scene_manager.hpp"

class RenderQueue {
public:
	RenderQueue(
		std::shared_ptr<SceneManager> scene_mgr,
		std::shared_ptr<MeshManager> mesh_mgr,
		std::shared_ptr<MaterialManager> material_mgr,
		const std::string& mesh_preprocessor_path);

	~RenderQueue() {};

	enum class PassType {
		Opaque = 0,
		Transparent,
		Count
	};
	enum class PipelineVariant {
		BackFaceCulled = 0,
		DoubleSided,
		Count
	};
	bool has(PassType pt, PipelineVariant pv) {
		if (_order_range_map.count(pt) == 0) {
			return false;
		}
		if (_order_range_map.at(pt).count(pv) == 0) {
			return false;
		}
		else {
			return true;
		}
	}
	struct OrderRange {
		// indices of _renderable_order
		uint32_t start;
		uint32_t count;
	};
	uint32_t range_index_of(PassType pt, PipelineVariant pv) {
		return _order_range_map.at(pt).at(pv);
	}
	OrderRange range_of(PassType pt, PipelineVariant pv) {
		return _order_ranges.at(range_index_of(pt, pv));
	}

	std::vector<RenderableHandle>	_order;
	std::vector<OrderRange>			_order_ranges;
	std::map<PassType, std::map<PipelineVariant, uint32_t>>	_order_range_map;
	std::shared_ptr<MeshPreprocessor>	_mesh_prep = nullptr;
};