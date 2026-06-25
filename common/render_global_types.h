#pragma once

#include <cstdint>

enum DescriptorSetRate : uint32_t {
	PerFrame = 0,
	PerObject = 1,
	PerMaterial = 2,
	ComputeRead = 1,
	ComputeWrite = 2,
	FrameGraph = 3,
};
