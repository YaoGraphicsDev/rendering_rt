#include "external_texture_loader.h"
#define TINYDDSLOADER_IMPLEMENTATION
#include "tinyddsloader.h"

otcv::Image* load_dds_rgba16f(const std::string& filename) {
    tinyddsloader::DDSFile dds;

    auto result = dds.Load(filename.data());
    if (result != tinyddsloader::Result::Success) {
        std::cout << "Load_dds_rgba16f error: cannot load " << filename << std::endl;
        assert(false);
        return nullptr;
    }

    if (dds.GetMipCount() > 1) {
        std::cout << "Load_dds_rgba16f error: " << filename << " has more than 1 mip level" << std::endl;
        assert(false);
        return nullptr;
    }

    const auto& img_data = dds.GetImageData(0, 0);

    if (dds.GetFormat() != tinyddsloader::DDSFile::DXGIFormat::R16G16B16A16_Float) {
        throw std::runtime_error("DDS is not RGBA16F");
    }

    otcv::Image* img;
    img = otcv::ImageBuilder()
        .size(dds.GetWidth(), dds.GetHeight(), 1)
        .format(VK_FORMAT_R16G16B16A16_SFLOAT)
        .usage(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        .build();
    img->populate(img_data->m_mem, img_data->m_memSlicePitch, otcv::ResourceState::FragSample);

    return img;
}

otcv::Image* load_dummy_image() {
    otcv::Image* img;
    img = otcv::ImageBuilder()
        .size(1, 1, 1)
        .format(VK_FORMAT_R8G8B8A8_UNORM)
        .usage(VK_IMAGE_USAGE_SAMPLED_BIT)
        .build();
    uint32_t zero = 0;
    img->populate(&zero, sizeof(zero), otcv::ResourceState::FragSample);

    return img;
}