#pragma once
#include "glm/glm.hpp"
#include <array>
#include <vector>

struct FrustumUtils {
	typedef std::array<glm::vec3, 8> Frustum;

	static glm::vec3 ndc_to_world(glm::vec3 ndc, glm::mat4 proj_inv, glm::mat4 view_inv);

	// in world space
	static Frustum view_frustum_vertices(glm::mat4 proj_inv, glm::mat4 view_inv);

	static void bounding_sphere(const Frustum& f, glm::vec3& center, float& radius);

	static glm::vec4 plane(glm::vec3 a, glm::vec3 b, glm::vec3 c);
};

class CSMUtils {
public:
	struct CascadeContext {
		float z_begin;
		float z_end;
		glm::mat4 light_view;
		glm::mat4 light_proj;
	};
	static std::vector<CascadeContext> csm_ortho_projections(
		glm::mat4 proj,
		glm::mat4 view,
		float near,
		float far,
		glm::vec3 light_dir,
		uint32_t n_cascades,
		uint32_t resolution,
		float blend_overlap);


private:
	static std::vector<std::pair<float, float>> split(float near, float far, uint32_t n_partitions);

	struct SquareBound {
		glm::vec3 center;
		float half_width;
	};

	static SquareBound bound_frustum(glm::mat3 light_space_inv, uint32_t resolution, const FrustumUtils::Frustum& frustum);
};

glm::vec4 encode_proj(const glm::mat4& proj);

glm::mat4 encoded_persp_proj_inv(glm::vec4 enc);

glm::mat4 encoded_ortho_proj_inv(glm::vec4 enc);