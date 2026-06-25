#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <iostream>

#include "math_utils.h"

struct PerspectiveCamera {
	PerspectiveCamera() {};
	PerspectiveCamera(
		glm::vec3 eye,
		glm::vec3 center,
		glm::vec3 up,
		float near, 
		float far,
		float fov,
		float aspect) {
		this->eye = eye;
		this->center = center;
		glm::vec3 front = center - eye;
		glm::vec3 right = glm::cross(front, up);
		this->up = glm::normalize(glm::cross(right, front));
		this->near = near;
		this->far = far;
		this->fov = fov;
		this->aspect = aspect;
		update_view();
		update_proj();
	}

	void update_view() {
		view = glm::lookAtRH(eye, center, up);
		view_base_quat = glm::quat_cast(glm::transpose(glm::mat3(view)));
		// Could have just been a inverse() call, but do it this way to minimize numerical error, since view_base_quat is what's being passed to shaders
		view_inv = glm::mat4(1.0f);
		view_inv = glm::mat3_cast(view_base_quat);
		view_inv[3] = glm::vec4(eye, 1.0f);
	}

	void update_proj() {
		proj = glm::perspectiveRH_ZO(fov, aspect, near, far);
		proj[1][1] *= -1.0f;
		proj_enc = encode_proj(proj);
		proj_inv = encoded_persp_proj_inv(proj_enc);
	}

	glm::vec3 eye;
	glm::vec3 center;
	glm::vec3 up;
	float near;
	float far;
	float fov;
	float aspect;
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 view_inv;
	glm::mat4 proj_inv;

	glm::vec4 proj_enc;
	glm::quat view_base_quat;
	
	//friend std::ostream & operator<<(std::ostream & os, const glm::vec3 & v) {
	//	os << v.x << ", " << v.y << ", " << v.z;
	//	return os;
	//}

	//friend std::ostream& operator<<(std::ostream& os, const PerspectiveCamera& cam) {
	//	os << "eye = " << cam.eye << ", center = " << cam.center << ", up = " << cam.up;
	//	return os;
	//}
};


struct OrthoCamera {

};