#pragma once

#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Dust {

struct Entity; // Core/Systems/Entity.hpp

// Local-space TRS. Attach with entity->add<Transform>({...}); world space is
// resolved by walking the entity hierarchy — see worldTransform() below.
struct Transform {
    glm::vec3 position = { 0.0f, 0.0f, 0.0f };
    glm::quat rotation  = { 1.0f, 0.0f, 0.0f, 0.0f }; // identity (w,x,y,z)
    glm::vec3 scale     = { 1.0f, 1.0f, 1.0f };

    glm::mat4 local() const;
};

// Composes this entity's Transform with every ancestor's, root-down.
// Entities (or ancestors) with no Transform component contribute identity —
// hierarchy still works without every level needing one.
glm::mat4 worldTransform(Entity& e);

} // namespace Dust
