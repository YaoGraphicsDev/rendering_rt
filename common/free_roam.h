#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

struct FreeRoam
{
    // current camera pose (authoritative output)
    glm::vec3 _eye = { 0,0,0 };
    glm::vec3 _center = { 0,0,-1 };

    // tunables
    float move_speed = 3.0f;   // units/sec
    float sensitivity = 0.08f;  // degrees per pixel
    bool invert_y = true;

    // orientation state
    float yaw_deg = 0.0f;
    float pitch_deg = 0.0f;

    // key-held state updated by callbacks
    bool w = false, a = false, s = false, d = false;
    bool space = false, ctrl = false;

    // mouse state updated by callback
    bool mouse_initialized = false;
    double last_x = 0.0, last_y = 0.0;
    float accum_dx = 0.0f, accum_dy = 0.0f;

    bool enabled = false;

    // Call when switching into free-roam from a preset camera
    void enter_free_roam(const glm::vec3& inEye, const glm::vec3& inCenter) {// default up (0, 1, 0)
        _eye = inEye;
        _center = inCenter;

        // derive yaw/pitch from current forward
        glm::vec3 f = glm::normalize(_center - _eye);

        yaw_deg = glm::degrees(atan2f(f.z, f.x));
        pitch_deg = glm::degrees(asinf(glm::clamp(f.y, -1.0f, 1.0f)));

        // reset input accumulators to avoid jump
        mouse_initialized = false;
        accum_dx = accum_dy = 0.0f;

        // clear movement keys (optional but often desirable)
        w = a = s = d = space = ctrl = false;

        enabled = true;
    }

    void exit_free_roam()
    {
        enabled = false;
        mouse_initialized = false;
        accum_dx = accum_dy = 0.0f;
        w = a = s = d = space = ctrl = false;
    }

    // call from key callback
    void on_key(int key, int action)
    {
        if (!enabled) return;
        const bool down = (action == GLFW_PRESS);

        switch (key)
        {
        case GLFW_KEY_W: w = down; break;
        case GLFW_KEY_A: a = down; break;
        case GLFW_KEY_S: s = down; break;
        case GLFW_KEY_D: d = down; break;
        case GLFW_KEY_SPACE: space = down; break;
        case GLFW_KEY_LEFT_CONTROL: ctrl = down; break;
        default: break;
        }
    }

    // call from cursor pos callback (absolute x,y)
    void on_mouse_move(double x, double y)
    {
        if (!enabled) return;

        if (!mouse_initialized)
        {
            mouse_initialized = true;
            last_x = x;
            last_y = y;
            return;
        }

        accum_dx += float(x - last_x);
        accum_dy += float(y - last_y);
        last_x  = x;
        last_y = y;
    }

    // call once per frame
    void update(float dt, glm::vec3& eye, glm::vec3& center, glm::vec3& up) {
        if (!enabled) return;

        // consume mouse delta
        float dx = accum_dx;
        float dy = accum_dy;
        accum_dx = accum_dy = 0.0f;

        // yaw/pitch update (degrees)
        yaw_deg += dx * sensitivity;
        float ySign = invert_y ? 1.0f : -1.0f;
        pitch_deg -= dy * sensitivity * ySign;
        pitch_deg = glm::clamp(pitch_deg, -89.0f, 89.0f);

        // basis from yaw/pitch
        float yaw = glm::radians(yaw_deg);
        float pitch = glm::radians(pitch_deg);

        glm::vec3 forward(
            cosf(pitch) * cosf(yaw),
            sinf(pitch),
            cosf(pitch) * sinf(yaw)
        );
        forward = glm::normalize(forward);

        glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
        glm::vec3 cam_up = glm::cross(right, forward);

        // movement from held keys
        glm::vec3 move(0.0f);
        if (w)
            move += forward;
        if (s)
            move -= forward;
        if (d)
            move += right;
        if (a)
            move -= right;
        if (space)
            move += cam_up;
        if (ctrl)
            move -= cam_up;

        if (glm::dot(move, move) > 0.0f)
            move = glm::normalize(move);

        eye += move * (move_speed * dt);

        // output pose
        center = eye + forward;
        up = cam_up;
    }
};
