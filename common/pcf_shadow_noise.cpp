#include "pcf_shadow_noise.h"
#include <random>
#include "glm/gtc/constants.hpp"

ShadowNoiseTexture::Grid ShadowNoiseTexture::strat_noise_2d_grid(uint32_t n_strata) {
    std::random_device rd;
    std::mt19937 gen(rd()); // Mersenne Twister RNG
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    Grid noise;
    for (uint32_t row = 0; row < n_strata; ++row) {
        noise.emplace_back();
        for (uint32_t col = 0; col < n_strata; ++col) {
            glm::vec2 norm_noise(dist(gen), dist(gen));

            /////////////////////////////////
            // glm::vec2 norm_noise(0.5f + 0.05f * dist(gen), 0.5f + 0.05f * dist(gen));
            /////////////////////////////////
            
            glm::vec2 strat_base(col, row);
            noise.back().push_back(glm::clamp((strat_base + norm_noise) / glm::vec2(n_strata), glm::vec2(0.0f), glm::vec2(1.0f)));
        }
    }

    return noise;
}

//void show_grid(const NoiseTexture::Grid& grid) {
//    std::cout << "disk:" << std::endl;
//    for (auto& ring : grid) {
//        std::cout << "ring:" << std::endl;
//        for (auto& ele : ring) {
//            std::cout << ele.x << ", " << ele.y << "\t";
//        }
//        std::cout << std::endl;
//    }
//}

ShadowNoiseTexture::Grid ShadowNoiseTexture::strat_noise_2d_disk(uint32_t n_strata) {
    Grid grid_noise = std::move(strat_noise_2d_grid(n_strata));

    // show_grid(grid_noise);

    Grid disk_noise;
    for (uint32_t ring = 0; ring < n_strata; ++ring) {
        disk_noise.emplace_back();
        for (uint32_t sector = 0; sector < n_strata; ++sector) {
            float r = glm::sqrt(grid_noise[ring][sector].y);
            float phi = glm::two_pi<float>() * grid_noise[ring][sector].x;
            float cos = glm::clamp(glm::cos(phi), -1.0f, 1.0f);
            float sin = glm::clamp(glm::sin(phi), -1.0f, 1.0f);
            disk_noise.back().push_back(glm::vec2(r * cos, r * sin));
        }
    }

    return disk_noise;
}

otcv::Image* ShadowNoiseTexture::disk_noise_texture(uint32_t tile_size, uint32_t n_strata_per_dim) {
    assert(n_strata_per_dim % 2 == 0);

    std::vector<Grid> noise_3d(n_strata_per_dim * n_strata_per_dim); // number of layers
    for (Grid& layer : noise_3d) {
        for (uint32_t row = 0; row < tile_size; ++row) {
            layer.push_back(std::vector<glm::vec2>(tile_size));
        }
    }

    // traverse noise_3d for every element in a layer
    for (uint32_t row = 0; row < tile_size; ++row) {
        for (uint32_t col = 0; col < tile_size; ++col) {
            Grid disk_noise = std::move(strat_noise_2d_disk(n_strata_per_dim));
            // show_grid(disk_noise);
            // traverse disk_noise for every ring
            for (uint32_t ring = 0; ring < n_strata_per_dim; ++ring) {
                // start from the outer-most ring 
                std::vector<glm::vec2>& ring_data = disk_noise[n_strata_per_dim - ring - 1];
                // traverse ring_data for every sector
                for (uint32_t sector = 0; sector < n_strata_per_dim; ++sector) {
                    uint32_t layer = ring * n_strata_per_dim + sector;
                    noise_3d[layer][row][col] = ring_data[sector];
                }
            }
        }
    }

    std::vector<float> noise_data;
    // reorganize noise_3d
    for (uint32_t layer = 0; layer < noise_3d.size() / 2; ++layer) {
        for (uint32_t row = 0; row < tile_size; ++row) {
            for (uint32_t col = 0; col < tile_size; ++col) {
                 noise_data.push_back(noise_3d[layer * 2][row][col].x);
                 noise_data.push_back(noise_3d[layer * 2][row][col].y);
                 noise_data.push_back(noise_3d[layer * 2 + 1][row][col].x);
                 noise_data.push_back(noise_3d[layer * 2 + 1][row][col].y);

                /////////////////////
 /*               float r1 = glm::length(noise_3d[layer * 2][row][col]);
                noise_data.push_back(r1);
                noise_data.push_back(r1);
                float r2 = glm::length(noise_3d[layer * 2 + 1][row][col]);
                noise_data.push_back(r2);
                noise_data.push_back(r2);*/

               //float r1 = glm::length(noise_3d[layer * 2][row][col]);
               //float a1 = glm::degrees(glm::atan(noise_3d[layer * 2][row][col].y / r1, noise_3d[layer * 2][row][col].x / r1)) / 180.0f;
               //noise_data.push_back(r1);
               //noise_data.push_back(a1);
               //float r2 = glm::length(noise_3d[layer * 2 + 1][row][col]);
               //float a2 = glm::degrees(glm::atan(noise_3d[layer * 2 + 1][row][col].y / r2, noise_3d[layer * 2 + 1][row][col].x / r2)) / 180.0f;
               //noise_data.push_back(r2);
               //noise_data.push_back(a2);

            }
        }
    }

    // convert to snorm
    std::vector<int8_t> noise_data_snorm(noise_data.size());
    for (uint32_t i = 0; i < noise_data.size(); ++i) {
        noise_data_snorm[i] = int8_t(std::round(glm::clamp(noise_data[i], -1.0f, 1.0f) * 127.0f));// static_cast<uint8_t>(std::round(std::clamp(noise_data[i], -1.0f, 1.0f) * 255.0f));
    }

    otcv::Image* texture;
    texture = otcv::ImageBuilder()
        .image_type(VK_IMAGE_TYPE_3D)
        .view_type(VK_IMAGE_VIEW_TYPE_3D)
        .size(tile_size, tile_size, n_strata_per_dim * n_strata_per_dim / 2) // cram 2 sample points into 1 vec4
        .format(VK_FORMAT_R8G8B8A8_SNORM)
        .usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        .build();
    texture->populate(noise_data_snorm.data(), noise_data_snorm.size() * sizeof(int8_t), otcv::ResourceState::FragSample);

    return texture;
}