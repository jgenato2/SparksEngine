#include "sparks/render/terrain/TerrainUtils.hpp"
#include <algorithm>
#include <cmath>

namespace sparks::render {

DiamondSquareTerrain g_diamondSquare(129, 1.0f, 42.0f);

float sampleTerrainHeight(const EnvironmentSettings& environmentSettings, const glm::vec2& positionXZ) {
    if (!environmentSettings.enableTerrain) {
        return environmentSettings.terrainHeight;
    }
    float terrainSize = std::max(environmentSettings.terrainSize, 1.0f);
    float fx = (positionXZ.x / terrainSize + 0.5f) * (g_diamondSquare.size() - 1);
    float fy = (positionXZ.y / terrainSize + 0.5f) * (g_diamondSquare.size() - 1);
    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    int x1 = std::min(x0 + 1, g_diamondSquare.size() - 1);
    int y1 = std::min(y0 + 1, g_diamondSquare.size() - 1);
    float tx = fx - x0;
    float ty = fy - y0;
    float h00 = g_diamondSquare.get(x0, y0);
    float h10 = g_diamondSquare.get(x1, y0);
    float h01 = g_diamondSquare.get(x0, y1);
    float h11 = g_diamondSquare.get(x1, y1);
    float h0 = h00 * (1 - tx) + h10 * tx;
    float h1 = h01 * (1 - tx) + h11 * tx;
    float h = h0 * (1 - ty) + h1 * ty;
    return environmentSettings.terrainHeight + h * environmentSettings.terrainRoughness;
}

float sampleOceanWaveHeight(const EnvironmentSettings& environmentSettings, const glm::vec2& positionXZ, float timeSeconds) {
    if (!environmentSettings.enableWater) {
        return environmentSettings.waterLevel;
    }
    const float amp = std::max(environmentSettings.waveAmplitude, 0.001f);
    const float wf = std::max(environmentSettings.waveFrequency, 0.01f);
    const float baseWavelength = 30.0f / wf;
    const float gravity = 9.81f;
    const float pi = 3.14159265359f;
    auto addWave = [&](const glm::vec2& direction, float amplitudeScale, float wavelengthScale, float speedScale) {
        const glm::vec2 dir = glm::normalize(direction);
        const float wavelength = baseWavelength * wavelengthScale;
        const float k = 2.0f * pi / std::max(wavelength, 0.0001f);
        const float angularVelocity = std::sqrt(gravity * k) * speedScale;
        return amp * amplitudeScale * std::sin(k * glm::dot(dir, positionXZ) + angularVelocity * timeSeconds);
    };
    float height = environmentSettings.waterLevel;
    height += addWave(glm::vec2( 1.00f,  0.42f), 1.00f, 1.00f, 0.88f);
    height += addWave(glm::vec2(-0.55f,  1.00f), 0.68f, 0.65f, 0.95f);
    height += addWave(glm::vec2( 0.80f, -0.62f), 0.38f, 0.40f, 1.10f);
    height += addWave(glm::vec2(-0.90f,  0.45f), 0.28f, 0.30f, 1.22f);
    height += addWave(glm::vec2( 0.40f,  1.00f), 0.14f, 0.16f, 1.30f);
    height += addWave(glm::vec2( 1.00f, -0.22f), 0.10f, 0.12f, 1.45f);
    return height;
}

} // namespace sparks::render
