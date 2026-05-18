#pragma once

#include "material_manager.hpp"
#include "mesh_manager.hpp"
#include "scene_manager.hpp"
#include "render_queue.h"
#include "pipeline_cache.hpp"

struct ResourceContext {
	std::shared_ptr<SceneManager>		scene_mgr = nullptr;
	std::shared_ptr<MeshManager>		mesh_mgr = nullptr;
	std::shared_ptr<MaterialManager>	material_mgr = nullptr;
	std::shared_ptr<RenderQueue>		render_queue = nullptr;
	// std::shared_ptr<PipelineCache>		pipeline_cache = nullptr;
};