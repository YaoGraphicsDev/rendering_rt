#pragma once

#include "material_manager.hpp"
#include "mesh_manager.hpp"
#include "scene_manager.hpp"
#include "render_queue.h"
#include "scene_acceleration.h"

struct ResourceContext {
	std::shared_ptr<SceneManager>		scene_mgr = nullptr;
	std::shared_ptr<MeshManager>		mesh_mgr = nullptr;
	std::shared_ptr<MaterialManager>	material_mgr = nullptr;
	std::shared_ptr<RenderQueue>		render_queue = nullptr;
	std::shared_ptr<SceneAcceleration>	scene_acc = nullptr;
};