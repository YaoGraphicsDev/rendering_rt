#include "render_queue.h"

RenderQueue::RenderQueue(
	std::shared_ptr<SceneManager> scene_mgr,
	std::shared_ptr<MeshManager> mesh_mgr,
	std::shared_ptr<MaterialManager> material_mgr,
	const std::string& mesh_preprocessor_path) {

	uint32_t n_renderables = scene_mgr->_renderable_metas.size();
	// group renderables by pass type & pipeline type
	/*
	*		Opqaue			|			Transparent		|
	*	Culled	|	Double	|	Culled		|	Double	|
	*/
	std::vector<RenderableHandle> opaque_culled, opaque_double, trans_culled, trans_double;
	for (int i = 0; i < n_renderables; ++i) {
		const RenderableMeta& rm = scene_mgr->_renderable_metas.at(i);
		const MaterialMeta& mm = material_mgr->_mat_metas.at(rm.mat.id);
		if (mm.alpha_mode == MaterialMeta::AlphaMode::Opaque || mm.alpha_mode == MaterialMeta::AlphaMode::Mask) {
			if (!mm.double_sided) {
				opaque_culled.push_back({ i });
			}
			else {
				opaque_double.push_back({ i });
			}
		}
		else if (mm.alpha_mode == MaterialMeta::AlphaMode::Blend) {
			if (!mm.double_sided) {
				trans_culled.push_back({ i });
			}
			else {
				trans_double.push_back({ i });
			}
		}
		else {
			assert(false);
		}
	}
	auto append_order = [&](std::vector<RenderableHandle>& group) -> OrderRange {
		OrderRange range;
		range.start = _order.size();
		_order.insert(_order.end(), group.begin(), group.end());
		range.count = _order.size() - range.start;
		return range;
	};
	
	OrderRange oc_range = append_order(opaque_culled);
	if (oc_range.count > 0) {
		_order_range_map[PassType::Opaque][PipelineVariant::BackFaceCulled] = _order_ranges.size();
		_order_ranges.push_back(oc_range);
	}
	OrderRange od_range = append_order(opaque_double);
	if (od_range.count > 0) {
		_order_range_map[PassType::Opaque][PipelineVariant::DoubleSided] = _order_ranges.size();
		_order_ranges.push_back(od_range);
	}
	OrderRange tc_range = append_order(trans_culled);
	if (tc_range.count > 0) {
		_order_range_map[PassType::Transparent][PipelineVariant::BackFaceCulled] = _order_ranges.size();
		_order_ranges.push_back(tc_range);
	}
	OrderRange td_range = append_order(trans_double);
	if (td_range.count > 0) {
		_order_range_map[PassType::Transparent][PipelineVariant::DoubleSided] = _order_ranges.size();
		_order_ranges.push_back(td_range);
	}

	// generate aabbs
	_mesh_prep.reset(new MeshPreprocessor(mesh_preprocessor_path));
	// reorder vertex offsets and counts
	std::vector<uint32_t> vertex_offsets;
	std::vector<uint32_t> vertex_counts;
	for (uint32_t i = 0; i < n_renderables; ++i) {
		const RenderableMeta& rm = scene_mgr->_renderable_metas.at(i);
		const auto& segment = mesh_mgr->_mesh_segments.at(rm.mesh.id);
		vertex_offsets.push_back(segment.vertex_start);
		vertex_counts.push_back(segment.vertex_count);
	}
	_mesh_prep->generate_aabb(
		mesh_mgr->_vb->buffers[0],
		vertex_offsets,
		vertex_counts,
		otcv::ResourceState::VertexRead,
		otcv::ResourceState::VertexRead,
		otcv::ResourceState::ComputeSSBORead);
}