#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

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
		this->view = update_view();
		this->proj = update_proj();
	}

	glm::mat4 update_view() {
		view = glm::lookAtRH(eye, center, up);
		return view;
	}

	glm::mat4 update_proj() {
		proj = glm::perspectiveRH_ZO(fov, aspect, near, far);
		proj[1][1] *= -1.0f;
		return proj;
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