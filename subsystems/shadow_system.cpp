#include "shadow_system.h"
#include "passes/frustum_culling.h"
#include "passes/shadow_mapping.h"
#include "common/pcf_shadow_noise.h"

#include "glsl_reflect/scene_culling/frustum_cull.comp.hpp"

using namespace otcv;

ShadowMapSystem::ShadowMapSystem(std::shared_ptr<ResourceContext> res_ctx, std::shared_ptr<PerspectiveCamera> cam) {
    _res_ctx = res_ctx;
    _cam = cam;

    _sampler_shadowmap = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).build();
    _pcf_noise = ShadowNoiseTexture::disk_noise_texture(ShadowJitterParams::tile_size, ShadowJitterParams::n_strata_per_dim);
    _sampler_shadow_jitter = SamplerBuilder().filter(VK_FILTER_NEAREST, VK_FILTER_NEAREST).address_mode(VK_SAMPLER_ADDRESS_MODE_REPEAT).build();

    uint32_t n_frames = otcv::get_context().swapchain->images.size();
    _shadow_ubos.resize(n_frames);
    for (uint32_t i = 0; i < n_frames; ++i) {
        update(i);
    }
}

ShadowMapSystem::~ShadowMapSystem() {
    _sampler_shadow_jitter->destroy();
    _pcf_noise->destroy();
    _sampler_shadowmap->destroy();
}


void ShadowMapSystem::update(uint32_t frame_id) {
    SceneManager& scene = *_res_ctx->scene_mgr;
    auto [sun_node_handle, sun_node] = scene.find_node_if([&](const SceneNodeMeta& snm) { // TODO: could save some iterations if traverse light node
        return
            snm.light.id != INVALID_MANAGER_HANDLE_ID &&
            scene._light_metas.at(snm.light.id).type == LightMeta::Type::Directional &&
            scene._light_metas.at(snm.light.id).shadow_settings.casts_shadows;
    });

    if (sun_node_handle.id != INVALID_MANAGER_HANDLE_ID) {
        // found a shadow casting directional light
        LightMeta& sun_light = scene._light_metas.at(sun_node.light.id);
        _csm_settings = sun_light.shadow_settings;
        _csm_ctxs = CSMUtils::csm_ortho_projections(
            _cam->proj, _cam->view, _cam->near, _cam->far,
            glm::vec3(sun_node.world_transform * glm::vec4(sun_light.direction, 0.0f)),
            _csm_settings.n_cascades,
            _csm_settings.resolution,
            sun_light.shadow_settings.blend_overlap);
        
        LightingFrag::ShadowUBO ubo{};
        ubo.cascadedShadow.enabled = uint32_t(true);
        ubo.cascadedShadow.blendDepth = _csm_settings.blend_overlap;
        ubo.cascadedShadow.nCascades = _csm_settings.n_cascades;
        ubo.cascadedShadow.resolution = _csm_settings.resolution;
        for (uint32_t c = 0; c < _csm_ctxs.size(); ++c) {
            ubo.cascadedShadow.cascades.at(c).zBegin = _csm_ctxs[c].z_begin;
            ubo.cascadedShadow.cascades.at(c).zEnd = _csm_ctxs[c].z_end;
            ubo.cascadedShadow.cascades.at(c).lightSpaceView = mat4_to_array(_csm_ctxs[c].light_view);
            ubo.cascadedShadow.cascades.at(c).lightSpaceProject = mat4_to_array(_csm_ctxs[c].light_proj);
        }
        ubo.shadowJitter.tileSize = ShadowJitterParams::tile_size;
        ubo.shadowJitter.nStrataPerDim = ShadowJitterParams::n_strata_per_dim;
        ubo.shadowJitter.radius = ShadowJitterParams::radius;

        _shadow_ubos.at(frame_id).set({ 0, sizeof(LightingFrag::ShadowUBO) }, &ubo);
    }
    else {
        _csm_ctxs.clear();
        LightingFrag::ShadowUBO ubo{};
        ubo.cascadedShadow.enabled = uint32_t(false);

        _shadow_ubos.at(frame_id).set({ 0, sizeof(LightingFrag::ShadowUBO) }, &ubo);
    }
}

ShadowMapSystem::FGResources ShadowMapSystem::commands(otcv::fg::Application* fg_app) {
    // find directional light (sun) and see if it casts shadows
    SceneManager& scene = *_res_ctx->scene_mgr;
    fg::ResourceHandle shadow_cascades = fg::FG_INVALID_HANDLE;
    if (!_csm_ctxs.empty()) { // directional light casts shadow
        std::shared_ptr<fg::FrameGraph> fg = fg_app->framegraph();
        std::vector<fg::ResourceHandle> s_indirect_cmds;
        std::vector<fg::ResourceHandle> s_indirect_counts;
        for (uint32_t i = 0; i < _csm_settings.n_cascades; ++i) {
            s_indirect_cmds.push_back(fg->add_resource("ShadowIndirectCmds" + std::to_string(i),
                BufferBuilder()
                .size(FrustumCullComp::IndirectBuffer::ElementStride * scene._renderable_metas.size())
                .host_access(otcv::BufferBuilder::Access::Invisible)));
            s_indirect_counts.push_back(fg->add_resource("ShadowIndirectCount" + std::to_string(i),
                BufferBuilder()
                .size(FrustumCullComp::DrawCountBuffer::ElementStride * _res_ctx->render_queue->_order_ranges.size())
                .host_access(otcv::BufferBuilder::Access::Invisible)));

            // frustum culling for shadow pass
            FrustumCulling::PassConfig cfg;
            cfg.res_context = _res_ctx;
            cfg.shader_dir = "./spirv/scene_culling/";
            cfg.pipelines_diff = false;
            std::shared_ptr<FrustumCulling> sfc = std::make_shared<FrustumCulling>(cfg);

            fg::Pass& s_frustum_cull_pass = fg->add_pass("SFrustumCull", fg::PassType::Compute);
            s_frustum_cull_pass.access(fg::ResourceAccessType::SSBOOut, s_indirect_cmds[i]);
            s_frustum_cull_pass.ssbo_clear_value(s_indirect_cmds[i], 0);
            s_frustum_cull_pass.access(fg::ResourceAccessType::SSBOOut, s_indirect_counts[i]);
            s_frustum_cull_pass.ssbo_clear_value(s_indirect_counts[i], 0);
            s_frustum_cull_pass.execute_func([this, sfc, i](CommandBuffer* cmd, fg::PassContext& ctx) {
                FrustumCulling::CommandContext fc_ctx;
                fc_ctx.cmd_buf = cmd;
                fc_ctx.proj = this->_csm_ctxs[i].light_proj;
                fc_ctx.view = this->_csm_ctxs[i].light_view;
                fc_ctx.fg_set = ctx.desc_set;
                sfc->commands(fc_ctx);
            });
        }

        shadow_cascades = fg->add_resource("ShadowCascade",
            ImageBuilder()
            .size(_csm_settings.resolution, _csm_settings.resolution, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .layers(_csm_settings.n_cascades)
            .view_type(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT));

        // shadow pass
        ShadowMapping::PassConfig cfg;
        cfg.res_context = _res_ctx;
        cfg.shader_dir = "./spirv/shadows/";
        cfg.depth_attachment_format = fg->get_img_builder(shadow_cascades)._image_info.format;
        std::shared_ptr<ShadowMapping> sm = std::make_shared<ShadowMapping>(cfg);

        fg::Pass& shadow_pass = fg->add_pass("ShadowCascade", fg::PassType::Graphics);
        for (uint32_t i = 0; i < _csm_settings.n_cascades; ++i) {
            shadow_pass.access(fg::ResourceAccessType::IndirectIn, s_indirect_cmds[i]);
            shadow_pass.access(fg::ResourceAccessType::IndirectIn, s_indirect_counts[i]);
        }
        shadow_pass.access(fg::ResourceAccessType::DepthStencilOut, shadow_cascades);
        shadow_pass.store_load_func(shadow_cascades, [](RenderingBegin::Attachment& attachment) {
            attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(1.0f, 0.0f); // all layers get the same treatment
        });
        shadow_pass.render_area_func([this](RenderingBegin& begin) {
            begin.area(_csm_settings.resolution, _csm_settings.resolution);
        });
        shadow_pass.execute_func([this, sm, fg_app](CommandBuffer* cmd, fg::PassContext& pass_ctx) {
            for (uint32_t i = 0; i < _csm_settings.n_cascades; ++i) {
                ShadowMapping::CommandContext s_ctx;
                s_ctx.cmd_buf = cmd;
                s_ctx.light_proj = this->_csm_ctxs[i].light_proj;
                s_ctx.light_view = this->_csm_ctxs[i].light_view;
                s_ctx.layer_id = i;
                s_ctx.fg_indirect_cmd = pass_ctx.indirect_bufs.at(i * 2);
                s_ctx.indirect_cmd_stride = FrustumCullComp::IndirectBuffer::ElementStride;
                s_ctx.fg_indirect_count = pass_ctx.indirect_bufs.at(i * 2 + 1);
                s_ctx.fg_frame_id = fg_app->frame_slot();
                s_ctx.width = _csm_settings.resolution;
                s_ctx.height = _csm_settings.resolution;
                sm->commands(s_ctx);
            }
        });
    }// CSM draw end


    return FGResources{ shadow_cascades };
}
