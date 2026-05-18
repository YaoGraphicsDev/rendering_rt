#pragma once
#include <cstdint>
#include <limits>

struct SamplerHandle {
	int id = -1;
};
struct TextureHandle {
	int id = -1;
};
struct MaterialHandle {
	int id = -1;
};
struct ImageHandle {
	int id = -1;
};
struct BufferHandle {
	int id = -1;
};
struct MeshHandle {
	int id = -1;
};
struct VertexBufferHandle {
	int id = -1;
};
struct SceneNodeHandle {
	int id = -1;
};
struct RenderableHandle {
	int id = -1;
};
const int INVALID_MANAGER_HANDLE_ID = -1;
