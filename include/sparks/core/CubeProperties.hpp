#pragma once

#include <glm/vec3.hpp>

namespace sparks::core {

struct CubeProperties {
    glm::vec3 baseColor{0.17f, 0.65f, 0.95f};
    glm::vec3 backgroundColor{0.08f, 0.09f, 0.11f};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEulerDegrees{20.0f, 35.0f, 0.0f};
    float scale{1.0f};
    bool wireframe{false};
};

}  // namespace sparks::core
