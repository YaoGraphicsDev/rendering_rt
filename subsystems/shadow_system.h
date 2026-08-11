#pragma once

#include "otcv.h"
#include "frame_graph_application.h"
#include "resource_managers/resource_context.h"
#include "common/camera.h"

#include "glsl_reflect/lighting_pass/lighting.frag.hpp"

class ShadowMapSystem {
public:
	ShadowMapSystem(std::shared_ptr<ResourceContext> res_ctx, std::shared_ptr<PerspectiveCamera> cam);

	~ShadowMapSystem();

	void update(uint32_t frame_id);

	struct FGResources {
		otcv::fg::ResourceHandle cascaded;
		otcv::fg::ResourceHandle cube;
	};
	FGResources commands(otcv::fg::Application* fg_app);

	std::vector<CSMUtils::CascadeContext> cascade_contexts() {
		return _csm_ctxs;
	}

	std::shared_ptr<ResourceContext>		_res_ctx;
	std::shared_ptr<PerspectiveCamera>		_cam;

	// CSM
	std::vector<CSMUtils::CascadeContext>	_csm_ctxs;		// empty means no CSM
	LightMeta::ShadowSettings				_csm_settings;
	
	// cube
	struct AABB {
		AABB(glm::vec3 center, float half_dim) {
			this->center = center;
			this->half_dim = half_dim;
			face_centers = { // in this order: +X, -X, +Y, -Y, +Z, -Z.
				glm::vec3( 1.0f, 0.0f, 0.0f) * half_dim + center,
				glm::vec3(-1.0f, 0.0f, 0.0f) * half_dim + center,
				glm::vec3( 0.0f, 1.0f, 0.0f) * half_dim + center,
				glm::vec3( 0.0f,-1.0f, 0.0f) * half_dim + center,
				glm::vec3( 0.0f, 0.0f, 1.0f) * half_dim + center,
				glm::vec3( 0.0f, 0.0f,-1.0f) * half_dim + center,
			};
		}
		glm::vec3 center;
		float half_dim;
		std::vector<glm::vec3> face_centers;
		std::vector<glm::vec3> face_ups = {
			{0.0f,-1.0f, 0.0f},
			{0.0f,-1.0f, 0.0f},
			{0.0f, 0.0f, 1.0f},
			{0.0f, 0.0f,-1.0f},
			{0.0f,-1.0f, 0.0f},
			{0.0f,-1.0f, 0.0f},
		};
	}; // AABB that bounds the entire light influence
	std::vector<AABB>	_cube_aabbs;
	uint32_t			_cube_face_resolution;

	otcv::Image*							_pcf_noise;
	otcv::Sampler*							_sampler_shadowmap;
	otcv::Sampler*							_sampler_shadow_jitter;
	std::vector<otcv::StaticUBO<LightingFrag::ShadowUBO>>	_shadow_ubos; // one for each frame-in-flight

	struct ShadowJitterParams {
		static constexpr uint32_t	tile_size = 8;
		static constexpr uint32_t	n_strata_per_dim = 8;
		static constexpr float		radius = 0.01f;
	};
};