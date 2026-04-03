#pragma once

#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace sparks::core {

struct WeatherSettings {
    bool enabled{true};
    float rainIntensity{0.55f};
    float rainSpeed{8.5f};
    float rainAreaRadius{18.0f};
    int maxDrops{1800};
    float rainLengthMin{0.45f};
    float rainLengthMax{1.35f};
    float windX{0.30f};
    float windZ{0.12f};
    float turbulence{0.60f};
    float splashAmount{1.0f};
    float splashForce{1.0f};
    float dropletAmount{1.0f};
    float mistDrift{0.30f};
    float windSwayStrength{0.65f};
    float windSwayFrequency{1.30f};
    float randomDropletBursts{1.0f};
    float rippleAmount{1.0f};
    float rippleSize{16.0f};
    float rippleOpacityScale{1.0f};
    float rainLineWidth{1.1f};
    float splashPointSize{6.8f};
    float dropletPointSize{3.8f};
    float rainOpacityScale{1.0f};
    float splashOpacityScale{1.0f};
    float dropletOpacityScale{1.0f};
};

class WeatherSystem {
public:
    WeatherSystem();

    void setSettings(const WeatherSettings& settings);
    const WeatherSettings& settings() const { return m_settings; }

    void update(float deltaSeconds, const glm::vec3& center);
    const std::vector<glm::vec3>& rainLineVertices() const { return m_rainLineVertices; }
    const std::vector<glm::vec4>& splashPoints() const { return m_splashPoints; }
    const std::vector<glm::vec4>& dropletPoints() const { return m_dropletPoints; }
    const std::vector<glm::vec4>& ripplePoints() const { return m_ripplePoints; }

private:
    struct RainDrop {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        float length{0.7f};
        float speedScale{1.0f};
        float swayPhase{0.0f};
    };

    struct SplashParticle {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f, 0.0f, 0.0f};
        float life{0.0f};
        float maxLife{0.0f};
    };

    struct DropletParticle {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f, 0.0f, 0.0f};
        float life{0.0f};
        float maxLife{0.0f};
    };

    struct RippleParticle {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        float life{0.0f};
        float maxLife{0.0f};
    };

    void ensureDropCount();
    void respawnDrop(RainDrop& drop, const glm::vec3& center, bool randomHeight);
    void spawnImpactParticles(const glm::vec3& impactPosition);
    void spawnRandomDropletBursts(float deltaSeconds, const glm::vec3& center);
    void updateImpactParticles(float deltaSeconds);

    WeatherSettings m_settings{};
    std::vector<RainDrop> m_drops;
    std::vector<SplashParticle> m_splashes;
    std::vector<DropletParticle> m_droplets;
    std::vector<RippleParticle> m_ripples;
    std::vector<glm::vec3> m_rainLineVertices;
    std::vector<glm::vec4> m_splashPoints;
    std::vector<glm::vec4> m_dropletPoints;
    std::vector<glm::vec4> m_ripplePoints;
    float m_timeSeconds{0.0f};
};

}  // namespace sparks::core
