#pragma once

#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace sparks::render {

class WeatherRenderer {
public:
    WeatherRenderer() = default;
    ~WeatherRenderer();

    WeatherRenderer(const WeatherRenderer&) = delete;
    WeatherRenderer& operator=(const WeatherRenderer&) = delete;

    void initialize();
    void shutdown();

    void updateRainGeometry(const std::vector<glm::vec3>& lineVertices);
    void updateSplashGeometry(const std::vector<glm::vec4>& splashPoints);
    void updateDropletGeometry(const std::vector<glm::vec4>& dropletPoints);
    void updateRippleGeometry(const std::vector<glm::vec4>& ripplePoints);
    void renderRain(
        const glm::mat4& projection,
        const glm::mat4& view,
        const glm::mat4& world,
        int rainConcept,
        bool useCustomVisualProfile,
        const glm::vec3& rainTint,
        float rainStyleBoost,
        float intensity,
        bool enabled,
        float lineWidth,
        float opacityScale) const;
    void renderSplashes(
        const glm::mat4& projection,
        const glm::mat4& view,
        const glm::mat4& world,
        int rainConcept,
        bool useCustomVisualProfile,
        const glm::vec3& splashTint,
        float particleStyleBoost,
        float intensity,
        bool enabled,
        float pointSize,
        float opacityScale) const;
    void renderDroplets(
        const glm::mat4& projection,
        const glm::mat4& view,
        const glm::mat4& world,
        int rainConcept,
        bool useCustomVisualProfile,
        const glm::vec3& dropletTint,
        float particleStyleBoost,
        float intensity,
        bool enabled,
        float pointSize,
        float opacityScale) const;
    void renderRipples(
        const glm::mat4& projection,
        const glm::mat4& view,
        const glm::mat4& world,
        int rainConcept,
        bool useCustomVisualProfile,
        const glm::vec3& rippleTint,
        float particleStyleBoost,
        float intensity,
        bool enabled,
        float pointSize,
        float opacityScale) const;

private:
    unsigned int m_rainProgram{0};
    unsigned int m_rainVao{0};
    unsigned int m_rainVbo{0};
    int m_rainVertexCount{0};

    unsigned int m_particleProgram{0};
    unsigned int m_splashVao{0};
    unsigned int m_splashVbo{0};
    int m_splashCount{0};

    unsigned int m_dropletVao{0};
    unsigned int m_dropletVbo{0};
    int m_dropletCount{0};

    unsigned int m_rippleVao{0};
    unsigned int m_rippleVbo{0};
    int m_rippleCount{0};
};

}  // namespace sparks::render
