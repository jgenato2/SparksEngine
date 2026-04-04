#pragma once

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace sparks::core {

struct WeatherSettings {
    bool enabled{true};
    int rainConcept{1};
    bool useCustomVisualProfile{false};
    glm::vec3 rainTint{1.0f, 1.0f, 1.0f};
    glm::vec3 splashTint{1.0f, 1.0f, 1.0f};
    glm::vec3 dropletTint{1.0f, 1.0f, 1.0f};
    glm::vec3 rippleTint{1.0f, 1.0f, 1.0f};
    float rainStyleBoost{1.0f};
    float particleStyleBoost{1.0f};
    float rainIntensity{0.55f};
    float rainSpeed{8.5f};
    float rainAreaRadius{36.0f};
    int maxDrops{1800};
    float rainLengthMin{0.90f};
    float rainLengthMax{2.70f};
    float windX{0.30f};
    float windZ{0.12f};
    float turbulence{0.60f};
    float splashAmount{1.8f};
    float splashForce{1.35f};
    float dropletAmount{2.2f};
    float mistDrift{0.30f};
    float windSwayStrength{0.65f};
    float windSwayFrequency{1.30f};
    float randomDropletBursts{1.0f};
    float rippleAmount{1.0f};
    float rippleSize{16.0f};
    float rippleOpacityScale{1.0f};
    float rainLineWidth{1.1f};
    float splashPointSize{10.5f};
    float dropletPointSize{5.2f};
    float rainOpacityScale{1.0f};
    float splashOpacityScale{1.6f};
    float dropletOpacityScale{1.3f};
};

class WeatherSystem {
public:
    struct CollisionBox {
        glm::vec3 min{0.0f, 0.0f, 0.0f};
        glm::vec3 max{0.0f, 0.0f, 0.0f};
    };

    struct TerrainSurface {
        bool enabled{false};
        float size{220.0f};
        float height{-0.76f};
        float patchScale{0.12f};
        float roughness{1.0f};
    };

    WeatherSystem();

    void setSettings(const WeatherSettings& settings);
    const WeatherSettings& settings() const { return m_settings; }

    void update(
        float deltaSeconds,
        const glm::vec3& center,
        const std::vector<CollisionBox>& collisionBoxes = {},
        float groundY = -0.7f,
        const TerrainSurface& terrainSurface = {});
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
        float velocityY{-8.0f};
    };

    struct SplashParticle {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f, 0.0f, 0.0f};
        float life{0.0f};
        float maxLife{0.0f};
        float groundY{0.0f};
    };

    struct DropletParticle {
        glm::vec3 position{0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f, 0.0f, 0.0f};
        float life{0.0f};
        float maxLife{0.0f};
        float groundY{0.0f};
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
    float sampleTerrainHeight(const glm::vec2& positionXZ, const TerrainSurface& terrainSurface) const;

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
    float m_groundY{-0.7f};
};

}  // namespace sparks::core
