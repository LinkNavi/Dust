#include "Core/Rendering/Camera.hpp"
#include "DustEngine.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Dust {

glm::vec3 Camera::forward() const {
    float yaw   = glm::radians(yawDeg);
    float pitch = glm::radians(pitchDeg);
    return glm::normalize(glm::vec3(
        cosf(yaw) * cosf(pitch),
        sinf(pitch),
        sinf(yaw) * cosf(pitch)
    ));
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Camera::up() const {
    return glm::normalize(glm::cross(right(), forward()));
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projection(float aspect) const {
    glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, nearClip, farClip);
    proj[1][1] *= -1.0f; // Vulkan clip space is Y-down
    return proj;
}

glm::mat4 Camera::viewProj(float aspect) const {
    return projection(aspect) * view();
}

void Camera::rotate(float deltaYawDeg, float deltaPitchDeg, float pitchLimitDeg) {
    yawDeg  += deltaYawDeg;
    pitchDeg = std::clamp(pitchDeg + deltaPitchDeg, -pitchLimitDeg, pitchLimitDeg);
}

void Camera::lookAt(glm::vec3 target) {
    glm::vec3 dir = target - position;
    if (glm::length(dir) < 0.0001f) return; // target == position, nothing to point at
    dir = glm::normalize(dir);
    pitchDeg = glm::degrees(asinf(dir.y));
    yawDeg   = glm::degrees(atan2f(dir.z, dir.x));
}

void updateFlyCamera(Camera& camera, DustEngine& engine, float dt,
                      float moveSpeed, float mouseSensitivity) {
    glm::vec2 delta = engine.mouseDelta(); // capture-aware — zero while UI has mouse focus
    camera.rotate(delta.x * mouseSensitivity, -delta.y * mouseSensitivity);

    glm::vec3 move{ 0.0f };
    if (engine.isKeyDown(GLFW_KEY_W)) move += camera.forward();
    if (engine.isKeyDown(GLFW_KEY_S)) move -= camera.forward();
    if (engine.isKeyDown(GLFW_KEY_D)) move += camera.right();
    if (engine.isKeyDown(GLFW_KEY_A)) move -= camera.right();
    if (engine.isKeyDown(GLFW_KEY_SPACE))      move += glm::vec3(0.0f, 1.0f, 0.0f);
    if (engine.isKeyDown(GLFW_KEY_LEFT_SHIFT)) move -= glm::vec3(0.0f, 1.0f, 0.0f);

    if (glm::length(move) > 0.0001f)
        camera.position += glm::normalize(move) * moveSpeed * dt;
}

} // namespace Dust
