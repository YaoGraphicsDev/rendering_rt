#pragma once

#include <vector>
#include <map>

#include "mesh_manager.hpp"
#include "material_manager.hpp"

struct RenderableMeta {
	MeshHandle		mesh;
	MaterialHandle	mat;
	SceneNodeHandle	node;
};

struct SceneNodeMeta {
	SceneNodeHandle parent;
	std::string name;
	glm::mat4 local_transform = glm::mat4(1.0f);
	glm::mat4 world_transform = glm::mat4(1.0f);
	std::vector<RenderableHandle> renderables;
};

class SceneManager {
public:
	RenderableHandle add_renderable(RenderableMeta rm) {
		_renderable_metas.push_back(rm);
		return { (int)_renderable_metas.size() - 1 };
	}

	SceneNodeHandle add_scene_node(SceneNodeMeta snm);

	void bindless_build();

	std::vector<RenderableMeta>		_renderable_metas;
	std::vector<SceneNodeMeta>		_node_metas;

	std::shared_ptr<otcv::StaticUBOArray>	_renderable_ubos = nullptr;
};