#include "ray_generation.h"
#include "common/render_global_types.h"
#include "common/math_utils.h"
#include "glsl_reflect/ray_trace/ray_gen_fullscreen.comp.hpp"
#include "glsl_reflect/ray_trace/ray_gen_probe_field.comp.hpp"

using namespace otcv;

RayGeneration::RayGeneration(const PassConfig& cfg) {
	_res = cfg.res_context;
	if (cfg.gen_type == PassConfig::GenType::FullScreen) {
		_shader = load_shader(cfg.shader_dir + "ray_gen_fullscreen.comp.spv");
		_pipeline = ComputePipeline::create(_shader);
	}
	else if (cfg.gen_type == PassConfig::GenType::SphericalFibonacci) {
		_shader = load_shader(cfg.shader_dir + "ray_gen_probe_field.comp.spv");
		_pipeline = ComputePipeline::create(_shader);
	}
	else {
		assert(false);
	}
	_type = cfg.gen_type;
}

RayGeneration::~RayGeneration() {
	_pipeline->destroy();
	_shader->destroy();
}

void RayGeneration::commands(CommandContext& ctx) {
	uint32_t width = 0;
	uint32_t height = 0;

	if (_type == PassConfig::GenType::FullScreen) {
		width = ctx.width;
		height = ctx.height;
		FrustumUtils::Frustum f = FrustumUtils::view_frustum_vertices(ctx.cam->proj_inv, ctx.cam->view_inv);
		RayGenFullscreenComp::PushConstants pc;
		pc.camPos = vec3_to_array(ctx.cam->eye);
		pc.topLeft = vec3_to_array(f[4]);
		pc.pixelDeltaX = vec3_to_array((f[7] - f[4]) / float(width));
		pc.pixelDeltaY = vec3_to_array((f[5] - f[4]) / float(height));
		pc.resolution = { int(width), int(height) };
		ctx.cmd_buf->cmd_push_constant(_pipeline, pc);
	}
	else if (_type == PassConfig::GenType::SphericalFibonacci) {
		width = ctx.rays_per_probe;
		auto is_power_of_two = [](int n) -> bool {
			// Guards against 0 and negative integers, then applies the bit-trick
			return n > 0 && (n & (n - 1)) == 0;
		};
		assert(is_power_of_two(ctx.probe_counts.x));
		assert(is_power_of_two(ctx.probe_counts.y));
		assert(is_power_of_two(ctx.probe_counts.z));
		height = ctx.probe_counts.x * ctx.probe_counts.y * ctx.probe_counts.z;
		RayGenProbeFieldComp::PushConstants pc;
		pc.probeStart = vec3_to_array(ctx.probe_start);
		pc.probeStep = vec3_to_array(ctx.probe_step);
		pc.probeOrientation = mat3_to_array(ctx.probe_orientation);
		pc.raysPerProbe = ctx.rays_per_probe;
		pc.probeCounts = vec3_to_array(ctx.probe_counts);
		ctx.cmd_buf->cmd_push_constant(_pipeline, pc);
	}


	ctx.cmd_buf->cmd_bind_compute_pipeline(_pipeline);
	ctx.cmd_buf->cmd_bind_descriptor_set(_pipeline, ctx.fg_set, DescriptorSetRate::FrameGraph);
	ctx.cmd_buf->cmd_dispatch(calc_group_count(width, _compute_group_size.x), calc_group_count(height, _compute_group_size.y), 1);
}