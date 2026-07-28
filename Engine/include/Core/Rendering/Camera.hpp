#pragma once

// Vulkan needs depth in [0,1] (GLM defaults to GL's [-1,1]) and its clip
// space is Y-down (GLM assumes GL's Y-up) — both handled in Camera.cpp.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace Dust {

// Simple free-fly perspective camera.
struct Camera {
    glm::vec3 position = { 0.0f, 0.0f, 3.0f };
    float yawDeg   = -90.0f; // -90 faces -Z
    float pitchDeg = 0.0f;
    float fovDeg   = 70.0f;
    float nearClip = 0.05f;
    float farClip  = 1000.0f;

    glm::vec3 forward() const;
    glm::vec3 right()   const;
    glm::vec3 up()      const;

    glm::mat4 view()                   const;
    glm::mat4 projection(float aspect) const; // Vulkan-correct (Y-flipped, depth [0,1])
    glm::mat4 viewProj(float aspect)   const; // projection() * view()

    // Mouse-look helper — feed raw cursor delta in degrees, pitch clamped
    void rotate(float deltaYawDeg, float deltaPitchDeg, float pitchLimitDeg = 89.0f);

    // One-shot "point at this" — sets yaw/pitch from position toward target.
    // Raylib-style Camera.target ergonomics without giving up the yaw/pitch
    // representation the rest of Camera (rotate()) is built around.
    void lookAt(glm::vec3 target);
};

} // namespace Dust
