#pragma once

#include <vector>

#include <glm/vec3.hpp>

#include "sparks/render/Renderer.hpp"

namespace sparks::render {

std::vector<StarData> generateStarList(int tileU = 16, int tileV = 12);
glm::vec3 computeSunDirection(float latitude, float longitude, float utcTime, int dayOfYear);

} // namespace sparks::render
