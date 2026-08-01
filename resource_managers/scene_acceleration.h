#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "scene_manager.hpp"
#include "glsl_reflect/ray_trace/trace_one_bounce.comp.hpp"

class SceneAcceleration {
public: 
	SceneAcceleration(
		std::shared_ptr<SceneManager> scene_mgr,
		std::shared_ptr<MeshManager> mesh_mgr,
		std::shared_ptr<MaterialManager> material_mgr);

	~SceneAcceleration() {};

	void update_tlas(uint32_t frame_id) { assert(false); }
	
	struct PerFrameAccObjects {
		std::vector<std::shared_ptr<otcv::AccelerationStructure>>	blases;
		std::shared_ptr<otcv::AccelerationStructure>				tlas;

		std::shared_ptr<otcv::SSBO<TraceOneBounceComp::InstanceInfoBuffer>>				insts;
		std::shared_ptr<otcv::SSBO<TraceOneBounceComp::GeometryInfoBuffer>>				geos;
	};
	std::vector<PerFrameAccObjects> _per_frame_objs;
};