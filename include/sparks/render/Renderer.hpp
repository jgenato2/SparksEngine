#include <string>
#pragma once


#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>

#include "sparks/render/WeatherRenderer.hpp"
#include "sparks/render/WaterChunkCuller.hpp"
#include "sparks/render/TerrainChunkCuller.hpp"
#include "sparks/render/SkydomeChunkCuller.hpp"

namespace sparks::render {

struct StarData {
    glm::vec3 direction;
    glm::vec3 color;
    float twinklePhase;
    float twinkleAmp;
    float radius;
};


struct ViewControls {
    float zoomDistance{4.0f};
    float zoomTargetDistance{4.0f};
    glm::vec2 panOffset{0.0f, 0.0f};
    glm::vec2 panTargetOffset{0.0f, 0.0f};
    float orbitTargetZ{0.0f};
    float orbitTargetZTarget{0.0f};
    glm::vec2 worldRotationDegrees{72.0f, 0.0f};
};

struct ImportedModelData {
    // --- Animation/Bone Data ---
    std::vector<std::string> boneNames;
    struct Keyframe {
        float time;
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
    };
    struct AnimationChannel {
        std::string boneName;
        std::vector<Keyframe> keyframes;
    };
    struct AnimationClip {
        std::string name;
        float duration = 0.0f;
        float ticksPerSecond = 1.0f;
        std::vector<AnimationChannel> channels;
    };
    std::vector<AnimationClip> animations;
    std::vector<glm::mat4> boneTransforms;
    std::vector<int> boneParentIndices;
    // Mesh/vertex data
    std::vector<float> vertices; // interleaved: position(3), normal(3), uv(2)
    std::vector<unsigned int> indices;
    // Skinning data: 4 bone indices and 4 weights per vertex (same order as vertices)
    std::vector<uint8_t> boneIndices; // 4 per vertex
    std::vector<float> boneWeights;   // 4 per vertex
    std::vector<unsigned char> textureRgba;
    int textureWidth{1};

        // --- Cloud shadow mapping resources ---
        void loadCloudShadowMapShader();
        void renderCloudShadowMap(float time, const glm::vec3& sunDir);
        void initializeCloudShadowMap(int size = 1024);
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
    bool floatOnWater{false};
    float floatHeightOffset{0.0f};
    float floatBobAmplitude{0.06f};
    float floatBobFrequency{0.85f};
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
            float starDensity{0.05f}; // 0 = none, 0.1 = sparse, 1 = max
        // Real-world sun and wind parameters
        float latitude{0.0f};      // Degrees, -90 to 90
        float longitude{0.0f};     // Degrees, -180 to 180
        float utcTime{0.0f};       // Decimal hours since midnight UTC
        glm::vec2 windDirection{1.0f, 0.0f}; // XZ wind direction, normalized
        float windSpeed{2.0f};     // m/s
    bool enableSkydome{true};
    float skydomeRadius{800.0f};
    glm::vec3 skyHorizonColor{0.70f, 0.82f, 0.95f};
    glm::vec3 skyZenithColor{0.35f, 0.55f, 0.82f};
    glm::vec3 skyCloudColor{0.95f, 0.97f, 1.0f};
    float skyCloudAmount{0.32f};
    float skyCloudScale{1.0f};
    float skyCloudSpeed{1.0f};
    float skyCloudShadowStrength{0.7f};
    bool enableSun{true};
    float sunDiscSize{1.0f};
    float sunIntensity{1.0f};
    float waterSunStrength{2.4f};
    float objectSunGlowStrength{0.36f};
    glm::vec3 sunColor{1.00f, 0.90f, 0.70f};
    float sunHeatStrength{0.35f};
    float dustAmount{0.16f};
    glm::vec3 dustColor{0.92f, 0.80f, 0.62f};
    float sunRayStrength{1.18f};
    float lensFlareStrength{0.92f};
    // New skydome shader parameters
    float sunHaloSize{0.18f};
    float sunHaloStrength{0.7f};

    bool enableFog{true};
    glm::vec3 fogColor{0.67f, 0.75f, 0.83f};
    float fogNear{14.0f};
    float fogFar{120.0f};
    float fogStrength{0.52f};

    bool enableTerrain{true};
    float terrainSize{220.0f};
    float terrainHeight{-5.0f};
    glm::vec3 terrainColorA{0.16f, 0.20f, 0.14f};
    glm::vec3 terrainColorB{0.24f, 0.28f, 0.20f};
    float terrainPatchScale{0.12f};
    float terrainRoughness{1.0f};
    float fbxShadowSoftness{1.0f};
    glm::vec3 terrainLightDirection{0.30f, 0.72f, -0.46f};

    bool enableCloudObjects{true};
    std::vector<CloudObjectSettings> cloudObjects{};

    bool enableWater{false};
    float waterLevel{0.0f};
    float waterHalfExtent{220.0f};
    glm::vec3 waterColor{0.10f, 0.36f, 0.54f};
    float waterOpacity{0.42f};
    float waterReflectionStrength{1.0f};
    float waveAmplitude{0.18f};
    float waveFrequency{2.0f};
    float waterFoamIntensity{1.0f};

    bool enableCinematic{false};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Precomputed star data for skydome
    std::vector<StarData> m_starList;
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
        int rainConcept,
        bool useCustomVisualProfile,
        const glm::vec3& rainTint,
        const glm::vec3& splashTint,
        const glm::vec3& dropletTint,
        const glm::vec3& rippleTint,
        float rainStyleBoost,
        float particleStyleBoost,
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

    unsigned int viewportTexture() const { return m_usePostProcessed ? m_postColorTexture : m_colorTexture; }
    int lastWaterVisibleVertices() const { return m_lastWaterVisibleVertices; }
    int lastTerrainVisibleVertices() const { return m_lastTerrainVisibleVertices; }
    int lastSkydomeVisibleVertices() const { return m_lastSkydomeVisibleVertices; }

    // --- Cloud shadow mapping resources ---
    void loadCloudShadowMapShader();
    void renderCloudShadowMap(float time, const glm::vec3& sunDir);
    void initializeCloudShadowMap(int size = 1024);

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
    SkydomeChunkCuller m_skydomeChunkCuller;
    int m_lastSkydomeVisibleVertices{0};

    unsigned int m_terrainProgram{0};
    unsigned int m_terrainVao{0};
    unsigned int m_terrainVbo{0};
    unsigned int m_terrainEbo{0};
    unsigned int m_terrainTexture{0};
    int m_terrainIndexCount{0};
    bool m_hasTerrainTexture{false};
    TerrainChunkCuller m_terrainChunkCuller;
    int m_lastTerrainVisibleVertices{0};

    unsigned int m_waterProgram{0};
    unsigned int m_waterVao{0};
    unsigned int m_waterVbo{0};
    unsigned int m_waterEbo{0};
    int m_waterIndexCount{0};
    WaterChunkCuller m_waterChunkCuller;
    int m_lastWaterVisibleVertices{0};

    unsigned int m_underwaterProgram{0};
    unsigned int m_fullscreenVao{0};
    unsigned int m_postFbo{0};
    unsigned int m_postColorTexture{0};
    bool m_cameraUnderwater{false};
    bool m_usePostProcessed{false};

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
    glm::vec3 m_importDimensions{0.0f, 0.0f, 0.0f};

    WeatherRenderer m_weatherRenderer;
    float m_rainIntensity{0.0f};
    int m_rainConcept{1};
    bool m_useCustomVisualProfile{false};
    glm::vec3 m_rainTint{1.0f, 1.0f, 1.0f};
    glm::vec3 m_splashTint{1.0f, 1.0f, 1.0f};
    glm::vec3 m_dropletTint{1.0f, 1.0f, 1.0f};
    glm::vec3 m_rippleTint{1.0f, 1.0f, 1.0f};
    float m_rainStyleBoost{1.0f};
    float m_particleStyleBoost{1.0f};
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

    // Cloud shadow map shader program
    unsigned int m_cloudShadowMapProgram = 0;
    int m_cloudShadowMapSunViewProjLoc = -1;
    int m_cloudShadowMapTimeLoc = -1;
    int m_cloudShadowMapBaseLoc = -1;
    int m_cloudShadowMapTopLoc = -1;
    int m_cloudShadowMapDensityLoc = -1;

    unsigned int m_cloudShadowFbo = 0;
    unsigned int m_cloudShadowTex = 0;
    int m_cloudShadowMapSize = 1024;
    glm::mat4 m_sunViewProj = glm::mat4(1.0f);
};

}  // namespace sparks::render
