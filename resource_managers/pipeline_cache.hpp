#pragma once

#include "otcv.h"
#include "otcv_utils.h"

class PipelineCache {
public:
	PipelineCache() { assert(false); };

	~PipelineCache() {
		for (auto& p : _pipeline_map) {
			delete p.second;
		}
	}
	
	otcv::GraphicsPipeline* acquire(const otcv::GraphicsPipelineBuilder& gpb) {
		assert(false);
		auto iter = _pipeline_map.find(gpb);
		if (iter == _pipeline_map.end()) {
			otcv::GraphicsPipelineBuilder gpb_copy = gpb;
			otcv::GraphicsPipeline* gp = new otcv::GraphicsPipeline(gpb_copy); // gpb_copy will get moved
			_pipeline_map[gpb] = gp;
			return gp;
		}
		else {
			return iter->second;
		}
	}

private:
	std::map<otcv::GraphicsPipelineBuilder, otcv::GraphicsPipeline*, otcv::GraphicsPipelineBuilderLess> _pipeline_map;
};