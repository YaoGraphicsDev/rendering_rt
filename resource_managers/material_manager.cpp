#include "material_manager.hpp"

#include <iostream>

using namespace otcv;

MaterialManager::~MaterialManager() {
	for (otcv::Image* img : _imgs) {
		delete img;
	}
	for (otcv::Sampler* samp : _samps) {
		delete samp;
	}
}


void MaterialManager::bindless_build() {
	// upload images 
	_imgs.resize(_img_metas.size());
	for (uint32_t i = 0; i < _img_metas.size(); ++i) {
		_imgs[i] = upload_image_async(i);
	}
	
	// upload samplers
	_samps.resize(_samp_metas.size());
	for (uint32_t i = 0; i < _samp_metas.size(); ++i) {
		otcv::SamplerBuilder sb;
		if (!map_sampler_meta(_samp_metas[i], sb)) {
			std::cout << "MaterialManager::bindless_build() error: cannot map sampler meta" << std::endl;
			assert(false);
			return;
		}
		_samps[i] = new otcv::Sampler(sb);
	}

	// check limits and acquire ubo alignment
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(otcv::get_context().physical_device->vk_physical_device, &device_properties);
	VkPhysicalDeviceLimits limits = device_properties.limits;
	// VkDeviceSize ubo_alignment = limits.minUniformBufferOffsetAlignment;
	if (limits.maxPerStageDescriptorSampledImages < _img_metas.size()) {
		assert(false);
		std::cout << "MaterialManager::bindless_build() error: number of images = " << _img_metas.size() <<
			" exceeds maxPerStageDescriptorSampledImages = " << limits.maxPerStageDescriptorSampledImages << std::endl;
		return;
	}
	if (limits.maxPerStageDescriptorSamplers < _samp_metas.size()) {
		assert(false);
		std::cout << "MaterialManager::bindless_build() error: number of samplers = " << _samp_metas.size() <<
			" exceeds maxPerStageDescriptorSamplers = " << limits.maxPerStageDescriptorSamplers << std::endl;
		return;
	}


	// binding 0 -- material ubos
	_mat_ubos.reset(new StaticUBOArray<GeometryFrag::MaterialUBO>(_mat_metas.size()));

	// upload material data to ubo
	for (uint32_t id = 0; id < _mat_metas.size(); ++id) {
		MaterialMeta& mat_meta = _mat_metas[id];

		GeometryFrag::MaterialUBO mat_ubo;
		GeometryFrag::MaterialCfg& mat_cfg = mat_ubo.materialCfg;
		mat_cfg.baseColorFactor = vec4_to_array(mat_meta.base_color_factor);
		mat_cfg.mrnoFactor[0] = mat_meta.metallic_factor;
		mat_cfg.mrnoFactor[1] = mat_meta.roughness_factor;
		mat_cfg.mrnoFactor[2] = mat_meta.normal_scale;
		mat_cfg.mrnoFactor[3] = mat_meta.occlusion_strength;
		mat_cfg.emissiveColorStrength = vec3_to_array(mat_meta.emissive_factor * mat_meta.emissive_strength);
		mat_cfg.alphaMode = (uint32_t)mat_meta.alpha_mode;
		mat_cfg.alphaCutoff = mat_meta.alpha_cutoff;
		mat_cfg.flipNormal = mat_meta.double_sided ? 1 : 0;
		mat_cfg.unlit = mat_meta.unlit ? 1 : 0;

		GeometryFrag::TextureIds& tex_ids = mat_ubo.textureIds;
		GeometryFrag::SamplerIds& samp_ids = mat_ubo.samplerIds;

		int bc_id = mat_meta.base_color.id;
		if (bc_id < 0) {
			tex_ids.baseColorId = INVALID_MANAGER_HANDLE_ID;
			samp_ids.baseColorId = INVALID_MANAGER_HANDLE_ID;
		}
		else {
			tex_ids.baseColorId = _tex_metas.at(bc_id).image.id;
			samp_ids.baseColorId = _tex_metas.at(bc_id).sampler.id;
		}

		int n_id = mat_meta.normal.id;
		if (n_id < 0) {
			tex_ids.normalId = INVALID_MANAGER_HANDLE_ID;
			samp_ids.normalId = INVALID_MANAGER_HANDLE_ID;
		}
		else {
			tex_ids.normalId = _tex_metas.at(n_id).image.id;
			samp_ids.normalId = _tex_metas.at(n_id).sampler.id;
		}

		int mr_id = mat_meta.metallic_roughness.id;
		if (mr_id < 0) {
			tex_ids.metallicRoughnessId = INVALID_MANAGER_HANDLE_ID;
			samp_ids.metallicRoughnessId = INVALID_MANAGER_HANDLE_ID;
		}
		else {
			tex_ids.metallicRoughnessId = _tex_metas.at(mr_id).image.id;
			samp_ids.metallicRoughnessId = _tex_metas.at(mr_id).sampler.id;
		}

		int em_id = mat_meta.emissive.id;
		if (em_id < 0) {
			tex_ids.emissiveId = INVALID_MANAGER_HANDLE_ID;
			samp_ids.emissiveId = INVALID_MANAGER_HANDLE_ID;
		}
		else {
			tex_ids.emissiveId = _tex_metas.at(em_id).image.id;
			samp_ids.emissiveId = _tex_metas.at(em_id).sampler.id;
		}

		int occ_id = mat_meta.occlusion.id; assert(occ_id == INVALID_MANAGER_HANDLE_ID);

		_mat_ubos->set(id, mat_ubo);
	}

	// binding 1 -- images
	std::vector<otcv::Image*> imgs;
	for (uint32_t id = 0; id < _img_metas.size(); ++id) {
		imgs.push_back(_imgs[id]);
	}

	// binding 2 -- samplers
	std::vector<otcv::Sampler*> samps;
	for (uint32_t id = 0; id < _samp_metas.size(); ++id) {
		samps.push_back(_samps[id]);
	}
}

otcv::Image* MaterialManager::upload_image_async(uint32_t id) {
	ImageMeta& meta = _img_metas[id];
	std::vector<uint8_t>& data = _img_contents[id];

	otcv::ImageBuilder imb;
	imb.size(meta.width, meta.height, 1)
		.name(meta.uri);
	VkFormat format = choose_format(meta.channels, meta.bits_per_channel, meta.color_encoding);
	assert(format != VK_FORMAT_UNDEFINED);
	imb
		.format(format)
		.usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) // transfer src for mipmap generation
		.enable_mips();
	if (meta.swizzle == ImageMeta::SwizzleType::BGR) {
		imb.swizzle(VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_R);
	}
	else {
		assert(meta.swizzle == ImageMeta::SwizzleType::None);
	}

	otcv::Image* image = new otcv::Image(imb);
	image->populate_async(
		data.data(),
		data.size(),
		otcv::ResourceState::FragSample,
		otcv::ResourceState::Created,
		otcv::Image::SyncType::GPUBarrier);
	return image;
}

VkFormat MaterialManager::choose_format(int channels, int bits_per_channel, ImageMeta::ColorEncoding encoding) {
	uint32_t format_index = otcv::pack((uint8_t)channels, (uint8_t)bits_per_channel, (uint8_t)encoding, 0);
	auto iter = format_lut.find(format_index);
	if (iter == format_lut.end()) {
		std::cout << "MaterialManager::choose_format() error: Cannot find suitable format. channels = " << channels << ", bit_depth = " << bits_per_channel << ", encoding = " << (int)encoding << std::endl;
		return VK_FORMAT_UNDEFINED;
	}
	else {
		return iter->second;
	}
}

bool MaterialManager::map_sampler_meta(SamplerMeta meta, otcv::SamplerBuilder& builder) {
	// magFilter
	VkFilter mag;
	switch (meta.mag_filter) {
	case SamplerMeta::Filter::Linear:
		mag = VK_FILTER_LINEAR;
		break;
	case SamplerMeta::Filter::Nearest:
		mag = VK_FILTER_NEAREST;
		break;
	default:
		assert(false);
		return false;
		break;
	}

	// minFilter
	VkFilter min;
	switch (meta.min_filter) {
	case SamplerMeta::Filter::Linear:
		min = VK_FILTER_LINEAR;
		break;
	case SamplerMeta::Filter::Nearest:
		min = VK_FILTER_NEAREST;
		break;
	default:
		assert(false);
		return false;
		break;
	}
	builder.filter(min, mag);

	VkSamplerMipmapMode mipmap;
	switch (meta.mipmap_mode) {
	case SamplerMeta::Filter::Linear:
		mipmap = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		break;
	case SamplerMeta::Filter::Nearest:
		mipmap = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		break;
	default:
		assert(false);
		return false;
		break;
	}
	builder.mipmap(mipmap);

	auto map_wrap = [&](SamplerMeta::Wrap wrap) -> VkSamplerAddressMode {
		switch (wrap) {
		case SamplerMeta::Wrap::Repeat:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case SamplerMeta::Wrap::MirroredRepeat:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case SamplerMeta::Wrap::ClampToEdge:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		default:
			assert(false);
			return VK_SAMPLER_ADDRESS_MODE_MAX_ENUM;
		}
	};

	builder.address_mode_u(map_wrap(meta.wrap_u));
	builder.address_mode_v(map_wrap(meta.wrap_v));
	builder.address_mode_w(map_wrap(meta.wrap_w));

	return true;
}