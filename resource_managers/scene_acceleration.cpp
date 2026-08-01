#include "scene_acceleration.h"

#include "glm/gtc/type_ptr.hpp"

using namespace otcv;

SceneAcceleration::SceneAcceleration(
	std::shared_ptr<SceneManager> scene_mgr,
	std::shared_ptr<MeshManager> mesh_mgr,
	std::shared_ptr<MaterialManager> material_mgr) {

	_per_frame_objs.resize(otcv::get_context().swapchain->mock_images.size());

	// scene node -- BLAS (1-1 relation with instance)
	// renderable -- geometry
	for (PerFrameAccObjects& frame : _per_frame_objs) {
		std::vector<TraceOneBounceComp::InstanceInfo> inst_infos;
		std::vector<TraceOneBounceComp::GeometryInfo> geo_infos;

		// BLASes, one for each scene node
		for (SceneNodeMeta& node : scene_mgr->_node_metas) {
			AccelerationStructureBuilder blas_builder;
			blas_builder
				.level(AccelerationStructureBuilder::Level::Bottom)
				.prefer(AccelerationStructureBuilder::Preference::FastTrace);

			TraceOneBounceComp::InstanceInfo inst_info;
			inst_info.firstGeometry = geo_infos.size();
			inst_info.modelInvT = mat3_to_array(glm::mat3(node.world_transform));
			inst_infos.push_back(inst_info);

			// geometries, one for each renderable
			for (RenderableHandle rh : node.renderables) {
				RenderableMeta& rm = scene_mgr->_renderable_metas.at(rh.id);
				MeshMeta& mm = mesh_mgr->_mesh_metas.at(rm.mesh.id);
				MaterialMeta& matm = material_mgr->_mat_metas.at(rm.mat.id);

				TraceOneBounceComp::GeometryInfo geo_info;
				geo_info.firstIndex = mesh_mgr->_mesh_segments.at(rm.mesh.id).index_start;
				geo_info.vertexOffset = mesh_mgr->_mesh_segments.at(rm.mesh.id).vertex_start;
				geo_info.matId = rm.mat.id;
				geo_infos.push_back(geo_info);

				auto& blas_geometry = blas_builder.add_triangles_geometry();
				if (matm.alpha_mode == MaterialMeta::AlphaMode::Opaque) {
					blas_geometry.opaque();
				}
				assert(matm.alpha_mode != MaterialMeta::AlphaMode::Blend); // cant handle blend at the moment
				// vertex positions
				{
					VertexBufferMeta& vbm = mesh_mgr->_vb_metas.at(mm.vb.id);
					BufferMeta& bm = mesh_mgr->_buf_metas.at(vbm.position.id);
					// can only process vec3 vertex buffer for the moment
					assert(bm.comp_type == BufferMeta::ComponentType::Float);
					assert(bm.comp_per_ele == 3);

					blas_geometry
						.vertex_count(bm.ele_count)
						.vertex_data(mesh_mgr->_buf_contents.at(vbm.position.id).data());
				}
				// indices
				{
					BufferMeta& bm = mesh_mgr->_buf_metas.at(mm.ib.id);
					// can only process uint16_t index buffer for the moment
					assert(bm.comp_type == BufferMeta::ComponentType::UInt16);
					assert(bm.comp_per_ele == 1);

					blas_geometry
						.triangles_count(bm.ele_count / 3)
						.index_data(mesh_mgr->_buf_contents.at(mm.ib.id).data());
				}
				blas_geometry.end();
			}

			frame.blases.push_back(std::make_shared<AccelerationStructure>(blas_builder));
		}

		frame.insts.reset(new SSBO<TraceOneBounceComp::InstanceInfoBuffer>(inst_infos.size()));
		frame.insts->full_sync_write(inst_infos);
		frame.geos.reset(new SSBO<TraceOneBounceComp::GeometryInfoBuffer>(geo_infos.size()));
		frame.geos->full_sync_write(geo_infos);

		// TLAS. Each BLAS takes one instance
		AccelerationStructureBuilder tlas_builder;
		tlas_builder
			.level(AccelerationStructureBuilder::Level::Top)
			.prefer(AccelerationStructureBuilder::Preference::FastTrace)
			.allow_update();
		auto& tlas_geometry = tlas_builder.instance_geometry();
		for (uint32_t n = 0; n < scene_mgr->_node_metas.size(); ++n) {
			tlas_geometry
				.add_instance()
				.transform(glm::value_ptr(glm::transpose(scene_mgr->_node_metas.at(n).world_transform)))
				.blas(frame.blases.at(n).get())
				.culling(false) // probes may go inside of geometry, also for double-sided geometries
				.end();
		}
		tlas_geometry.end();
		frame.tlas = std::make_shared<AccelerationStructure>(tlas_builder);
	}
	
}