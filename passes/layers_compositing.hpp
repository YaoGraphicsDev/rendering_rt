#pragma once

#include "otcv.h"
#include "otcv_utils.h"
#include "resource_managers/resource_context.h"

class LayersCompositing {
public:
	struct CommandContext {
		otcv::CommandBuffer*		cmd_buf = nullptr;
		std::vector<otcv::Image*>	fg_src_imgs;
		otcv::Image*				fg_dst_img = nullptr;
		VkImageAspectFlags			aspects = VK_IMAGE_ASPECT_NONE;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx) {
		for (uint32_t i = 0; i < ctx.fg_src_imgs.size(); ++i) {
			otcv::Image* src = ctx.fg_src_imgs[i];
			otcv::ImageCopy copy;
			copy
				.dst_layer(i)
				.extent(src->builder._image_info.extent)
				.src_aspect(ctx.aspects)
				.dst_aspect(ctx.aspects);
			ctx.cmd_buf->cmd_image_copy(src, ctx.fg_dst_img, copy);
		}
	}
};