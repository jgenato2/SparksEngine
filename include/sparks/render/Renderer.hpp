#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "sparks/render/WeatherRenderer.hpp"

namespace sparks::render {

struct ViewControls {
    float zoomDistance{4.0f};
    float zoomTargetDistance{4.0f};
    glm::vec2 panOffset{0.0f, 0.0f};
    glm::vec2 panTargetOffset{0.0f, 0.0f};
    float orbitTargetZ{0.0f};
    float orbitTargetZTarget{0.0f};
    glm::vec2 worldRotationDegrees{0.0f, 0.0f};
};

struct ImportedModelData {
    std::vector<float> vertices; // interleaved: position(3), normal(3), uv(2)
    std::vector<unsigned int> indices;
    std::vector<unsigned char> textureRgba;
    int textureWidth{1};
    int textureHeight{1};
    float opacity{1.0f};
    bool alphaBlend{false};
    float alphaCutoff{0.01f};
    bool unlitShading{false};
    glm::vec4 diffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEulerDegrees{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 dimensions{0.0f, 0.0f, 0.0f};
};

struct CloudObjectSettings {
    bool enabled{true};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEulerDegrees{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{3.4f, 1.7f, 2.4f};
    glm::vec3 color{0.95f, 0.97f, 1.0f};
    float opacity{0.78f};
    float softness{0.82f};
    float detail{1.0f};
    float planeFade{0.90f};
    int planeCount{1};
    float cubeSpread{1.0f};
    float glowStrength{0.50f};
    float motionSpeed{1.0f};
    int cloudType{0}; // 0: Cumulus, 1: Stratus, 2: Cirrus, 3: Mares Tail
    std::uint32_t planeSelectionSeed{1u};
};

struct EnvironmentSettings {
    bool enableSkydome{true};
    float skydomeRadius{220.0f};
    float skydomePitchDegrees{90.0f};
    float skydomeYawDegrees{0.0f};
    glm::vec3 skyHorizonColor{0.70f, 0.82f, 0.95f};
    glm::vec3 skyZenithColor{0.35f, 0.55f, 0.82f};
    glm::vec3 skyCloudColor{0.82f, 0.87f, 0.93f};
    float skyCloudAmount{0.18f};
    float skyCloudScale{1.0f};

    bool enableTerrain{true};
    float terrainSize{220.0f};
    float terrainHeight{-0.76f};
    glm::vec3 terrainColorA{0.16f, 0.20f, 0.14f};
    glm::vec3 terrainColorB{0.24f, 0.28f, 0.20f};
    float terrainPatchScale{0.12f};
    float terrainRoughness{1.0f};
    glm::vec3 terrainLightDirection{0.35f, 1.0f, 0.24f};

    bool enableCloudObjects{true};
    std::vector<CloudObjectSettings> cloudObjects{};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize();
    void setViewportSize(int width, int height);
    void setImportedModel(const ImportedModelData& model);
    void setImportedModelTransform(const glm::vec3& position, const glm::vec3& rotationEulerDegrees, const glm::vec3& scale);
    void setEnvironmentSettings(const EnvironmentSettings& settings);
    void setWeatherRain(
        const std::vector<glm::vec3>& lineVertices,
        const std::vector<glm::vec4>& splashPoints,
        const std::vector<glm::vec4>& dropletPoints,
        const std::vector<glm::vec4>& ripplePoints,
        float intensity,
        bool enabled,
        float rainLineWidth,
        float splashPointSize,
        float dropletPointSize,
        float ripplePointSize,
        float rainOpacityScale,
        float splashOpacityScale,
        float dropletOpacityScale,
        float rippleOpacityScale);
    void render(const ViewControls& viewControls);

    unsigned int viewportTexture() const { return m_colorTexture; }

private:
    void createGridResources();
    void createEnvironmentResources();
    void createFramebuffer();
    void destroyFramebuffer();
    void rebuildFramebufferIfNeeded(int width, int height);

    unsigned int m_shaderProgram{0};
    unsigned int m_gridVao{0};
    unsigned int m_gridVbo{0};
    int m_gridRegularCount{0};
    int m_gridAxisXStart{0};
    int m_gridAxisYStart{0};

    unsigned int m_skydomeProgram{0};
    unsigned int m_skydomeVao{0};
    unsigned int m_skydomeVbo{0};
    unsigned int m_skydomeEbo{0};
    unsigned int m_skydomeTexture{0};
    int m_skydomeIndexCount{0};
    bool m_hasSkydomeTexture{false};

    unsigned int m_terrainProgram{0};
    unsigned int m_terrainVao{0};
    unsigned int m_terrainVbo{0};
    unsigned int m_terrainEbo{0};
    unsigned int m_terrainTexture{0};
    int m_terrainIndexCount{0};
    bool m_hasTerrainTexture{false};

    unsigned int m_cloudProgram{0};
    unsigned int m_cloudVao{0};
    unsigned int m_cloudVbo{0};
    unsigned int m_cloudSortedEbo{0};
    int m_cloudIndexCount{0};
    int m_cloudMvpLoc{-1};
    int m_cloudModelLoc{-1};
    int m_cloudColorLoc{-1};
    int m_cloudOpacityLoc{-1};
    int m_cloudSoftnessLoc{-1};
    int m_cloudDetailLoc{-1};
    int m_cloudPlaneFadeLoc{-1};
    int m_cloudSpreadLoc{-1};
    int m_cloudCameraLoc{-1};
    int m_cloudLightDirLoc{-1};
    int m_cloudGlowLoc{-1};
    int m_cloudTimeLoc{-1};
    int m_cloudMotionSpeedLoc{-1};
    int m_cloudTypeLoc{-1};

    unsigned int m_texturedProgram{0};
    unsigned int m_importVao{0};
    unsigned int m_importVbo{0};
    unsigned int m_importEbo{0};
    unsigned int m_importTexture{0};
    int m_importIndexCount{0};
    float m_importOpacity{1.0f};
    bool m_importAlphaBlend{false};
    float m_importAlphaCutoff{0.01f};
    bool m_importUnlitShading{false};
    glm::vec4 m_importDiffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 m_importEmissive{0.0f, 0.0f, 0.0f};
    glm::vec3 m_importPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 m_importRotationEuler{0.0f, 0.0f, 0.0f};
    glm::vec3 m_importScale{1.0f, 1.0f, 1.0f};

    WeatherRenderer m_weatherRenderer;
    float m_rainIntensity{0.0f};
    bool m_weatherEnabled{false};
    float m_rainLineWidth{1.1f};
    float m_splashPointSize{6.8f};
    float m_dropletPointSize{3.8f};
    float m_ripplePointSize{16.0f};
    float m_rainOpacityScale{1.0f};
    float m_splashOpacityScale{1.0f};
    float m_dropletOpacityScale{1.0f};
    float m_rippleOpacityScale{1.0f};

    EnvironmentSettings m_environmentSettings{};

    unsigned int m_fbo{0};
    unsigned int m_colorTexture{0};
    unsigned int m_depthStencilRbo{0};

    int m_viewportWidth{1280};
    int m_viewportHeight{720};
};

}  // namespace sparks::render
