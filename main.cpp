#include "frame_graph_application.h"
#include "resource_managers/resource_context.h"
#include "gltf_parser/gltf_parser.h"
#include "passes/passes.h"
#include "common/camera.h"
#include "common/math_utils.h"

using namespace otcv;

struct CSMParams {
    static constexpr uint32_t n_cascades = 3;
    static constexpr uint32_t resolution = 2048;
    static constexpr float blend_overlap = 1.0f;
};
std::vector<CSMUtils::CascadeContext> csm_ctxs;

glm::vec3 light_direction(2.0f, -7.0f, 1.0f); // TODO: fixed directional light for now, will have a light manager in the future

int main() {
    fg::Application::Config config;
    config.desired_window_width = 960;
    config.desired_window_height = 480;
    std::shared_ptr<fg::Application> fg_app = std::make_shared<fg::Application>(config); // implicitly initialize otcv context here

    std::shared_ptr<SceneManager> scene_mgr = std::make_shared<SceneManager>();
    std::shared_ptr<MeshManager> mesh_mgr = std::make_shared<MeshManager>();
    std::shared_ptr<MaterialManager> mat_mgr = std::make_shared<MaterialManager>();
    if (!load_gltf("C:/Users/Yao/models/Sponza/glTF/Sponza.gltf", scene_mgr, mat_mgr, mesh_mgr)) {
        std::cout << "Cannot load gltf file" << std::endl;
        assert(false);
        exit(1);
    }
    scene_mgr->bindless_build();
    mesh_mgr->bindless_build();
    mat_mgr->bindless_build();

    std::shared_ptr<ResourceContext> res_ctx = std::make_shared<ResourceContext>();
    res_ctx->scene_mgr = scene_mgr;
    res_ctx->mesh_mgr = mesh_mgr;
    res_ctx->material_mgr = mat_mgr;
    res_ctx->render_queue = std::make_shared<RenderQueue>(scene_mgr, mesh_mgr, mat_mgr, "./spirv/mesh_preprocess/");
    
    std::shared_ptr<PerspectiveCamera> cam = std::make_shared<PerspectiveCamera>(
        glm::vec3(15.0f, 15.0f, 15.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.1f,
        50.0f,
        glm::radians(60.0f),
        (float)config.desired_window_width / (float)config.desired_window_height);

    Std430AlignmentType IndirectDrawCmd;
    IndirectDrawCmd.add(Std430AlignmentType::InlineType::Uint, "indexCount");
    IndirectDrawCmd.add(Std430AlignmentType::InlineType::Uint, "instanceCount");
    IndirectDrawCmd.add(Std430AlignmentType::InlineType::Uint, "firstIndex");
    IndirectDrawCmd.add(Std430AlignmentType::InlineType::Int, "vertexOffset");
    IndirectDrawCmd.add(Std430AlignmentType::InlineType::Uint, "firstInstance");
    SSBOLayout IndirectDrawCmdLayout(IndirectDrawCmd, scene_mgr->_renderable_metas.size());

    Std430AlignmentType IndirectDrawCount;
    IndirectDrawCount.add(Std430AlignmentType::InlineType::Uint, "value");
    SSBOLayout IndirectDrawCountLayout(IndirectDrawCount, res_ctx->render_queue->_order_ranges.size());

    // declare lighting pass here as it gets updated every frame
    std::shared_ptr<LightingPass> lp = nullptr;

    auto configure_framegraph = [&](fg::Application* app) {
        otcv::Context otcv_context = app->otcv_context();
        uint32_t window_width = otcv_context.swapchain->image_info.extent.width;
        uint32_t window_height = otcv_context.swapchain->image_info.extent.height;
        std::shared_ptr<fg::FrameGraph> fg = app->framegraph();

        // resources
        BufferBuilder indirect_cmd_builder =
            BufferBuilder()
            .size(IndirectDrawCmdLayout._builder._info.size)
            .host_access(otcv::BufferBuilder::Access::Invisible);
        
        BufferBuilder indirect_count_builder =
            BufferBuilder()
            .size(IndirectDrawCountLayout._builder._info.size)
            .host_access(otcv::BufferBuilder::Access::Invisible);

        fg::ResourceHandle g_indirect_cmds_zero = fg->add_resource("GIndirectCmdsZeros", indirect_cmd_builder);
        fg::ResourceHandle g_indirect_counts_zero = fg->add_resource("GIndirectCountZeros", indirect_count_builder);
        fg::ResourceHandle g_indirect_cmds_filled = fg->add_resource("GIndirectCmdsFilled", indirect_cmd_builder);
        fg::ResourceHandle g_indirect_counts_filled = fg->add_resource("GIndirectCountFilled", indirect_count_builder);

        fg::ResourceHandle g_albedo = fg->add_resource("GAlbedo",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R8G8B8A8_SRGB));

        fg::ResourceHandle g_normal = fg->add_resource("GNormal",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));

        fg::ResourceHandle g_metallic_roughness = fg->add_resource("GMetallicRoughness",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R8G8B8A8_UNORM));

        fg::ResourceHandle g_depth = fg->add_resource("GDepth",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT));

        std::vector<fg::ResourceHandle> shadow_maps(CSMParams::n_cascades);
        for (uint32_t i = 0; i < CSMParams::n_cascades; ++i) {
            shadow_maps[i] = fg->add_resource("ShadowCascade",
                ImageBuilder()
                .size(CSMParams::resolution, CSMParams::resolution, 1)
                .format(VK_FORMAT_D24_UNORM_S8_UINT)
                .aspect(VK_IMAGE_ASPECT_DEPTH_BIT));
        }
        
        fg::ResourceHandle shadow_cascades = fg->add_resource("ShadowCascadeComposite",
            ImageBuilder()
            .size(CSMParams::resolution, CSMParams::resolution, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .layers(CSMParams::n_cascades)
            .view_type(VK_IMAGE_VIEW_TYPE_2D_ARRAY)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT));

        fg::ResourceHandle lit = fg->add_resource("Lit",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        
        fg::ResourceHandle tonemapped = fg->add_resource("ToneMapped",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(otcv_context.swapchain->image_info.format));
        
        // passes
        // clear indirect commands and counts for g-pass
        {
            fg::Pass& g_indirect_clear_pass = fg->add_pass("GIndirectClearPass", fg::PassType::Transfer);
            g_indirect_clear_pass.access(fg::ResourceAccessType::TransferOut, g_indirect_cmds_zero);
            g_indirect_clear_pass.access(fg::ResourceAccessType::TransferOut, g_indirect_counts_zero);
            g_indirect_clear_pass.execute_func([](CommandBuffer* cmd, fg::PassContext& ctx) {
                cmd->cmd_fill_buffer(ctx.transfer_bufs.at(0), 0);
                cmd->cmd_fill_buffer(ctx.transfer_bufs.at(1), 0);
            });
        }

        // frustum culling
        {
            FrustumCulling::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/scene_culling/";
            cfg.pipelines_diff = true;
            std::shared_ptr<FrustumCulling> gfc = std::make_shared<FrustumCulling>(cfg);

            fg::Pass& g_frustum_cull_pass = fg->add_pass("GFrustumCull", fg::PassType::Compute);
            g_frustum_cull_pass.access(fg::ResourceAccessType::SSBOInOut, g_indirect_cmds_zero, g_indirect_cmds_filled);
            g_frustum_cull_pass.access(fg::ResourceAccessType::SSBOInOut, g_indirect_counts_zero, g_indirect_counts_filled);
            g_frustum_cull_pass.execute_func([gfc, cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                FrustumCulling::CommandContext fc_ctx;
                fc_ctx.cmd_buf = cmd;
                fc_ctx.proj = cam->proj;
                fc_ctx.view = cam->view;
                fc_ctx.fg_set = ctx.desc_set;
                gfc->commands(fc_ctx);
            });
        }

        // g-pass
        {
            GeometryPass::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/geometry_pass/";
            cfg.color_attachment_formats = {
                fg->get_img_builder(g_albedo)._image_info.format,
                fg->get_img_builder(g_normal)._image_info.format,
                fg->get_img_builder(g_metallic_roughness)._image_info.format };
            cfg.depth_attachment_format = fg->get_img_builder(g_depth)._image_info.format;
            std::shared_ptr<GeometryPass> gp = std::make_shared<GeometryPass>(cfg);

            fg::Pass& g_pass = fg->add_pass("Geometry", fg::PassType::Graphics);
            g_pass.access(fg::ResourceAccessType::IndirectIn, g_indirect_cmds_filled);
            g_pass.access(fg::ResourceAccessType::IndirectIn, g_indirect_counts_filled);
            g_pass.access(fg::ResourceAccessType::ColorOut, g_albedo);
            g_pass.store_load_func(g_albedo, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::ColorOut, g_normal);
            g_pass.store_load_func(g_normal, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::ColorOut, g_metallic_roughness);
            g_pass.store_load_func(g_metallic_roughness, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::DepthStencilOut, g_depth);
            g_pass.store_load_func(g_depth, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(1.0f, 0);
            });
            g_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                begin.area(window_width, window_height);
            });
            g_pass.execute_func([gp, &IndirectDrawCmdLayout, &IndirectDrawCountLayout, window_width, window_height, cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                GeometryPass::CommandContext g_ctx;
                g_ctx.cmd_buf = cmd;
                g_ctx.proj = cam->proj;
                g_ctx.view = cam->view;
                g_ctx.fg_indirect_cmd = ctx.indirect_bufs.at(0);
                g_ctx.indirect_cmd_layout = IndirectDrawCmdLayout;
                g_ctx.fg_indirect_count = ctx.indirect_bufs.at(1);
                g_ctx.indirect_count_layout = IndirectDrawCountLayout;
                g_ctx.width = window_width;
                g_ctx.height = window_height;
                gp->commands(g_ctx);
            });
        }

        // cascaded shadow passes
        for (uint32_t i = 0; i < CSMParams::n_cascades; ++i) {
            fg::ResourceHandle s_indirect_cmds_zero = fg->add_resource("ShadowIndirectCmdsZeros", indirect_cmd_builder);
            fg::ResourceHandle s_indirect_cmds_filled = fg->add_resource("ShadowIndirectCmdsFilled", indirect_cmd_builder);
            fg::ResourceHandle s_indirect_counts_zero = fg->add_resource("ShadowIndirectCountZeros", indirect_count_builder);
            fg::ResourceHandle s_indirect_counts_filled = fg->add_resource("ShadowIndirectCountFilled", indirect_count_builder);

            // clear indirect commands and counts for shadow pass
            {
                fg::Pass& s_indirect_clear_pass = fg->add_pass("SIndirectClearPass", fg::PassType::Transfer);
                s_indirect_clear_pass.access(fg::ResourceAccessType::TransferOut, s_indirect_cmds_zero);
                s_indirect_clear_pass.access(fg::ResourceAccessType::TransferOut, s_indirect_counts_zero);
                s_indirect_clear_pass.execute_func([](CommandBuffer* cmd, fg::PassContext& ctx) {
                    cmd->cmd_fill_buffer(ctx.transfer_bufs.at(0), 0);
                    cmd->cmd_fill_buffer(ctx.transfer_bufs.at(1), 0);
                });
            }

            // frustum culling for shadow pass
            {
                FrustumCulling::PassConfig cfg;
                cfg.res_context = res_ctx;
                cfg.shader_dir = "./spirv/scene_culling/";
                cfg.pipelines_diff = false;
                std::shared_ptr<FrustumCulling> sfc = std::make_shared<FrustumCulling>(cfg);

                fg::Pass& s_frustum_cull_pass = fg->add_pass("SFrustumCull", fg::PassType::Compute);
                s_frustum_cull_pass.access(fg::ResourceAccessType::SSBOInOut, s_indirect_cmds_zero, s_indirect_cmds_filled);
                s_frustum_cull_pass.access(fg::ResourceAccessType::SSBOInOut, s_indirect_counts_zero, s_indirect_counts_filled);
                s_frustum_cull_pass.execute_func([sfc, cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                    FrustumCulling::CommandContext fc_ctx;
                    fc_ctx.cmd_buf = cmd;
                    fc_ctx.proj = cam->proj;
                    fc_ctx.view = cam->view;
                    fc_ctx.fg_set = ctx.desc_set;
                    sfc->commands(fc_ctx);
                });
            }

            // shadow pass
            {
                ShadowMapping::PassConfig cfg;
                cfg.res_context = res_ctx;
                cfg.shader_dir = "./spirv/shadows/";
                cfg.depth_attachment_format = fg->get_img_builder(shadow_maps[i])._image_info.format;
                std::shared_ptr<ShadowMapping> sm = std::make_shared<ShadowMapping>(cfg);

                fg::Pass& shadow_pass = fg->add_pass("ShadowOneCascade", fg::PassType::Graphics);
                shadow_pass.access(fg::ResourceAccessType::IndirectIn, s_indirect_cmds_filled);
                shadow_pass.access(fg::ResourceAccessType::IndirectIn, s_indirect_counts_filled);
                shadow_pass.access(fg::ResourceAccessType::DepthStencilOut, shadow_maps[i]);
                shadow_pass.store_load_func(shadow_maps[i], [](RenderingBegin::Attachment& attachment) {
                    attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(1.0f, 0.0f);
                });
                shadow_pass.render_area_func([](RenderingBegin& begin) {
                    begin.area(CSMParams::resolution, CSMParams::resolution);
                });
                shadow_pass.execute_func([&IndirectDrawCmdLayout, &IndirectDrawCountLayout, sm, cam, i](CommandBuffer* cmd, fg::PassContext& ctx) {
                    ShadowMapping::CommandContext s_ctx;
                    s_ctx.cmd_buf = cmd;
                    s_ctx.light_proj = csm_ctxs[i].light_proj;
                    s_ctx.light_view = csm_ctxs[i].light_view;
                    s_ctx.fg_indirect_cmd = ctx.indirect_bufs.at(0);
                    s_ctx.indirect_cmd_layout = IndirectDrawCmdLayout;
                    s_ctx.fg_indirect_count = ctx.indirect_bufs.at(1);
                    s_ctx.indirect_count_layout = IndirectDrawCountLayout;
                    s_ctx.width = CSMParams::resolution;
                    s_ctx.height = CSMParams::resolution;
                    sm->commands(s_ctx);
                });
            }
        }

        // Copy individual shadowmaps to cascaded shadowmap
        {
            fg::Pass& shadow_composite_pass = fg->add_pass("CascadeShadowComposite", fg::PassType::Transfer);
            for (uint32_t i = 0; i < CSMParams::n_cascades; ++i) {
                shadow_composite_pass.access(fg::ResourceAccessType::TransferIn, shadow_maps[i]);
            }
            shadow_composite_pass.access(fg::ResourceAccessType::TransferOut, shadow_cascades);
            std::shared_ptr<LayersCompositing> lc = std::make_shared<LayersCompositing>();
            shadow_composite_pass.execute_func([lc](CommandBuffer* cmd, fg::PassContext& ctx) {
                LayersCompositing::CommandContext lc_ctx;
                lc_ctx.cmd_buf = cmd;
                lc_ctx.fg_src_imgs = std::vector<otcv::Image*>(ctx.transfer_imgs.begin(), ctx.transfer_imgs.end() - 1);
                lc_ctx.fg_dst_img = ctx.transfer_imgs.back();
                lc_ctx.aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
                lc->commands(lc_ctx);
            });
        }

        // lighting pass
        {
            LightingPass::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/lighting_pass/";
            cfg.color_attachment_format = fg->get_img_builder(lit)._image_info.format;
            cfg.cascaded_shadow.n_cascades = CSMParams::n_cascades;
            cfg.cascaded_shadow.blend_depth = CSMParams::blend_overlap;
            cfg.cascaded_shadow.resolution = CSMParams::resolution;
            lp.reset(new LightingPass(cfg));

            fg::Pass& lighting_pass = fg->add_pass("Lighting", fg::PassType::Graphics);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_depth);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_albedo);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_normal);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_metallic_roughness);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, shadow_cascades);
            lighting_pass.access(fg::ResourceAccessType::ColorOut, lit);
            lighting_pass.store_load_func(lit, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            lighting_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                begin.area(window_width, window_height);
            });
            lighting_pass.execute_func([app, window_width, window_height, lp](CommandBuffer* cmd, fg::PassContext& ctx) {
                LightingPass::CommandContext s_ctx;
                s_ctx.cmd_buf = cmd;
                s_ctx.fg_set = ctx.desc_set;
                s_ctx.fg_frame_id = app->current_frame();
                s_ctx.width = window_width;
                s_ctx.height = window_height;
                lp->commands(s_ctx);
            });
        }

        // tonemapping pass
        {
            ToneMapping::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/post_process/";
            cfg.color_attachment_format = fg->get_img_builder(tonemapped)._image_info.format;
            std::shared_ptr<ToneMapping> tm = std::make_shared<ToneMapping>(cfg);

            fg::Pass& tone_mapping_pass = fg->add_pass("ToneMapping", fg::PassType::Graphics);
            tone_mapping_pass.access(fg::ResourceAccessType::TextureIn, lit);
            tone_mapping_pass.access(fg::ResourceAccessType::ColorOut, tonemapped);
            tone_mapping_pass.store_load_func(tonemapped, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            tone_mapping_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                begin.area(window_width, window_height);
            });
            tone_mapping_pass.execute_func([window_width, window_height, tm](CommandBuffer* cmd, fg::PassContext& ctx) {
                ToneMapping::CommandContext tm_ctx;
                tm_ctx.cmd_buf = cmd;
                tm_ctx.fg_set = ctx.desc_set;
                tm_ctx.width = window_width;
                tm_ctx.height = window_height;
                tm->command(tm_ctx);
            });
        }

        if (!fg->set_as_backbuffer(tonemapped)) {
            assert(false);
        }
    };

    auto frame_update = [&](fg::Application* app) {
        uint32_t width = app->otcv_context().swapchain->image_info.extent.width;
        uint32_t height = app->otcv_context().swapchain->image_info.extent.height;

        // update camera
        cam->aspect = (float)width / (float)height;
        cam->update_view();
        cam->update_proj();

        // TODO: update light here

        // update CSM parameters
        csm_ctxs = CSMUtils::csm_ortho_projections(
            cam->proj, cam->view, cam->near, cam->far,
            light_direction,
            CSMParams::n_cascades, CSMParams::resolution, CSMParams::blend_overlap);

        // update lighiting pass
        LightingPass::UpdateContext lp_ctx;
        lp_ctx.inv_proj = glm::inverse(cam->proj);
        lp_ctx.inv_view = glm::inverse(cam->view);
        lp_ctx.shadow.cascades = csm_ctxs;
        lp_ctx.width = width;
        lp_ctx.height = height;
        lp->update(app->current_frame(), lp_ctx);
    };

    fg_app->framegraph_initial_build(configure_framegraph);
	// fg_app->register_framegraph_rebuild(configure_framegraph);
     
	fg_app->synchronized_frame_update(frame_update);

	fg_app->run();

    res_ctx = nullptr;
    fg_app = nullptr;

    return 0;
}