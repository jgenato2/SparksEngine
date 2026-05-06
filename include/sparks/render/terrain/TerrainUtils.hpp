#pragma once
#include "DiamondSquareTerrain.hpp"
#include <glm/vec2.hpp>
#include "sparks/render/Renderer.hpp"

namespace sparks::render {

float sampleTerrainHeight(const EnvironmentSettings& environmentSettings, const glm::vec2& positionXZ);
float sampleOceanWaveHeight(const EnvironmentSettings& environmentSettings, const glm::vec2& positionXZ, float timeSeconds);

} // namespace sparks::render
