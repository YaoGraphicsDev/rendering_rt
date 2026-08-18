#include "frame_graph_application.h"
#include "resource_managers/resource_context.h"
#include "subsystems/shadow_system.h"
#include "gltf_parser/gltf_parser.h"
#include "passes/passes.h"
#include "common/camera.h"
#include "common/free_roam.h"
#include "common/math_utils.h"

#include "glsl_reflect/light_clustering/assign_lights.comp.hpp"
#include "glsl_reflect/scene_culling/frustum_cull.comp.hpp"

using namespace otcv;

struct UIControls {
    bool ddgi_on = true;
    bool draw_probes = true;
    bool rebuild_framegraph = false;
};
UIControls ui_controls;

struct LightClusterParams {
    static constexpr glm::uvec3 n_clusters = glm::uvec3(32, 32, 32);
    static constexpr glm::uint max_lights_per_cluster = 32;
};

struct IrradianceFieldParams {
    static constexpr glm::vec3 probe_start = glm::vec3(-11.955299, -0.706239, -5.6519);
    static constexpr glm::vec3 probe_step = glm::vec3(1.586456, 1.715203, 0.754157);
    // The number give by DDGI was 64. At that rate, sample rays may miss some high frequency details around it..
    // Take sponza for example, probes hiding in shadows on the second floor looking down on the well lit atrium below. Lots of hight frequency dark & lit details on it
    static constexpr uint32_t rays_per_probe = 192; 
    static constexpr glm::ivec3 probe_counts = glm::ivec3(16, 8, 16);
    static constexpr uint32_t n_probes = probe_counts.x * probe_counts.y * probe_counts.z;
    static constexpr float depth_sharpness = 50.0f;
    static constexpr float hysteresis = 0.98f;
    static constexpr int probe_size_irrad = 16;
    static constexpr int probe_size_depth = 32;
    static constexpr glm::uvec2 atlas_size_irrad = glm::uvec2(probe_counts.x * probe_counts.y, probe_counts.z) * (glm::uvec2(probe_size_irrad) + glm::uvec2(2)) + glm::uvec2(2);
    static constexpr glm::uvec2 atlas_size_depth = glm::uvec2(probe_counts.x * probe_counts.y, probe_counts.z) * (glm::uvec2(probe_size_depth) + glm::uvec2(2)) + glm::uvec2(2);
    static constexpr VkFormat irrad_atlas_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat depth_atlas_format = VK_FORMAT_R16G16_SFLOAT;
};

int main() {
    const uint32_t startup_window_width = 960;
    const uint32_t startup_window_height = 480;
    std::shared_ptr<PerspectiveCamera> cam = std::make_shared<PerspectiveCamera>(
        glm::vec3(2.67119622f, 2.41205978f, -1.46302509f),
        glm::vec3(1.81380475f, 2.02222157f, -1.12700975f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        0.1f,
        40.0f,
        glm::radians(60.0f),
        (float)startup_window_width / (float)startup_window_height);
    std::shared_ptr<FreeRoam> free_roam = std::make_shared<FreeRoam>();
    
    struct FreeRoamContext {
        std::shared_ptr<PerspectiveCamera>  cam         = nullptr;
        std::shared_ptr<FreeRoam>           controller  = nullptr;
    };
    FreeRoamContext fr_ctx = { cam, free_roam };

    fg::Application::Config config;
    config.desired_window_width = startup_window_width;
    config.desired_window_height = startup_window_height;
    config.user_cb_data = &fr_ctx;
    config.key_cb = [](GLFWwindow* w, int key, int scancode, int action, int mods) {
        FreeRoamContext* fr_ctx = static_cast<FreeRoamContext*>(glfwGetWindowUserPointer(w));
        auto cam = fr_ctx->cam;
        auto controller = fr_ctx->controller;
        // press C to toggle free roam camera
        if (key == GLFW_KEY_C && action == GLFW_PRESS) {
            if (!controller->enabled) {
                // initialize free roam
                controller->enter_free_roam(cam->eye, cam->center);
                glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            }
            else {
                // exit free roam
                controller->exit_free_roam();
                glfwSetInputMode(w, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
        }

        if (controller->enabled && (action == GLFW_PRESS || action == GLFW_RELEASE)) {
            controller->on_key(key, action);
        }
    };
    config.cursor_pos_cb = [](GLFWwindow* w, double x, double y) {
        FreeRoamContext* fr_ctx = static_cast<FreeRoamContext*>(glfwGetWindowUserPointer(w));
        auto controller = fr_ctx->controller;

        if (controller->enabled) {
            controller->on_mouse_move(x, y);
        }
    };
    std::shared_ptr<fg::Application> fg_app = std::make_shared<fg::Application>(config); // implicitly initialize otcv context here

    std::shared_ptr<SceneManager> scene_mgr = std::make_shared<SceneManager>();
    std::shared_ptr<MeshManager> mesh_mgr = std::make_shared<MeshManager>();
    std::shared_ptr<MaterialManager> mat_mgr = std::make_shared<MaterialManager>();
    if (!load_gltf(
        "C:/Users/Yao/models/ddgi_test/gltf/probes_test.gltf",
        // "C:/Users/Yao/models/sponza_lit/sponza_lit.gltf",
        scene_mgr,
        mat_mgr,
        mesh_mgr)) {
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
    res_ctx->scene_acc = std::make_shared<SceneAcceleration>(scene_mgr, mesh_mgr, mat_mgr);

    std::shared_ptr<ShadowMapSystem> shadowmap_sys = std::make_shared<ShadowMapSystem>(res_ctx, cam);

    auto configure_framegraph = [&](fg::Application* app) {
        otcv::Context otcv_context = app->otcv_context();
        uint32_t window_width = otcv_context.swapchain->image_info.extent.width;
        uint32_t window_height = otcv_context.swapchain->image_info.extent.height;
        std::shared_ptr<fg::FrameGraph> fg = app->framegraph();

        // resources
        BufferBuilder indirect_cmd_builder =
            BufferBuilder()
            .size(FrustumCullComp::IndirectBuffer::ElementStride * scene_mgr->_renderable_metas.size())
            .host_access(otcv::BufferBuilder::Access::Invisible);

        BufferBuilder indirect_count_builder =
            BufferBuilder()
            .size(FrustumCullComp::DrawCountBuffer::ElementStride * res_ctx->render_queue->_order_ranges.size())
            .host_access(otcv::BufferBuilder::Access::Invisible);

        fg::ResourceHandle g_indirect_cmds = fg->add_resource("GIndirectCmds", indirect_cmd_builder);
        fg::ResourceHandle g_indirect_counts = fg->add_resource("GIndirectCount", indirect_count_builder);

        fg::ResourceHandle g_position = fg->add_resource("GPosition",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));

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
            .format(VK_FORMAT_R8G8_UNORM));

        fg::ResourceHandle g_emissive = fg->add_resource("GEmissive",
            ImageBuilder()
            .size(window_width, window_height, 1)
            // That's how Unity URP store it https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal@16.0/manual/rendering/deferred-rendering-path.html
            .format(VK_FORMAT_B10G11R11_UFLOAT_PACK32));

        fg::ResourceHandle g_depth = fg->add_resource("GDepth",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_D24_UNORM_S8_UINT)
            .aspect(VK_IMAGE_ASPECT_DEPTH_BIT));

        BufferBuilder visible_light_id_builder =
            BufferBuilder()
            .size(AssignLightsComp::VisibleLightIdBuffer::ElementStride * scene_mgr->_light_metas.size())
            .host_access(otcv::BufferBuilder::Access::Invisible);

        BufferBuilder visible_light_count_builder =
            BufferBuilder()
            .size(AssignLightsComp::VisibleLightCountBuffer::ElementStride)
            .host_access(otcv::BufferBuilder::Access::Invisible);

        BufferBuilder light_assign_builder =
            BufferBuilder()
            .size(AssignLightsComp::LightAssignmentBuffer::ElementStride * LightClusterParams::n_clusters.x * LightClusterParams::n_clusters.y * LightClusterParams::n_clusters.z)
            .host_access(otcv::BufferBuilder::Access::Invisible);

        fg::ResourceHandle visible_light_ids = fg->add_resource("LightId", visible_light_id_builder);
        fg::ResourceHandle visible_light_count = fg->add_resource("LightCount", visible_light_count_builder);
        fg::ResourceHandle light_assign = fg->add_resource("LightAssign", light_assign_builder);

        fg::ResourceHandle lit = fg->add_resource("Lit",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        
        // frustum culling
        {
            FrustumCulling::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/scene_culling/";
            cfg.pipelines_diff = true;
            std::shared_ptr<FrustumCulling> gfc = std::make_shared<FrustumCulling>(cfg);

            fg::Pass& g_frustum_cull_pass = fg->add_pass("GFrustumCull", fg::PassType::Compute);
            g_frustum_cull_pass.access(fg::ResourceAccessType::SSBOOut, g_indirect_cmds);
            g_frustum_cull_pass.ssbo_clear_value(g_indirect_cmds, 0);
            g_frustum_cull_pass.access(fg::ResourceAccessType::SSBOOut, g_indirect_counts);
            g_frustum_cull_pass.ssbo_clear_value(g_indirect_counts, 0);
            g_frustum_cull_pass.execute_func([gfc, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
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
                fg->get_img_builder(g_position)._image_info.format,
                fg->get_img_builder(g_normal)._image_info.format,
                fg->get_img_builder(g_albedo)._image_info.format,
                fg->get_img_builder(g_metallic_roughness)._image_info.format,
                fg->get_img_builder(g_emissive)._image_info.format };
            cfg.depth_attachment_format = fg->get_img_builder(g_depth)._image_info.format;
            std::shared_ptr<GeometryPass> gp = std::make_shared<GeometryPass>(cfg);

            fg::Pass& g_pass = fg->add_pass("Geometry", fg::PassType::Graphics);
            g_pass.access(fg::ResourceAccessType::IndirectIn, g_indirect_cmds);
            g_pass.access(fg::ResourceAccessType::IndirectIn, g_indirect_counts);
            g_pass.access(fg::ResourceAccessType::ColorOut, g_position);
            g_pass.store_load_func(g_position, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::ColorOut, g_normal);
            g_pass.store_load_func(g_normal, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::ColorOut, g_albedo);
            g_pass.store_load_func(g_albedo, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::ColorOut, g_metallic_roughness);
            g_pass.store_load_func(g_metallic_roughness, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::ColorOut, g_emissive);
            g_pass.store_load_func(g_emissive, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            g_pass.access(fg::ResourceAccessType::DepthStencilOut, g_depth);
            g_pass.store_load_func(g_depth, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(1.0f, 0);
            });
            g_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                begin.area(window_width, window_height);
            });
            g_pass.execute_func([app, gp, window_width, window_height, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                GeometryPass::CommandContext g_ctx;
                g_ctx.cmd_buf = cmd;
                g_ctx.proj = cam->proj;
                g_ctx.view = cam->view;
                g_ctx.fg_indirect_cmd = ctx.indirect_bufs.at(0);
                g_ctx.indirect_cmd_stride = FrustumCullComp::IndirectBuffer::ElementStride;
                g_ctx.fg_indirect_count = ctx.indirect_bufs.at(1);
                g_ctx.indirect_count_stride = FrustumCullComp::DrawCountBuffer::ElementStride;
                g_ctx.fg_frame_id = app->frame_slot();
                g_ctx.width = window_width;
                g_ctx.height = window_height;
                gp->commands(g_ctx);
            });
        }

        fg::ResourceHandle shadow_cascades;
        fg::ResourceHandle shadow_cube_faces;
        ShadowMapSystem::FGResources res = shadowmap_sys->commands(app);
        shadow_cascades = res.cascaded;
        shadow_cube_faces = res.cube;

        // light clustering pass
        {
            LightClustering::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/light_clustering/";
            cfg.n_clusters = LightClusterParams::n_clusters;
            cfg.width = window_width;
            cfg.height = window_height;
            cfg.z_near_abs = cam->near;
            cfg.z_far_abs = cam->far;
            cfg.inv_proj = cam->proj_inv;
            std::shared_ptr<LightClustering> lc = std::make_shared<LightClustering>(cfg);

            // light culling
            fg::Pass& light_culling_pass = fg->add_pass("LightCulling", fg::PassType::Compute);
            light_culling_pass.access(fg::ResourceAccessType::SSBOOut, visible_light_ids);
            light_culling_pass.ssbo_clear_value(visible_light_ids, 0);
            light_culling_pass.access(fg::ResourceAccessType::SSBOOut, visible_light_count);
            light_culling_pass.ssbo_clear_value(visible_light_count, 0);
            light_culling_pass.execute_func([app, lc, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                LightClustering::CullContext c_ctx;
                c_ctx.cmd_buf = cmd;
                c_ctx.proj = cam->proj;
                c_ctx.view = cam->view;
                c_ctx.fg_set = ctx.desc_set;
                c_ctx.fg_frame_id = app->frame_slot();
                lc->cull_commands(c_ctx);
            });

            // assign lights to clusters
            fg::Pass& light_assign_pass = fg->add_pass("LightAssign", fg::PassType::Compute);
            light_assign_pass.access(fg::ResourceAccessType::SSBOIn, visible_light_ids);
            light_assign_pass.access(fg::ResourceAccessType::SSBOIn, visible_light_count);
            light_assign_pass.access(fg::ResourceAccessType::SSBOOut, light_assign); // light assignment buffer 
            light_assign_pass.execute_func([app, lc, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                LightClustering::AssignContext a_ctx;
                a_ctx.cmd_buf = cmd;
                a_ctx.view = cam->view;
                a_ctx.fg_set = ctx.desc_set;
                a_ctx.fg_frame_id = app->frame_slot();
                lc->assign_commands(a_ctx);
            });
        }

        // lighting pass
        {
            LightingPass::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shadow_sys = shadowmap_sys;
            cfg.shader_dir = "./spirv/lighting_pass/";
            cfg.lct_luts_paths = { "./assets/lct/lut_0.dds", "./assets/lct/lut_1.dds" };
            cfg.color_attachment_format = fg->get_img_builder(lit)._image_info.format;
            std::shared_ptr<LightingPass> lp = std::make_shared<LightingPass>(cfg);

            fg::Pass& lighting_pass = fg->add_pass("Lighting", fg::PassType::Graphics);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_depth);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_albedo);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_normal);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_metallic_roughness);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, g_emissive);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, shadow_cascades);
            lighting_pass.access(fg::ResourceAccessType::TextureIn, shadow_cube_faces);
            {
                VkImageSubresourceRange sub_range{};
                sub_range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                sub_range.baseMipLevel = 0;
                sub_range.levelCount = 1;
                sub_range.baseArrayLayer = 0;
                sub_range.layerCount = fg->get_img_builder(shadow_cube_faces)._image_info.arrayLayers;
                lighting_pass.texture_view_as(shadow_cube_faces, sub_range, VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);
            }
            lighting_pass.access(fg::ResourceAccessType::SSBOIn, light_assign);
            lighting_pass.access(fg::ResourceAccessType::ColorOut, lit);
            lighting_pass.store_load_func(lit, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f);
            });
            lighting_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                begin.area(window_width, window_height);
            });
            lighting_pass.execute_func([app, window_width, window_height, lp, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                LightingPass::CommandContext s_ctx;
                s_ctx.cmd_buf = cmd;
                s_ctx.fg_set = ctx.desc_set;
                s_ctx.fg_frame_id = app->frame_slot();
                s_ctx.cam = cam;
                s_ctx.n_clusters = LightClusterParams::n_clusters; // light cluster dimensions
                s_ctx.width = window_width;
                s_ctx.height = window_height;
                lp->commands(s_ctx);
            });
        }

        /*
        fg::ResourceHandle rt_ray_origin = fg->add_resource("RTRayOrigin",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_direction = fg->add_resource("RTRayDirection",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_hit_location = fg->add_resource("RTRayHitLocation",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_hit_radiance = fg->add_resource("RTRayHitRadiance",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_hit_normal = fg->add_resource("RTRayHitNormal",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_hit_albedo = fg->add_resource("RTRayHitAlbedo",
            ImageBuilder()
            .size(window_width, window_height, 1)
            // storage images cant be srgb format. We have to go linear. Use floating point format to mitigate precision loss.
            // Dont use full-blown RGB16_SRGB format. Use short floating point to save on space
            // Downside being no alpha support. We dont need that for albedo buffer for now anyway
            .format(VK_FORMAT_B10G11R11_UFLOAT_PACK32));
        fg::ResourceHandle rt_ray_hit_metallic_roughness = fg->add_resource("RTRayHitMetallicRoughness",
            fg->get_img_builder(g_metallic_roughness)
            .size(window_width, window_height, 1));


        // full screen ray generation pass
        {
            RayGeneration::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/ray_trace/";
            cfg.gen_type = RayGeneration::PassConfig::GenType::FullScreen;
            std::shared_ptr<RayGeneration> rg = std::make_shared<RayGeneration>(cfg);
            fg::Pass& ray_gen_pass = fg->add_pass("RayGeneration", fg::PassType::Compute);
            ray_gen_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_origin);
            ray_gen_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_direction);
            ray_gen_pass.execute_func([window_width, window_height, rg, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                RayGeneration::CommandContext rg_ctx;
                rg_ctx.cmd_buf = cmd;
                rg_ctx.fg_set = ctx.desc_set;
                rg_ctx.cam = cam;
                rg_ctx.width = window_width;
                rg_ctx.height = window_height;
                rg->commands(rg_ctx);
            });
        }
        
        {
            RayQueryDirect::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/ray_trace/";
            std::shared_ptr<RayQueryDirect> rq = std::make_shared<RayQueryDirect>(cfg);
        
            fg::Pass& ray_query_pass = fg->add_pass("RayQuery", fg::PassType::Compute);
            ray_query_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_origin);
            ray_query_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_direction);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_location);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_radiance);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_normal);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_albedo);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_metallic_roughness);
            ray_query_pass.execute_func([app, rq, &cam, window_width, window_height](CommandBuffer* cmd, fg::PassContext& ctx) {
                RayQueryDirect::CommandContext rq_ctx;
                rq_ctx.cmd_buf = cmd;
                rq_ctx.fg_set = ctx.desc_set;
                rq_ctx.fg_frame_id = app->frame_slot();
                rq_ctx.width = window_width;
                rq_ctx.height = window_height;
                rq->commands(rq_ctx);
            });
        }
        */
        
        
        fg::ResourceHandle rt_ray_origin = fg->add_resource("RTRayOrigin",
            ImageBuilder()
            .size(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_direction = fg->add_resource("RTRayDirection",
            ImageBuilder()
            .size(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_hit_location = fg->add_resource("RTRayHitLocation",
            fg->get_img_builder(g_position)
            .size(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes, 1));
        fg::ResourceHandle rt_ray_hit_radiance_direct = fg->add_resource("RTRayHitRadiance",
            ImageBuilder()
            .size(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes, 1)
            .format(VK_FORMAT_R16G16B16A16_SFLOAT));
        fg::ResourceHandle rt_ray_hit_radiance_full = fg->version_resource(rt_ray_hit_radiance_direct);
        fg::ResourceHandle rt_ray_hit_normal = fg->add_resource("RTRayHitNormal",
            fg->get_img_builder(g_normal)
            .size(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes, 1));
        fg::ResourceHandle rt_ray_hit_albedo = fg->add_resource("RTRayHitAlbedo",
            ImageBuilder()
            .size(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes, 1)
            // storage images cant be srgb format. We have to go linear. Use floating point format to mitigate precision loss.
            // Dont use full-blown RGB16_SRGB format. Use short floating point to save on space
            // Downside being no alpha support. We dont need that for albedo buffer for now anyway
            .format(VK_FORMAT_B10G11R11_UFLOAT_PACK32)); 
        fg::ResourceHandle rt_ray_hit_metallic_roughness = fg->add_resource("RTRayHitMetallicRoughness",
            fg->get_img_builder(g_metallic_roughness)
            .size(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes, 1));

        // probe field ray generation pass
        {
            RayGeneration::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/ray_trace/";
            cfg.gen_type = RayGeneration::PassConfig::GenType::SphericalFibonacci;
            std::shared_ptr<RayGeneration> rg = std::make_shared<RayGeneration>(cfg);
            fg::Pass& ray_gen_pass = fg->add_pass("RayGeneration", fg::PassType::Compute);
            ray_gen_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_origin);
            ray_gen_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_direction);
            ray_gen_pass.execute_func([rg, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                RayGeneration::CommandContext rg_ctx;
                rg_ctx.cmd_buf = cmd;
                rg_ctx.fg_set = ctx.desc_set;
                rg_ctx.probe_start = IrradianceFieldParams::probe_start;
                rg_ctx.probe_step = IrradianceFieldParams::probe_step;
                rg_ctx.probe_orientation = random_rotation();
                rg_ctx.rays_per_probe = IrradianceFieldParams::rays_per_probe;
                rg_ctx.probe_counts = IrradianceFieldParams::probe_counts;
                rg->commands(rg_ctx);
            });
        }

        // direct ray query pass
        {
            RayQueryDirect::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/ray_trace/";
            std::shared_ptr<RayQueryDirect> rq = std::make_shared<RayQueryDirect>(cfg);
        
            fg::Pass& ray_query_pass = fg->add_pass("RayQuery", fg::PassType::Compute);
            ray_query_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_origin);
            ray_query_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_direction);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_location);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_radiance_direct);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_normal);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_albedo);
            ray_query_pass.access(fg::ResourceAccessType::StorageImageOut, rt_ray_hit_metallic_roughness);
            ray_query_pass.execute_func([app, rq, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                RayQueryDirect::CommandContext rq_ctx;
                rq_ctx.cmd_buf = cmd;
                rq_ctx.fg_set = ctx.desc_set;
                rq_ctx.fg_frame_id = app->frame_slot();
                rq_ctx.width = IrradianceFieldParams::rays_per_probe;
                rq_ctx.height = IrradianceFieldParams::n_probes;
                rq->commands(rq_ctx);
            });
        }

        // TODO: remember to destroy these. Otherwise this will cause memory leak upon framegraph rebuild
        Image* irrad_atlas_0 = ImageBuilder()
            .size(IrradianceFieldParams::atlas_size_irrad.x, IrradianceFieldParams::atlas_size_irrad.y, 1)
            .format(IrradianceFieldParams::irrad_atlas_format)
            .usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();
        fg::ResourceHandle irrad_atlas_0_hist = fg->import_resource("IrradAtlas0Hist", irrad_atlas_0);
        fg::ResourceHandle irrad_atlas_0_in = fg->import_resource("IrradAtlas0", irrad_atlas_0);
        fg::ResourceHandle irrad_atlas_0_out = fg->version_resource(irrad_atlas_0_in);

        Image* irrad_atlas_1 = ImageBuilder(irrad_atlas_0->builder).build();
        fg::ResourceHandle irrad_atlas_1_hist = fg->import_resource("IrradAtlas1Hist", irrad_atlas_1);
        fg::ResourceHandle irrad_atlas_1_in = fg->import_resource("IrradAtlas1", irrad_atlas_1);
        fg::ResourceHandle irrad_atlas_1_out = fg->version_resource(irrad_atlas_1_in);

        Image* depth_atlas_0 = ImageBuilder()
            .size(IrradianceFieldParams::atlas_size_depth.x, IrradianceFieldParams::atlas_size_depth.y, 1)
            .format(IrradianceFieldParams::depth_atlas_format)
            .usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
            .build();
        fg::ResourceHandle depth_atlas_0_hist = fg->import_resource("DepthAtlas0Hist", depth_atlas_0);
        fg::ResourceHandle depth_atlas_0_in = fg->import_resource("DepthAtlas0", depth_atlas_0);
        fg::ResourceHandle depth_atlas_0_out = fg->version_resource(depth_atlas_0_in);

        Image* depth_atlas_1 = ImageBuilder(depth_atlas_0->builder).build();
        fg::ResourceHandle depth_atlas_1_hist = fg->import_resource("DepthAtlas1Hist", depth_atlas_1);
        fg::ResourceHandle depth_atlas_1_in = fg->import_resource("IrradAtlas1", depth_atlas_1);
        fg::ResourceHandle depth_atlas_1_out = fg->version_resource(depth_atlas_1_in);

        IrradianceFields::PassConfig cfg;
        cfg.shader_dir = "./spirv/irradiance_field/";
        cfg.probe_counts = IrradianceFieldParams::probe_counts;
        cfg.probe_size_irrad = IrradianceFieldParams::probe_size_irrad;
        cfg.probe_size_depth = IrradianceFieldParams::probe_size_depth;
        cfg.probe_start = IrradianceFieldParams::probe_start;
        cfg.probe_step = IrradianceFieldParams::probe_step;
        cfg.rays_per_probe = IrradianceFieldParams::rays_per_probe;
        cfg.depth_sharpness = IrradianceFieldParams::depth_sharpness;
        cfg.hysteresis = IrradianceFieldParams::hysteresis;
        assert(fg->get_img_builder(lit)._image_info.format == fg->get_img_builder(rt_ray_hit_radiance_direct)._image_info.format);
        cfg.direct_lit_format = fg->get_img_builder(lit)._image_info.format;
        cfg.visualize_probes = true;
        cfg.probe_visualize_color_format = fg->get_img_builder(lit)._image_info.format;
        cfg.probe_visualize_depth_format = fg->get_img_builder(g_depth)._image_info.format;
        std::shared_ptr<IrradianceFields> rf = std::make_shared<IrradianceFields>(cfg);

        // indirect lighting for ray query result
        {
            fg::Pass& indirect_lighting_pass = fg->add_pass("ProbeIndirect", fg::PassType::Graphics);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_location);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_normal);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_albedo);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_metallic_roughness);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, irrad_atlas_0_hist);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, irrad_atlas_1_hist);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, depth_atlas_0_hist);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, depth_atlas_1_hist);
            indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_origin);
            indirect_lighting_pass.access(fg::ResourceAccessType::ColorInOut, rt_ray_hit_radiance_direct, rt_ray_hit_radiance_full);
            indirect_lighting_pass.store_load_func(lit, [](RenderingBegin::Attachment& attachment) {
                // attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f); // temporary. Just to show indirect lighting
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
            });
            indirect_lighting_pass.render_area_func([](RenderingBegin& begin) {
                begin.area(IrradianceFieldParams::rays_per_probe, IrradianceFieldParams::n_probes);
            });
            indirect_lighting_pass.execute_func([app, rf, &cam](CommandBuffer* cmd, fg::PassContext& ctx) {
                IrradianceFields::SampleFieldsContext sf_ctx;
                sf_ctx.cmd_buf = cmd;
                sf_ctx.fg_set = ctx.desc_set;
                sf_ctx.cam = cam;
                sf_ctx.normal_bias = 0.05f;
                sf_ctx.sample_atlas_index = (app->frame_count() + 1) % 2; // sample from atlas 1, from last frame
                sf_ctx.width = IrradianceFieldParams::rays_per_probe;
                sf_ctx.height = IrradianceFieldParams::n_probes;
                sf_ctx.view_type = IrradianceFields::ViewType::FromProbe;
                rf->sample_fields_commands(sf_ctx);
            });
        }

        //fg::ResourceHandle irrad_atlas_0 = fg->add_resource("IrradAtlas0",
        //    ImageBuilder()
        //    .size(IrradianceFieldParams::atlas_size_irrad.x, IrradianceFieldParams::atlas_size_irrad.y, 1)
        //    .format(IrradianceFieldParams::irrad_atlas_format));
        //fg::ResourceHandle irrad_atlas_1 = fg->add_resource("IrradAtlas1", fg->get_img_builder(irrad_atlas_0));
        //
        //fg::ResourceHandle depth_atlas_0 = fg->add_resource("DepthAtlas0",
        //    ImageBuilder()
        //    .size(IrradianceFieldParams::atlas_size_depth.x, IrradianceFieldParams::atlas_size_depth.y, 1)
        //    .format(IrradianceFieldParams::depth_atlas_format));
        //fg::ResourceHandle depth_atlas_1 = fg->add_resource("DepthAtlas1", fg->get_img_builder(depth_atlas_0));

        fg::ResourceHandle lit_with_probes = fg->version_resource(lit);
        fg::ResourceHandle lit_indirect = fg->add_resource("LitIndirect", fg->get_img_builder(lit)); //fg->add_resource("LitGI", fg->get_img_builder(lit));
        // direct + indirect
        fg::ResourceHandle lit_full = fg->version_resource(lit);
        
        //Image* depth_atlas_0 = ImageBuilder()
        //    .size(IrradianceFieldParams::atlas_size_depth.x, IrradianceFieldParams::atlas_size_depth.y, 1)
        //    .format(IrradianceFieldParams::depth_atlas_format)
        //    .usage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        //    .build();
        //fg::ResourceHandle depth_atlas_0_fg = fg->import_resource("DepthAtlas0", depth_atlas_0);

        //Image* depth_atlas_1 = ImageBuilder(depth_atlas_0->builder).build();
        //fg::ResourceHandle depth_atlas_1_fg = fg->import_resource("DepthAtlas1", depth_atlas_1);

        // DDGI passes
        {

            // irradiance update
            {
                fg::Pass& probe_update_pass = fg->add_pass("IrradProbeUpdate", fg::PassType::Compute);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_origin);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_direction);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_location);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_radiance_full);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_normal);
                probe_update_pass.access(fg::ResourceAccessType::StorageImageInOut, irrad_atlas_0_in, irrad_atlas_0_out);
                probe_update_pass.access(fg::ResourceAccessType::StorageImageInOut, irrad_atlas_1_in, irrad_atlas_1_out);
                probe_update_pass.execute_func([app, rf](CommandBuffer* cmd, fg::PassContext& ctx) {
                    IrradianceFields::UpdateProbesContext up_ctx;
                    up_ctx.cmd_buf = cmd;
                    up_ctx.fg_set = ctx.desc_set;
                    up_ctx.atlas_type = IrradianceFields::AtlasType::Irradiance;
                    up_ctx.src_atlas_index = app->frame_count() % 2; // write to atlas 1
                    rf->update_probes_commands(up_ctx);
                });

                fg::Pass& probe_visualize_pass = fg->add_pass("ProbeVisualize", fg::PassType::Graphics);
                probe_visualize_pass.access(fg::ResourceAccessType::TextureIn, irrad_atlas_0_out);
                probe_visualize_pass.access(fg::ResourceAccessType::TextureIn, irrad_atlas_1_out);
                probe_visualize_pass.access(fg::ResourceAccessType::ColorInOut, lit, lit_with_probes);
                probe_visualize_pass.access(fg::ResourceAccessType::DepthStencilIn, g_depth);
                probe_visualize_pass.store_load_func(lit, [](RenderingBegin::Attachment& attachment) {
                    attachment.load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
                });
                probe_visualize_pass.store_load_func(g_depth, [](RenderingBegin::Attachment& attachment) {
                    attachment.load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
                });
                probe_visualize_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                    begin.area(window_width, window_height);
                });
                probe_visualize_pass.execute_func([app, rf, &cam, window_width, window_height](CommandBuffer* cmd, fg::PassContext& ctx) {
                    IrradianceFields::VisualizeProbesContext vp_ctx;
                    vp_ctx.cmd_buf = cmd;
                    vp_ctx.proj_view = cam->proj * cam->view;
                    vp_ctx.fg_set = ctx.desc_set;
                    vp_ctx.visualize_type = IrradianceFields::AtlasType::Irradiance;
                    vp_ctx.probe_radius = 0.07f;
                    vp_ctx.sample_atlas_index = (app->frame_count() + 1) % 2; // sample from atlas 1
                    vp_ctx.width = window_width;
                    vp_ctx.height = window_height;
                    rf->visualize_probes_commands(vp_ctx);
                });
            }

            // depth update
            {
                fg::Pass& probe_update_pass = fg->add_pass("DepthProbeUpdate", fg::PassType::Compute);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_origin);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_direction);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_location);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_radiance_full);
                probe_update_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_normal);
                probe_update_pass.access(fg::ResourceAccessType::StorageImageInOut, depth_atlas_0_in, depth_atlas_0_out);
                probe_update_pass.access(fg::ResourceAccessType::StorageImageInOut, depth_atlas_1_in, depth_atlas_1_out);
                probe_update_pass.execute_func([app, rf](CommandBuffer* cmd, fg::PassContext& ctx) {
                    IrradianceFields::UpdateProbesContext up_ctx;
                    up_ctx.cmd_buf = cmd;
                    up_ctx.fg_set = ctx.desc_set;
                    up_ctx.atlas_type = IrradianceFields::AtlasType::Depth;
                    up_ctx.src_atlas_index = app->frame_count() % 2; // write to atlas 1
                    rf->update_probes_commands(up_ctx);
                });

                //fg::Pass& probe_visualize_pass = fg->add_pass("ProbeVisualize", fg::PassType::Graphics);
                //probe_visualize_pass.access(fg::ResourceAccessType::TextureIn, depth_atlas_0_out);
                //probe_visualize_pass.access(fg::ResourceAccessType::TextureIn, depth_atlas_1_out);
                //probe_visualize_pass.access(fg::ResourceAccessType::ColorInOut, lit, lit_with_probes);
                //probe_visualize_pass.access(fg::ResourceAccessType::DepthStencilIn, g_depth);
                //probe_visualize_pass.store_load_func(lit, [](RenderingBegin::Attachment& attachment) {
                //    attachment.load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
                //});
                //probe_visualize_pass.store_load_func(g_depth, [](RenderingBegin::Attachment& attachment) {
                //    attachment.load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
                //});
                //probe_visualize_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                //    begin.area(window_width, window_height);
                //});
                //probe_visualize_pass.execute_func([app, rf, &cam, window_width, window_height](CommandBuffer* cmd, fg::PassContext& ctx) {
                //    IrradianceFields::VisualizeProbesContext vp_ctx;
                //    vp_ctx.cmd_buf = cmd;
                //    vp_ctx.proj_view = cam->proj * cam->view;
                //    vp_ctx.fg_set = ctx.desc_set;
                //    vp_ctx.visualize_type = IrradianceFields::AtlasType::Depth;
                //    vp_ctx.probe_radius = 0.07f;
                //    vp_ctx.sample_atlas_index = (app->frame_count() + 1) % 2; // sample from atlas 1
                //    vp_ctx.width = window_width;
                //    vp_ctx.height = window_height;
                //    rf->visualize_probes_commands(vp_ctx);
                //});
            }

            // indirect lighting
            {
                fg::Pass& indirect_lighting_pass = fg->add_pass("LightingIndirect", fg::PassType::Graphics);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, g_position);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, g_normal);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, g_albedo);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, g_metallic_roughness);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, irrad_atlas_0_out);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, irrad_atlas_1_out);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, depth_atlas_0_out);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, depth_atlas_1_out);
                indirect_lighting_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_origin);
                indirect_lighting_pass.access(fg::ResourceAccessType::ColorInOut, lit, lit_full);

                indirect_lighting_pass.store_load_func(lit, [](RenderingBegin::Attachment& attachment) {
                    // attachment.load_store(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE).clear_value(0.0f, 0.0f, 0.0f, 1.0f); // temporary. Just to show indirect lighting
                    attachment.load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
                });
                indirect_lighting_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                    begin.area(window_width, window_height);
                });
                indirect_lighting_pass.execute_func([app, rf, &cam, window_width, window_height](CommandBuffer* cmd, fg::PassContext& ctx) {
                    IrradianceFields::SampleFieldsContext sf_ctx;
                    sf_ctx.cmd_buf = cmd;
                    sf_ctx.fg_set = ctx.desc_set;
                    sf_ctx.cam = cam;
                    sf_ctx.normal_bias = 0.05f;
                    sf_ctx.sample_atlas_index = (app->frame_count() + 1) % 2; // sample from atlas 1
                    sf_ctx.width = window_width;
                    sf_ctx.height = window_height;
                    sf_ctx.view_type = IrradianceFields::ViewType::FromCamera;
                    rf->sample_fields_commands(sf_ctx);
                });
            }
        }

        // tonemapping pass
        fg::ResourceHandle tonemapped = fg->add_resource("ToneMapped",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(otcv_context.swapchain->image_info.format));
        {
            ToneMapping::PassConfig cfg;
            cfg.res_context = res_ctx;
            cfg.shader_dir = "./spirv/post_process/";
            cfg.color_attachment_format = fg->get_img_builder(tonemapped)._image_info.format;
            std::shared_ptr<ToneMapping> tm = std::make_shared<ToneMapping>(cfg);

            fg::Pass& tone_mapping_pass = fg->add_pass("ToneMapping", fg::PassType::Graphics);
            // tone_mapping_pass.access(fg::ResourceAccessType::TextureIn, lit);
            // tone_mapping_pass.access(fg::ResourceAccessType::TextureIn, rt_ray_hit_radiance);
            // tone_mapping_pass.access(fg::ResourceAccessType::TextureIn, lit_with_probes);
            // tone_mapping_pass.access(fg::ResourceAccessType::TextureIn, lit_indirect);

            if (ui_controls.ddgi_on) {
                tone_mapping_pass.access(fg::ResourceAccessType::TextureIn, lit_full);
            }
            else {
                tone_mapping_pass.access(fg::ResourceAccessType::TextureIn, lit);
            }
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

        // imgui pass
        fg::ResourceHandle ui_overlayed = fg->add_resource("UIOverlayed",
            ImageBuilder()
            .size(window_width, window_height, 1)
            .format(otcv_context.swapchain->image_info.format));
        {
            std::shared_ptr<ImGuiDrawPass> igd = std::make_shared<ImGuiDrawPass>(fg_app->window());
            fg::Pass& ui_overlay_pass = fg->add_pass("UIOverlayPass", fg::PassType::Graphics);
            ui_overlay_pass.access(fg::ResourceAccessType::ColorInOut, tonemapped, ui_overlayed);
            ui_overlay_pass.store_load_func(tonemapped, [](RenderingBegin::Attachment& attachment) {
                attachment.load_store(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
            });
            ui_overlay_pass.render_area_func([window_width, window_height](RenderingBegin& begin) {
                begin.area(window_width, window_height);
            });
            ui_overlay_pass.execute_func([app, igd](CommandBuffer* cmd, fg::PassContext& ctx) {
                ImGuiDrawPass::CommandContext igd_ctx;
                igd_ctx.cmd_buf = cmd;
                igd_ctx.fg_frame_id = app->frame_slot();
                igd->commands(igd_ctx);
            });
        }

        if (!fg->set_as_backbuffer(ui_overlayed)) {
            assert(false);
        }
    };


    auto immediate_gui = [&]() {
        ImGuiIO& io = ImGui::GetIO();

        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        {
            ImGui::Begin("DDGI");
            ImGui::Text("%.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::Checkbox("DDGI", &ui_controls.ddgi_on);
            if (ImGui::Button("Rebuild")) {
                ui_controls.rebuild_framegraph = true;
            }
            else {
                ui_controls.rebuild_framegraph = false;
            }
            ImGui::End();
        }
        {
            ImGui::Begin("Nodes");
            for (auto& node : scene_mgr->_node_metas) {
                bool temp;
                ImGui::Checkbox(node.name.c_str(), &temp);
            }
            ImGui::End();
        }
        ImGui::Render();
    };


    //SceneNodeHandle area_light_snh = scene_mgr->get_node_handle("LightPlane");
    //SceneNodeHandle sun_snh = scene_mgr->get_node_handle("Sun");
    //SceneNodeMeta sun_node = scene_mgr->get_node(sun_snh);
    //LightMeta& sun_light = scene_mgr->get_light(sun_snh);
    auto frame_update = [&](fg::Application* app) {
        uint32_t width = app->otcv_context().swapchain->image_info.extent.width;
        uint32_t height = app->otcv_context().swapchain->image_info.extent.height;

        float dt = 1.0f / 60.0f; // TODO: assume 60fps for now. Should be passed as update parameter
        free_roam->update(dt, cam->eye, cam->center, cam->up);

        immediate_gui();

        // update camera
        cam->aspect = (float)width / (float)height;
        cam->update_view();
        // TODO: This is not right actually. If projection matrix is not updated upon window resize then the scene will look squeezed/stretched.
        // But updating projection matrix somehow messes up light clusters. Don't light clusters get rebuilt upon framegraph rebuild? Doesnt make sense
        // Fix this.
        // cam->update_proj();

        // update scene
        //if (area_light_snh.id != INVALID_MANAGER_HANDLE_ID) {
        //    scene_mgr->move_node_local(area_light_snh, glm::vec3(0.0f), glm::rotate(glm::mat4(1.0f), dt, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(1.0f));
        //}
        scene_mgr->update(app->frame_slot());
        
        shadowmap_sys->update(app->frame_slot());

        if (ui_controls.rebuild_framegraph) {
            app->register_framegraph_rebuild(configure_framegraph);
        }
    };

    fg_app->framegraph_initial_build(configure_framegraph);
	// fg_app->register_framegraph_rebuild(configure_framegraph);
     
	fg_app->synchronized_frame_update(frame_update);

	fg_app->run();

    res_ctx = nullptr;
    fg_app = nullptr;

    return 0;
}