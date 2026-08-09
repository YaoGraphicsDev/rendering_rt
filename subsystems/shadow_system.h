#pragma once

#include "otcv.h"
#include "frame_graph_application.h"
#include "resource_managers/resource_context.h"
#include "common/camera.h"

class ShadowMapSystem {
public:
	ShadowMapSystem(std::shared_ptr<ResourceContext> res_ctx, std::shared_ptr<PerspectiveCamera> cam);

	~ShadowMapSystem() {};

	void update();

	struct FGResources {
		otcv::fg::ResourceHandle cascaded;
		otcv::fg::ResourceHandle cube;
	};
	FGResources commands(otcv::fg::Application* fg_app);

	std::vector<CSMUtils::CascadeContext> cascade_contexts() {
		return _csm_ctxs;
	}

private:
	std::shared_ptr<ResourceContext>		_res_ctx;
	std::shared_ptr<PerspectiveCamera>		_cam;
	std::vector<CSMUtils::CascadeContext>	_csm_ctxs;		// empty means no CSM
	LightMeta::ShadowSettings				_csm_settings;
};