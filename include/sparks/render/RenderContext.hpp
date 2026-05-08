#pragma once

#include <glm/mat4x4.hpp>
#include <glm/mat3x3.hpp>
#include <glm/vec3.hpp>

namespace sparks::render {

enum class GpuProfileSlot : int {
    Skydome = 0,
    Terrain,
    Water,
    Import,
    Weather,
    Total,
    Count
};

// All per-frame matrices and derived quantities passed to render pass methods.
struct RenderContext {
    glm::mat4 projection{};
    glm::mat4 view{};
    glm::mat4 viewRotOnly{};  // view with translation stripped (skydome)
    glm::mat4 world{};
    glm::mat4 skyWorld{};     // translate(cameraPos) * rotX * rotY
    glm::mat4 inverseWorld{};
    glm::mat3 worldRot3x3{};
    glm::vec3 cameraPos{};
    glm::vec3 cameraTarget{};
    glm::vec3 rotatedLightDir{};
    glm::vec3 rotatedCameraPos{};
    float elapsedSeconds{0.0f};
    float envBloom{0.0f};
};

} // namespace sparks::render
