#pragma once
#include <vector>
#include "glm/glm.hpp"
#include "otcv.h"

class ShadowNoiseTexture {
public:
	typedef std::vector<std::vector<glm::vec2>> Grid;

	static Grid strat_noise_2d_grid(uint32_t n_strata);

	// inner vector groups a ring of stratas together, outer vector groups all the rings
	static Grid strat_noise_2d_disk(uint32_t n_strata);

	static otcv::Image* disk_noise_texture(uint32_t tile_size, uint32_t n_strata_per_dim);
};