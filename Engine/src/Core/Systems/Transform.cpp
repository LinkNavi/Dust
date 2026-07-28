#include "Core/Systems/Transform.hpp"
#include "Core/Systems/Entity.hpp"
#include <glm/gtc/matrix_transform.hpp>

namespace Dust {

glm::mat4 Transform::local() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m *= glm::mat4_cast(rotation);
    m  = glm::scale(m, scale);
    return m;
}

glm::mat4 worldTransform(Entity& e) {
    glm::mat4 local = e.has<Transform>() ? e.get<Transform>()->local() : glm::mat4(1.0f);
    if (e.parent)
        return worldTransform(*e.parent) * local;
    return local;
}

} // namespace Dust
