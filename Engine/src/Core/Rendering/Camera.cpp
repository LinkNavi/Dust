#include "Core/Rendering/Camera.hpp"
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

} // namespace Dust
