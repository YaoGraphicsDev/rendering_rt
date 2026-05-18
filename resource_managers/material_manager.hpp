#pragma once

#include <vector>
#include <string>
#include <cassert>
#include <memory>
#include <glm/glm.hpp>

#include "otcv.h"
#include "otcv_utils.h"
#include "manager_handles.h"

// sampler
struct SamplerMeta {
	enum class Filter {
		Linear,
		Nearest
	};
	Filter mag_filter = Filter::Linear;
	Filter min_filter = Filter::Linear;
	Filter mipmap_mode = Filter::Linear;
	enum class Wrap {
		Repeat,
		MirroredRepeat,
		ClampToEdge
	};
	Wrap wrap_u = Wrap::Repeat;
	Wrap wrap_v = Wrap::Repeat;
	Wrap wrap_w = Wrap::Repeat;
};


// image
struct ImageMeta {
	std::string uri;
	int width;
	int height;
	int channels;
	int bits_per_channel;
	enum class SwizzleType {
		None,
		BGR
	};
	SwizzleType swizzle = SwizzleType::None;

	enum class ColorEncoding {
		Linear = 0,
		SRGB
	};
	ColorEncoding color_encoding = ColorEncoding::Linear;
};

// texture
struct TextureMeta {
	SamplerHandle sampler;
	ImageHandle image;
};

struct MaterialMeta {
	std::string name;

	glm::vec4 base_color_factor = glm::vec4(1.0f);
	float metallic_factor = 1.0f;
	float roughness_factor = 1.0f;
	float normal_scale = 1.0f;
	float occlusion_strength = 0.0f;

	enum class AlphaMode {
		Opaque = 0,
		Mask,
		Blend
	};
	AlphaMode alpha_mode = AlphaMode::Opaque;
	float alpha_cutoff = 0.5f;
	bool double_sided = false;

	TextureHandle base_color; // should allow null values
	TextureHandle metallic_roughness;
	TextureHandle normal;
	TextureHandle occlusion;
	TextureHandle emissive;
};

class MaterialManager {
public:
	~MaterialManager();

	SamplerHandle add_sampler(SamplerMeta sm) {
		_samp_metas.push_back(sm);
		return { (int)_samp_metas.size() - 1 };
	}

	// use move semantics on pixel_data
	ImageHandle add_image(ImageMeta im, const uint8_t* pixel_content) {
		_img_metas.push_back(im);
		_img_contents.emplace_back();
		size_t size_in_bytes = im.width * im.height * im.channels * (im.bits_per_channel / 8);
		_img_contents.back().resize(size_in_bytes);
		std::memcpy(_img_contents.back().data(), pixel_content, size_in_bytes);
		assert(_img_metas.size() == _img_contents.size());
		return { (int)_img_metas.size() - 1 };
	}

	TextureHandle add_texture(TextureMeta tm) {
		_tex_metas.push_back(tm);
		return { (int)_tex_metas.size() - 1 };
	}

	MaterialHandle add_material(MaterialMeta mm) {
		_mat_metas.push_back(mm);
		return { (int)_mat_metas.size() - 1 };
	}

	void bindless_build();

	otcv::Image* upload_image_async(uint32_t id);

	VkFormat choose_format(int channels, int bits_per_channel, ImageMeta::ColorEncoding encoding);

	bool map_sampler_meta(SamplerMeta meta, otcv::SamplerBuilder& builder);

	// CPU objects
	std::vector<SamplerMeta>			_samp_metas;
	std::vector<ImageMeta>				_img_metas;
	std::vector<std::vector<uint8_t>>	_img_contents;
	std::vector<TextureMeta>			_tex_metas;
	std::vector<MaterialMeta>			_mat_metas;

	// GPU objects
	std::vector<otcv::Image*>								_imgs;
	std::vector<otcv::Sampler*>								_samps;
	std::shared_ptr<otcv::StaticUBOArray>							_mat_ubos = nullptr;


	// pack(channels, bit_depth) | color_space, VkFormat
	const std::map<uint32_t, VkFormat> format_lut = {
		{otcv::pack(4, 8,	(uint8_t)ImageMeta::ColorEncoding::SRGB,	0),	VK_FORMAT_R8G8B8A8_SRGB},
		{otcv::pack(4, 8,	(uint8_t)ImageMeta::ColorEncoding::Linear,	0),	VK_FORMAT_R8G8B8A8_UNORM},
		{otcv::pack(3, 8,	(uint8_t)ImageMeta::ColorEncoding::SRGB,	0),	VK_FORMAT_R8G8B8_SRGB},
		{otcv::pack(3, 8,	(uint8_t)ImageMeta::ColorEncoding::Linear,	0),	VK_FORMAT_R8G8B8_UNORM},
	};
};