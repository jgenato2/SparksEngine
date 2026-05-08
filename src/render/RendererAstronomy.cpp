#include "sparks/render/RendererAstronomy.hpp"

#include <cmath>

#include <glm/common.hpp>

namespace sparks::render {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float hash21(const float x, const float y) {
    const glm::vec2 p = glm::fract(glm::vec2(x, y) * glm::vec2(127.1f, 311.7f));
    return glm::fract(p.x * p.y);
}

} // namespace

std::vector<StarData> generateStarList(const int tileU, const int tileV) {
    std::vector<StarData> stars;
    const float tileUf = static_cast<float>(tileU);
    const float tileVf = static_cast<float>(tileV);

    for (int y = 0; y < tileV; ++y) {
        for (int x = 0; x < tileU; ++x) {
            const float xf = static_cast<float>(x);
            const float yf = static_cast<float>(y);
            const float u = (xf + 0.5f) / tileUf;
            if (u < 0.04f || u > 0.96f) {
                continue;
            }

            const float starU = (hash21(xf + 0.1f, yf) * 0.6f + 0.2f) / tileUf + xf / tileUf;
            const float starV = (hash21(xf + 0.7f, yf) * 0.6f + 0.2f) / tileVf + yf / tileVf;
            const float phi = starU * 2.0f * kPi;
            const float theta = starV * kPi;

            const glm::vec3 direction(
                std::cos(phi) * std::sin(theta),
                std::cos(theta),
                std::sin(phi) * std::sin(theta));

            const float colorSeed = hash21(xf + 17.0f, yf);
            const glm::vec3 color = glm::mix(glm::vec3(1.0f, 0.95f, 0.95f), glm::vec3(0.7f, 0.85f, 1.0f), colorSeed);
            const float twinklePhase = hash21(xf + 5.7f, yf) * 2.0f * kPi;
            const float shineRand = hash21(xf + 9.7f, yf);
            const float twinkleAmp = glm::mix(0.5f, 1.0f, shineRand);
            const float sizeRand = hash21(xf + 8.3f, yf);
            const float radius = glm::mix(0.00004f, 0.00010f, sizeRand);

            stars.push_back(StarData{direction, color, twinklePhase, twinkleAmp, radius});
        }
    }

    return stars;
}

glm::vec3 computeSunDirection(const float latitude, const float longitude, const float utcTime, const int dayOfYear) {
    const float latRad = glm::radians(latitude);
    const float gamma = 2.0f * kPi / 365.0f * (dayOfYear - 1 + (utcTime - 12.0f) / 24.0f);

    const float eqTime = 229.18f * (0.000075f + 0.001868f * std::cos(gamma) - 0.032077f * std::sin(gamma)
        - 0.014615f * std::cos(2.0f * gamma) - 0.040849f * std::sin(2.0f * gamma));
    const float decl = 0.006918f - 0.399912f * std::cos(gamma) + 0.070257f * std::sin(gamma)
        - 0.006758f * std::cos(2.0f * gamma) + 0.000907f * std::sin(2.0f * gamma)
        - 0.002697f * std::cos(3.0f * gamma) + 0.00148f * std::sin(3.0f * gamma);

    const float timeOffset = eqTime + 4.0f * longitude;
    const float tst = utcTime * 60.0f + timeOffset;
    const float ha = (tst / 4.0f) - 180.0f;
    const float haRad = glm::radians(ha);

    const float elevation = std::asin(std::sin(latRad) * std::sin(decl) + std::cos(latRad) * std::cos(decl) * std::cos(haRad));
    const float azimuth = std::atan2(-std::sin(haRad), std::cos(latRad) * std::tan(decl) - std::sin(latRad) * std::cos(haRad));

    const float y = std::sin(elevation);
    const float x = std::cos(elevation) * std::sin(azimuth);
    const float z = std::cos(elevation) * std::cos(azimuth);
    return glm::normalize(glm::vec3(x, y, z));
}

} // namespace sparks::render
