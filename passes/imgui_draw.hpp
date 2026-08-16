#pragma once

#include "otcv.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_otcv.h"

#include <GLFW/glfw3.h>

class ImGuiDrawPass {
public:
	ImGuiDrawPass(GLFWwindow* window) {
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		// io.DisplaySize = ImVec2(init_window_width, init_window_height);
		io.ConfigFlags |= ImGuiConfigFlags_NavNoCaptureKeyboard;
		ImGui::StyleColorsDark();

		ImGui_ImplGlfw_InitForVulkan(window, true);

		ImGui_ImplOTCV_InitInfo info;
		info.queue = otcv::get_context().queue;
		// can also add font here
		info.target_format = otcv::get_context().swapchain->image_info.format;
		ImGui_ImplOTCV_Init(&info);

		_meshes.resize(otcv::get_context().swapchain->images.size());
		for (auto& mesh : _meshes) {
			ImGui_ImplOTCV_Data* bd = ImGui_ImplOTCV_GetBackendData();
			otcv::VertexBufferBuilder vb_builder = bd->vertex_buffer->builder;
			mesh.vb = vb_builder.build();
			otcv::BufferBuilder ib_builder = bd->index_buffer->builder;
			mesh.ib = ib_builder.build();
		}
	}

	~ImGuiDrawPass() {
		ImGui_ImplOTCV_Shutdown();
		ImGui_ImplGlfw_Shutdown();
	}

	struct CommandContext {
		otcv::CommandBuffer*	cmd_buf = nullptr;
		uint32_t				fg_frame_id;
	};
	// call this in framegraph exec function
	void commands(CommandContext& ctx) {
		ImGui_ImplOTCV_BuildBuffers(_meshes[ctx.fg_frame_id].vb, _meshes[ctx.fg_frame_id].ib);
		ImGui_ImplOTCV_Exec(ctx.cmd_buf, _meshes[ctx.fg_frame_id].vb, _meshes[ctx.fg_frame_id].ib);
	}

private:
	struct Mesh {
		otcv::VertexBuffer* vb = nullptr;
		otcv::Buffer* ib = nullptr;
	};
	std::vector<Mesh> _meshes; // one per frame
};