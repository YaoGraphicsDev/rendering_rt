#pragma once

#include <vector>

#include "scene_manager.hpp"

struct ShadowMeta {
	enum class Type : uint32_t {
		Cascade = 0,
		Cube,
		HalfCube,
		Null
	};
	Type type;
	// exclusive to cascaded
	float z_near;
	float z_far;
	uint32_t n_cascades;
};

class ShadowManager {
public:
	ShadowHandle add_shadow(ShadowMeta sm) {
		_shadow_metas.push_back(sm);
	}
	
	std::vector<ShadowMeta> _shadow_metas;
};