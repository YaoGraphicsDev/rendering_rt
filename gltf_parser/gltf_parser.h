#pragma once

#include "resource_managers/resource_context.h"

#include <string>

bool load_gltf(
	const std::string& filename,
	std::shared_ptr<SceneManager> scene_mngr,
	std::shared_ptr<MaterialManager> mat_mngr,
	std::shared_ptr<MeshManager> mesh_mngr);