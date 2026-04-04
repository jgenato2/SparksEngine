#include "sparks/core/WeatherSystem.hpp"

#include <algorithm>
#include <cmath>
#include <random>

#include <glm/common.hpp>
#include <glm/vec2.hpp>

namespace {

constexpr float kRainTopMinY = 2.0f;
constexpr float kRainTopMaxY = 35.0f;
constexpr float kRainGroundOffsetY = -0.70f;
constexpr float kGravity = 19.2f;
constexpr float kRainGravityAccel = 30.0f;
constexpr std::size_t kMaxSplashParticles = 2400;
constexpr std::size_t kMaxDropletParticles = 1800;
constexpr std::size_t kMaxRippleParticles = 1200;

std::mt19937& weatherRng() {
    static std::mt19937 rng(0x51A7B2E1u);
    return rng;
}

float randomRange(const float minValue, const float maxValue) {
    std::uniform_real_distribution<float> distribution(minValue, maxValue);
    return distribution(weatherRng());
}

float terrainHeightOffset(const glm::vec2& positionXZ, const float patchScale, const float roughness) {
    const float scaleA = std::max(patchScale, 0.001f) * 0.14f;
    const float scaleB = std::max(patchScale, 0.001f) * 0.33f;
    const float ridge = std::sin(positionXZ.x * scaleA) * std::cos(positionXZ.y * scaleA * 1.17f);
    const float swell = std::sin(positionXZ.x * scaleB + 1.3f) * std::sin(positionXZ.y * scaleB * 0.87f - 0.8f);
    return ridge * (1.35f * roughness) + swell * (0.42f * roughness);
}

}  // namespace

namespace sparks::core {

WeatherSystem::WeatherSystem() {
    ensureDropCount();
}

void WeatherSystem::setSettings(const WeatherSettings& settings) {
    m_settings = settings;
    // Custom profile mode is intentionally disabled for consistent rain behavior.
    m_settings.useCustomVisualProfile = false;
    m_settings.rainTint = glm::vec3(1.0f, 1.0f, 1.0f);
    m_settings.splashTint = glm::vec3(1.0f, 1.0f, 1.0f);
    m_settings.dropletTint = glm::vec3(1.0f, 1.0f, 1.0f);
    m_settings.rippleTint = glm::vec3(1.0f, 1.0f, 1.0f);
    m_settings.rainStyleBoost = 1.0f;
    m_settings.particleStyleBoost = 1.0f;
    m_settings.rainConcept = std::clamp(m_settings.rainConcept, 0, 5);
    m_settings.rainTint.r = std::clamp(m_settings.rainTint.r, 0.0f, 2.0f);
    m_settings.rainTint.g = std::clamp(m_settings.rainTint.g, 0.0f, 2.0f);
    m_settings.rainTint.b = std::clamp(m_settings.rainTint.b, 0.0f, 2.0f);
    m_settings.splashTint.r = std::clamp(m_settings.splashTint.r, 0.0f, 2.0f);
    m_settings.splashTint.g = std::clamp(m_settings.splashTint.g, 0.0f, 2.0f);
    m_settings.splashTint.b = std::clamp(m_settings.splashTint.b, 0.0f, 2.0f);
    m_settings.dropletTint.r = std::clamp(m_settings.dropletTint.r, 0.0f, 2.0f);
    m_settings.dropletTint.g = std::clamp(m_settings.dropletTint.g, 0.0f, 2.0f);
    m_settings.dropletTint.b = std::clamp(m_settings.dropletTint.b, 0.0f, 2.0f);
    m_settings.rippleTint.r = std::clamp(m_settings.rippleTint.r, 0.0f, 2.0f);
    m_settings.rippleTint.g = std::clamp(m_settings.rippleTint.g, 0.0f, 2.0f);
    m_settings.rippleTint.b = std::clamp(m_settings.rippleTint.b, 0.0f, 2.0f);
    m_settings.rainStyleBoost = std::clamp(m_settings.rainStyleBoost, 0.40f, 2.50f);
    m_settings.particleStyleBoost = std::clamp(m_settings.particleStyleBoost, 0.40f, 2.50f);
    m_settings.rainIntensity = std::clamp(m_settings.rainIntensity, 0.0f, 1.0f);
    m_settings.rainSpeed = std::max(m_settings.rainSpeed, 0.1f);
    m_settings.rainAreaRadius = std::max(m_settings.rainAreaRadius, 1.0f);
    m_settings.maxDrops = std::max(m_settings.maxDrops, 64);
    m_settings.rainLengthMin = std::clamp(m_settings.rainLengthMin, 0.1f, 4.0f);
    m_settings.rainLengthMax = std::clamp(m_settings.rainLengthMax, m_settings.rainLengthMin, 6.0f);
    m_settings.windX = std::clamp(m_settings.windX, -8.0f, 8.0f);
    m_settings.windZ = std::clamp(m_settings.windZ, -8.0f, 8.0f);
    m_settings.turbulence = std::clamp(m_settings.turbulence, 0.0f, 4.0f);
    m_settings.splashAmount = std::clamp(m_settings.splashAmount, 0.0f, 4.0f);
    m_settings.splashForce = std::clamp(m_settings.splashForce, 0.2f, 3.5f);
    m_settings.dropletAmount = std::clamp(m_settings.dropletAmount, 0.0f, 4.0f);
    m_settings.mistDrift = std::clamp(m_settings.mistDrift, 0.0f, 3.0f);
    m_settings.windSwayStrength = std::clamp(m_settings.windSwayStrength, 0.0f, 4.0f);
    m_settings.windSwayFrequency = std::clamp(m_settings.windSwayFrequency, 0.1f, 6.0f);
    m_settings.randomDropletBursts = std::clamp(m_settings.randomDropletBursts, 0.0f, 4.0f);
    m_settings.rippleAmount = std::clamp(m_settings.rippleAmount, 0.0f, 4.0f);
    m_settings.rippleSize = std::clamp(m_settings.rippleSize, 2.0f, 48.0f);
    m_settings.rippleOpacityScale = std::clamp(m_settings.rippleOpacityScale, 0.0f, 2.5f);
    m_settings.rainLineWidth = std::clamp(m_settings.rainLineWidth, 0.5f, 4.0f);
    m_settings.splashPointSize = std::clamp(m_settings.splashPointSize, 1.0f, 20.0f);
    m_settings.dropletPointSize = std::clamp(m_settings.dropletPointSize, 1.0f, 14.0f);
    m_settings.rainOpacityScale = std::clamp(m_settings.rainOpacityScale, 0.0f, 2.5f);
    m_settings.splashOpacityScale = std::clamp(m_settings.splashOpacityScale, 0.0f, 2.5f);
    m_settings.dropletOpacityScale = std::clamp(m_settings.dropletOpacityScale, 0.0f, 2.5f);
    ensureDropCount();
}

void WeatherSystem::update(
    const float deltaSeconds,
    const glm::vec3& center,
    const std::vector<CollisionBox>& collisionBoxes,
    const float groundY,
    const TerrainSurface& terrainSurface) {
    ensureDropCount();
    m_timeSeconds += std::max(deltaSeconds, 0.0f);
    m_groundY = groundY;

    if (!m_settings.enabled || m_settings.rainIntensity <= 0.001f || deltaSeconds <= 0.0f) {
        m_rainLineVertices.clear();
        m_splashPoints.clear();
        m_dropletPoints.clear();
        m_ripplePoints.clear();
        m_splashes.clear();
        m_droplets.clear();
        m_ripples.clear();
        return;
    }

    const float clampedDt = std::min(deltaSeconds, 0.05f);
    m_rainLineVertices.clear();
    m_rainLineVertices.reserve(m_drops.size() * 2);

    for (RainDrop& drop : m_drops) {
        const float previousTopY = drop.position.y;
        const float previousBottomY = previousTopY - drop.length;
        const float windJitterX = randomRange(-m_settings.turbulence, m_settings.turbulence);
        const float windJitterZ = randomRange(-m_settings.turbulence, m_settings.turbulence);
        const float swayPhase = drop.swayPhase + m_timeSeconds * m_settings.windSwayFrequency;
        const float swayX = std::sin(swayPhase) * m_settings.windSwayStrength;
        const float swayZ = std::cos(swayPhase * 0.77f) * m_settings.windSwayStrength;
        drop.position.x += (m_settings.windX + windJitterX * 0.25f + swayX) * clampedDt;
        drop.position.z += (m_settings.windZ + windJitterZ * 0.25f + swayZ) * clampedDt;

        // Gravity-driven rain travel.
        drop.velocityY -= kRainGravityAccel * clampedDt;
        drop.position.y += drop.velocityY * clampedDt;
        const float bottomY = drop.position.y - drop.length;
        const float terrainGroundY = sampleTerrainHeight(glm::vec2(drop.position.x, drop.position.z), terrainSurface);

        bool collidedWithObject = false;
        glm::vec3 impactPosition(drop.position.x, terrainGroundY, drop.position.z);
        for (const CollisionBox& box : collisionBoxes) {
            if (drop.position.x < box.min.x || drop.position.x > box.max.x) {
                continue;
            }
            if (drop.position.z < box.min.z || drop.position.z > box.max.z) {
                continue;
            }

            // Trigger splash when the drop tip crosses the top of an object.
            if (previousBottomY > box.max.y && bottomY <= box.max.y) {
                collidedWithObject = true;
                impactPosition = glm::vec3(drop.position.x, box.max.y, drop.position.z);
                break;
            }
        }

        if (collidedWithObject || bottomY < terrainGroundY) {
            spawnImpactParticles(collidedWithObject ? impactPosition : glm::vec3(drop.position.x, terrainGroundY, drop.position.z));
            respawnDrop(drop, center, true);
        }

        const glm::vec3 top = drop.position;
        const glm::vec3 bottom = glm::vec3(drop.position.x, drop.position.y - drop.length, drop.position.z);
        m_rainLineVertices.push_back(top);
        m_rainLineVertices.push_back(bottom);
    }

    spawnRandomDropletBursts(clampedDt, center);
    updateImpactParticles(clampedDt);
}

void WeatherSystem::ensureDropCount() {
    const int targetCount = std::max(64, static_cast<int>(static_cast<float>(m_settings.maxDrops) * m_settings.rainIntensity));
    const std::size_t count = static_cast<std::size_t>(targetCount);

    if (m_drops.size() == count) {
        return;
    }

    m_drops.resize(count);
    for (RainDrop& drop : m_drops) {
        if (drop.length <= 0.0f) {
            drop.length = randomRange(m_settings.rainLengthMin, m_settings.rainLengthMax);
            drop.speedScale = randomRange(0.75f, 1.3f);
            drop.swayPhase = randomRange(0.0f, 6.2831853f);
            drop.velocityY = -(m_settings.rainSpeed * drop.speedScale);
        }
        respawnDrop(drop, glm::vec3(0.0f), true);
    }
}

void WeatherSystem::respawnDrop(RainDrop& drop, const glm::vec3& center, const bool randomHeight) {
    const float radius = m_settings.rainAreaRadius;
    drop.position.x = center.x + randomRange(-radius, radius);
    drop.position.z = center.z + randomRange(-radius, radius);
    drop.position.y = center.y + (randomHeight
        ? randomRange(kRainTopMinY, kRainTopMaxY)
        : randomRange(kRainTopMinY, kRainTopMaxY));
    drop.length = randomRange(m_settings.rainLengthMin, m_settings.rainLengthMax);
    drop.velocityY = -(m_settings.rainSpeed * drop.speedScale * randomRange(0.90f, 1.25f));
}

void WeatherSystem::spawnImpactParticles(const glm::vec3& impactPosition) {
    if (m_settings.rainIntensity <= 0.001f) {
        return;
    }

    const int splashSpawnCount = static_cast<int>((2.0f + randomRange(0.0f, 5.0f) * m_settings.rainIntensity) * m_settings.splashAmount);
    const int dropletSpawnCount = static_cast<int>((2.0f + randomRange(0.0f, 3.0f) * m_settings.rainIntensity) * m_settings.dropletAmount);
    const int rippleSpawnCount = static_cast<int>((1.0f + randomRange(0.0f, 2.0f) * m_settings.rainIntensity) * m_settings.rippleAmount);

    for (int i = 0; i < splashSpawnCount && m_splashes.size() < kMaxSplashParticles; ++i) {
        const float angle = randomRange(0.0f, 6.2831853f);
        const float speed = randomRange(1.8f, 4.9f) * (0.7f + 0.9f * m_settings.rainIntensity) * m_settings.splashForce;
        SplashParticle particle;
        particle.position = impactPosition;
        particle.velocity = glm::vec3(
            std::cos(angle) * speed + m_settings.windX * 0.15f,
            randomRange(3.0f, 6.4f) * m_settings.splashForce,
            std::sin(angle) * speed + m_settings.windZ * 0.15f);
        particle.life = 0.0f;
        particle.maxLife = randomRange(0.22f, 0.45f);
        particle.groundY = impactPosition.y;
        m_splashes.push_back(particle);
    }

    for (int i = 0; i < dropletSpawnCount && m_droplets.size() < kMaxDropletParticles; ++i) {
        const float angle = randomRange(0.0f, 6.2831853f);
        const float horizontal = randomRange(0.2f, 1.0f);
        DropletParticle particle;
        particle.position = impactPosition + glm::vec3(0.0f, randomRange(0.05f, 0.25f), 0.0f);
        particle.velocity = glm::vec3(
            std::cos(angle) * horizontal + m_settings.windX * m_settings.mistDrift,
            randomRange(1.0f, 2.4f),
            std::sin(angle) * horizontal + m_settings.windZ * m_settings.mistDrift);
        particle.life = 0.0f;
        particle.maxLife = randomRange(0.32f, 0.72f);
        particle.groundY = impactPosition.y;
        m_droplets.push_back(particle);
    }

    for (int i = 0; i < rippleSpawnCount && m_ripples.size() < kMaxRippleParticles; ++i) {
        RippleParticle ripple;
        ripple.position = impactPosition + glm::vec3(randomRange(-0.10f, 0.10f), 0.0f, randomRange(-0.10f, 0.10f));
        ripple.life = 0.0f;
        ripple.maxLife = randomRange(0.35f, 0.9f);
        m_ripples.push_back(ripple);
    }
}

void WeatherSystem::spawnRandomDropletBursts(const float deltaSeconds, const glm::vec3& center) {
    if (m_settings.randomDropletBursts <= 0.001f || m_settings.dropletAmount <= 0.001f) {
        return;
    }

    const float expectedBursts = 26.0f * m_settings.rainIntensity * m_settings.dropletAmount * m_settings.randomDropletBursts * deltaSeconds;
    int burstCount = static_cast<int>(expectedBursts);
    if (randomRange(0.0f, 1.0f) < (expectedBursts - static_cast<float>(burstCount))) {
        ++burstCount;
    }

    for (int i = 0; i < burstCount; ++i) {
        if (m_droplets.size() >= kMaxDropletParticles) {
            return;
        }

        const float radius = m_settings.rainAreaRadius;
        const glm::vec3 spawnPos(
            center.x + randomRange(-radius, radius),
            randomRange(0.6f, 4.2f),
            center.z + randomRange(-radius, radius));
        const float angle = randomRange(0.0f, 6.2831853f);
        const float horizontal = randomRange(0.05f, 1.3f);

        DropletParticle particle;
        particle.position = spawnPos;
        particle.velocity = glm::vec3(
            std::cos(angle) * horizontal + m_settings.windX * m_settings.mistDrift,
            randomRange(-0.4f, 1.6f),
            std::sin(angle) * horizontal + m_settings.windZ * m_settings.mistDrift);
        particle.life = 0.0f;
        particle.maxLife = randomRange(0.25f, 0.85f);
        m_droplets.push_back(particle);
    }
}

void WeatherSystem::updateImpactParticles(const float deltaSeconds) {
    auto splashAlive = [](const SplashParticle& particle) {
        return particle.life < particle.maxLife && particle.position.y > particle.groundY - 0.02f;
    };
    auto dropletAlive = [](const DropletParticle& particle) {
        return particle.life < particle.maxLife && particle.position.y > particle.groundY - 0.1f;
    };
    auto rippleAlive = [](const RippleParticle& particle) {
        return particle.life < particle.maxLife;
    };

    for (SplashParticle& particle : m_splashes) {
        particle.life += deltaSeconds;
        particle.velocity.y -= kGravity * deltaSeconds;
        particle.position += particle.velocity * deltaSeconds;
    }
    for (DropletParticle& particle : m_droplets) {
        particle.life += deltaSeconds;
        particle.velocity.x += m_settings.windX * m_settings.mistDrift * 0.3f * deltaSeconds;
        particle.velocity.z += m_settings.windZ * m_settings.mistDrift * 0.3f * deltaSeconds;
        particle.velocity.y -= (kGravity * 0.55f) * deltaSeconds;
        particle.position += particle.velocity * deltaSeconds;
    }
    for (RippleParticle& particle : m_ripples) {
        particle.life += deltaSeconds;
    }

    m_splashes.erase(
        std::remove_if(m_splashes.begin(), m_splashes.end(), [&](const SplashParticle& particle) { return !splashAlive(particle); }),
        m_splashes.end());
    m_droplets.erase(
        std::remove_if(m_droplets.begin(), m_droplets.end(), [&](const DropletParticle& particle) { return !dropletAlive(particle); }),
        m_droplets.end());
    m_ripples.erase(
        std::remove_if(m_ripples.begin(), m_ripples.end(), [&](const RippleParticle& particle) { return !rippleAlive(particle); }),
        m_ripples.end());

    m_splashPoints.clear();
    m_dropletPoints.clear();
    m_ripplePoints.clear();
    m_splashPoints.reserve(m_splashes.size());
    m_dropletPoints.reserve(m_droplets.size());
    m_ripplePoints.reserve(m_ripples.size());

    for (const SplashParticle& particle : m_splashes) {
        const float t = glm::clamp(particle.life / std::max(0.001f, particle.maxLife), 0.0f, 1.0f);
        const float alpha = (1.0f - t) * (0.45f + 0.45f * m_settings.rainIntensity) * m_settings.splashOpacityScale;
        m_splashPoints.emplace_back(particle.position, alpha);
    }

    for (const DropletParticle& particle : m_droplets) {
        const float t = glm::clamp(particle.life / std::max(0.001f, particle.maxLife), 0.0f, 1.0f);
        const float alpha = (1.0f - t) * (0.22f + 0.48f * m_settings.rainIntensity) * m_settings.dropletOpacityScale;
        m_dropletPoints.emplace_back(particle.position, alpha);
    }

    for (const RippleParticle& particle : m_ripples) {
        const float progress = glm::clamp(particle.life / std::max(0.001f, particle.maxLife), 0.0f, 1.0f);
        const float encoded = glm::clamp(progress, 0.0f, 1.0f);
        m_ripplePoints.emplace_back(particle.position, encoded);
    }
}

float WeatherSystem::sampleTerrainHeight(const glm::vec2& positionXZ, const TerrainSurface& terrainSurface) const {
    if (!terrainSurface.enabled) {
        return m_groundY;
    }

    const float halfSize = std::max(terrainSurface.size, 1.0f) * 0.5f;
    if (positionXZ.x < -halfSize || positionXZ.x > halfSize || positionXZ.y < -halfSize || positionXZ.y > halfSize) {
        return terrainSurface.height;
    }

    return terrainSurface.height + terrainHeightOffset(positionXZ, terrainSurface.patchScale, terrainSurface.roughness);
}

}  // namespace sparks::core
