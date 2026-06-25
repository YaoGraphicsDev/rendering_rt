#include "math_utils.h"

#include <glm/gtc/matrix_transform.hpp>

glm::vec3 FrustumUtils::ndc_to_world(glm::vec3 ndc, glm::mat4 proj_inv, glm::mat4 view_inv) {
	glm::vec4 view_space_coord = proj_inv * glm::vec4(ndc, 1.0f);
	view_space_coord = view_space_coord / view_space_coord.w;
	glm::vec4 world_space_coord = view_inv * view_space_coord;
	return world_space_coord;
}

// in world space
FrustumUtils::Frustum FrustumUtils::view_frustum_vertices(glm::mat4 proj_inv, glm::mat4 view_inv) {
	Frustum f;
	/*near top left*/		f[0] = ndc_to_world(glm::vec3(-1.0f, -1.0f, 0.0f), proj_inv, view_inv);
	/*near bottom left*/	f[1] = ndc_to_world(glm::vec3(-1.0f, 1.0f, 0.0f), proj_inv, view_inv);
	/*near bottom right*/	f[2] = ndc_to_world(glm::vec3(1.0f, 1.0f, 0.0f), proj_inv, view_inv);
	/*near top right*/		f[3] = ndc_to_world(glm::vec3(1.0f, -1.0f, 0.0f), proj_inv, view_inv);
	/*far top left*/		f[4] = ndc_to_world(glm::vec3(-1.0f, -1.0f, 1.0f), proj_inv, view_inv);
	/*far bottom left*/		f[5] = ndc_to_world(glm::vec3(-1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	/*far bottom right*/	f[6] = ndc_to_world(glm::vec3(1.0f, 1.0f, 1.0f), proj_inv, view_inv);
	/*far top right*/		f[7] = ndc_to_world(glm::vec3(1.0f, -1.0f, 1.0f), proj_inv, view_inv);

	return f;
}


void FrustumUtils::bounding_sphere(const Frustum& f, glm::vec3& center, float& radius) {
	auto length2 = [](glm::vec3 v) -> float {
		return glm::dot(v, v);
	};
	glm::vec3 center_far = (f[6] + f[4]) * 0.5f;
	float b2 = length2(center_far - f[4]);
	float r2 = length2(center_far - f[0]);
	if (r2 > b2) {
		// can't envelop near-plane vertices
		glm::vec3 center_near = (f[2] + f[0]) * 0.5f;
		float a2 = length2(center_near - f[0]);
		float H = glm::length(center_near - center_far);
		float c = (b2 - a2 - H*H) / (2.0f * H);
		center = glm::mix(center_near, center_far, (H + c) / H);
		radius = glm::sqrt(b2 + c * c);
	}
	else {
		center = center_far;
		radius = glm::sqrt(b2);
	}
}

glm::vec4 FrustumUtils::plane(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
	glm::vec3 cross = glm::cross(b - a, c - a);
	float len = glm::length(cross);
	if (len < 10E-5) {
		return glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	}
	glm::vec3 n = cross / len;
	float d = -glm::dot(n, a);
	return glm::vec4(n, d);
}

// https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus
// "Practical split scheme"
std::vector<std::pair<float, float>> CSMUtils::split(float near, float far, uint32_t n_partitions) {
	assert(n_partitions != 0);
	std::vector<float> splits;
	for (uint32_t i = 1; i < n_partitions; ++i) {
		float r = (float)i / n_partitions;
		float log_split = near * std::pow((far / near), r);
		float uniform_split = near + (far - near) * r;
		splits.push_back(0.5f * (log_split + uniform_split));
	}

	std::vector<std::pair<float, float>> partitions(splits.size() + 1);
	for (uint32_t i = 0; i < splits.size(); ++i) {
		partitions[i].second = splits[i];
		partitions[i + 1].first = splits[i];
	}
	partitions.front().first = near;
	partitions.back().second = far;

	assert(partitions.size() == n_partitions);
	return partitions;
}

// world space frustum
CSMUtils::SquareBound CSMUtils::bound_frustum(glm::mat3 light_space_inv, uint32_t resolution, const FrustumUtils::Frustum& frustum) {
	// radius and center of bounding sphere
	//float r = glm::length(frustum[0] - frustum[6]) * 0.5f;
	//glm::vec3 c = (frustum[0] + frustum[6]) * 0.5f;

	float r;
	glm::vec3 c;
	FrustumUtils::bounding_sphere(frustum, c, r);

	c = light_space_inv * c;
	glm::vec2 c_xy = glm::vec2(c);
	// expand to fit snapping
	float margin = r / (resolution - 1);
	float unit_texel = 2.0f * (r + margin) / resolution;

	// texel snapping
	glm::vec2 c_xy_snapped = glm::roundEven(c_xy / unit_texel) * unit_texel;

	SquareBound bound;
	bound.center = glm::vec3(c_xy_snapped.x, c_xy_snapped.y, c.z);
	bound.half_width = r + margin;
	return bound;
}

std::vector<CSMUtils::CascadeContext> CSMUtils::csm_ortho_projections(
	glm::mat4 proj,
	glm::mat4 view,
	float near,
	float far,
	glm::vec3 light_dir,
	uint32_t n_cascades,
	uint32_t resolution,
	float blend_overlap) {

	//determine light space
	glm::vec3 z = -glm::normalize(light_dir);
	glm::vec3 x = glm::cross(z, glm::vec3(0.0f, 1.0f, 0.0f));
	if (glm::length(x) < 10E-4) {
		// colinear
		x = glm::vec3(1.0f, 0.0f, 0.0f);
	}
	else {
		x = glm::normalize(x);
	}
	glm::vec3 y = glm::normalize(glm::cross(z, x));
	glm::mat3 light_space(x, y, z); // a left-handed coordinate system
	glm::mat3 light_space_inv = glm::transpose(light_space);

	FrustumUtils::Frustum f_whole = FrustumUtils::view_frustum_vertices(glm::inverse(proj), glm::inverse(view));

	std::vector<std::pair<float, float>> partitions = split(near, far, n_cascades);

	// add overlap to partitions
	if (n_cascades > 1) {
		for (uint32_t i = 1; i < partitions.size(); ++i) {
			float half_overlap = blend_overlap * 0.5f;
			partitions[i - 1].second += half_overlap;
			partitions[i].first -= half_overlap;
		}
	}

	// normalize partitions
	std::vector<std::pair<float, float>> norm_partitions = partitions;
	for (auto& p : norm_partitions) {
		p.first = (p.first - near) / (far - near);
		p.second = (p.second - near) / (far - near);
	}

	std::vector<CascadeContext> cascade_ctxs;
	for (uint32_t i = 0; i < partitions.size(); ++i) {
		// normalized start and end
		float start_norm = (partitions[i].first - near) / (far - near);
		float end_norm = (partitions[i].second - near) / (far - near);

		FrustumUtils::Frustum f_part;
		f_part[0] = glm::mix(f_whole[0], f_whole[4], start_norm);
		f_part[1] = glm::mix(f_whole[1], f_whole[5], start_norm);
		f_part[2] = glm::mix(f_whole[2], f_whole[6], start_norm);
		f_part[3] = glm::mix(f_whole[3], f_whole[7], start_norm);
		f_part[4] = glm::mix(f_whole[0], f_whole[4], end_norm);
		f_part[5] = glm::mix(f_whole[1], f_whole[5], end_norm);
		f_part[6] = glm::mix(f_whole[2], f_whole[6], end_norm);
		f_part[7] = glm::mix(f_whole[3], f_whole[7], end_norm);

		SquareBound square_bound = bound_frustum(light_space_inv, resolution, f_part);
		float hw = square_bound.half_width;

		// TODO: Ideally near/far plane should be determined by passing the AABB of the entire scene.
		glm::mat4 ortho = glm::orthoRH_ZO(-hw, hw, -hw, hw, -6.0f * hw, 6.0f * hw);
		ortho[1][1] *= -1.0f;
		glm::mat4 light_view(1.0f);
		light_view = light_space_inv;
		light_view[3] = glm::vec4(-square_bound.center, 1.0f);

		cascade_ctxs.push_back({ partitions[i].first, partitions[i].second, light_view, ortho });
	}
	return cascade_ctxs;
}

glm::vec4 encode_proj(const glm::mat4& proj) {
	float a = proj[0][0];
	float b = -proj[1][1];
	float c = proj[3][2];
	float d = c / proj[2][2];
	return { a, b, c, d };
}

glm::mat4 encoded_persp_proj_inv(glm::vec4 enc) {
	glm::mat4 inv(0.0f);
	inv[0][0] = 1.0 / enc.x;
	inv[1][1] = -1.0 / enc.y;
	inv[2][3] = 1.0 / enc.z;
	inv[3][2] = -1.0;
	inv[3][3] = 1.0 / enc.w;
	return inv;
}

glm::mat4 encoded_ortho_proj_inv(glm::vec4 enc) {
	glm::mat4 inv(0.0f);
	inv[0][0] = 1.0 / enc.x;
	inv[1][1] = -1.0 / enc.y;
	inv[2][2] = enc.w / enc.z;
	inv[3][2] = -enc.w;
	return inv;
}