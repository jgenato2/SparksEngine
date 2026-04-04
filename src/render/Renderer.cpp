#include "sparks/render/Renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <cmath>
#include <filesystem>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

namespace {

constexpr float kCameraFarPlane = 1000.0f;

float sampleOceanWaveHeight(const sparks::render::EnvironmentSettings& environmentSettings, const glm::vec2& positionXZ, const float timeSeconds) {
    if (!environmentSettings.enableWater) {
        return environmentSettings.waterLevel;
    }

    const float amp = std::max(environmentSettings.waveAmplitude, 0.001f);
    const float wf = std::max(environmentSettings.waveFrequency, 0.01f);
    const float baseWavelength = 30.0f / wf;
    const float gravity = 9.81f;
    const float pi = 3.14159265359f;

    auto addWave = [&](const glm::vec2& direction, const float amplitudeScale, const float wavelengthScale, const float speedScale) {
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

float sampleTerrainHeight(const sparks::render::EnvironmentSettings& environmentSettings, const glm::vec2& positionXZ) {
    if (!environmentSettings.enableTerrain) {
        return environmentSettings.terrainHeight;
    }

    const float patchScale = std::max(environmentSettings.terrainPatchScale, 0.001f);
    const float roughness = std::max(environmentSettings.terrainRoughness, 0.0f);
    const float scaleA = patchScale * 0.14f;
    const float scaleB = patchScale * 0.33f;
    const float ridge = std::sin(positionXZ.x * scaleA) * std::cos(positionXZ.y * scaleA * 1.17f);
    const float swell = std::sin(positionXZ.x * scaleB + 1.3f) * std::sin(positionXZ.y * scaleB * 0.87f - 0.8f);
    return environmentSettings.terrainHeight + ridge * (1.35f * roughness) + swell * (0.42f * roughness);
}

unsigned int loadTexture2DFromFile(const std::filesystem::path& filePath, const bool clampToEdge) {
    const bool isHdr = stbi_is_hdr(filePath.string().c_str()) == 1;

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned int texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT);

    if (isHdr) {
        float* pixelsHdr = stbi_loadf(filePath.string().c_str(), &width, &height, &channels, 3);
        if (pixelsHdr == nullptr || width <= 0 || height <= 0) {
            if (pixelsHdr != nullptr) {
                stbi_image_free(pixelsHdr);
            }
            glDeleteTextures(1, &texture);
            return 0;
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, pixelsHdr);
        stbi_image_free(pixelsHdr);
    } else {
        stbi_uc* pixels = stbi_load(filePath.string().c_str(), &width, &height, &channels, 4);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            if (pixels != nullptr) {
                stbi_image_free(pixels);
            }
            glDeleteTextures(1, &texture);
            return 0;
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        stbi_image_free(pixels);
    }

    glGenerateMipmap(GL_TEXTURE_2D);
    return texture;
}

unsigned int compileShader(unsigned int shaderType, const char* source) {
    const unsigned int shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        int length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(shader, length, nullptr, infoLog.data());
        throw std::runtime_error("Shader compilation failed: " + infoLog);
    }

    return shader;
}

unsigned int createProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;

        uniform mat4 uMvp;
        uniform mat4 uModel;
        out vec3 vLocalPos;
        out vec3 vWorldPos;
        out vec3 vWorldNormal;

        void main() {
            vLocalPos = aPos;
            vWorldPos = vec3(uModel * vec4(aPos, 1.0));
            mat3 normalMat = mat3(transpose(inverse(uModel)));
            vWorldNormal = normalize(normalMat * aPos);
            gl_Position = uMvp * vec4(aPos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;
        in vec3 vLocalPos;
        in vec3 vWorldPos;
        in vec3 vWorldNormal;

        uniform vec3 uColor;
        uniform float uAlpha;
        uniform float uGradientStrength;
        uniform vec3 uLightPos;

        void main() {
            const float t = clamp(vLocalPos.y * 0.5 + 0.5, 0.0, 1.0);
            const vec3 lightDir = normalize(uLightPos - vWorldPos);
            const float lambert = max(dot(normalize(vWorldNormal), lightDir), 0.0);
            const float shade = mix(0.45, 1.25, pow(lambert, 0.85));
            const vec3 lightGradient = mix(uColor * (0.65 + 0.25 * t), uColor * shade, 0.8);
            const vec3 finalColor = mix(uColor, lightGradient, uGradientStrength);
            FragColor = vec4(finalColor, uAlpha);
        }
    )";

    const unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, infoLog.data());
        throw std::runtime_error("Program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

unsigned int createTexturedProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aUv;

        uniform mat4 uMvp;
        uniform mat4 uModel;
        out vec3 vWorldPos;
        out vec3 vWorldNormal;
        out vec2 vUv;

        void main() {
            vWorldPos = vec3(uModel * vec4(aPos, 1.0));
            mat3 normalMat = mat3(transpose(inverse(uModel)));
            vWorldNormal = normalize(normalMat * aNormal);
            vUv = aUv;
            gl_Position = uMvp * vec4(aPos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;
        in vec3 vWorldPos;
        in vec3 vWorldNormal;
        in vec2 vUv;

        uniform sampler2D uTex;
        uniform vec3 uLightPos;
        uniform float uOpacity;
        uniform float uAlphaCutoff;
        uniform int uUnlitShading;
        uniform int uShadowPass;
        uniform vec4 uDiffuseColor;
        uniform vec3 uEmissive;
        uniform vec3 uSunColor;
        uniform float uSunGlowStrength;
        uniform int uWaterEnabled;
        uniform float uWaterLevel;
        uniform vec3 uWaterTint;
        uniform vec3 uCameraPos;
        uniform vec3 uFogColor;
        uniform float uFogNear;
        uniform float uFogFar;
        uniform float uFogStrength;

        void main() {
            if (uShadowPass == 1) {
                FragColor = vec4(0.02, 0.02, 0.03, 0.38);
                return;
            }

            vec4 base = texture(uTex, vUv) * uDiffuseColor;
            vec3 n = normalize(vWorldNormal);
            vec3 l = normalize(uLightPos - vWorldPos);
            float alpha = base.a * uOpacity;
            if (alpha <= uAlphaCutoff) {
                discard;
            }
            if (uUnlitShading == 1) {
                FragColor = vec4(base.rgb + uEmissive + uSunColor * (0.06 * max(uSunGlowStrength, 0.0)), alpha);
                return;
            }

            float ndl = max(dot(n, l), 0.0);
            float lit = mix(0.35, 1.25, pow(ndl, 0.9));
            float sunGlow = pow(ndl, 6.0) * max(uSunGlowStrength, 0.0);
            vec3 color = base.rgb * lit + uEmissive + uSunColor * sunGlow;

            if (uWaterEnabled == 1) {
                float waterDepth = uWaterLevel - vWorldPos.y;
                float submerged = smoothstep(0.0, 1.8, waterDepth);
                float deepSubmerged = smoothstep(0.35, 4.5, waterDepth);
                float waterline = 1.0 - smoothstep(0.0, 0.08, abs(waterDepth));
                vec3 underwaterColor = mix(color, uWaterTint * mix(0.82, 1.12, ndl), submerged * 0.58);
                underwaterColor *= mix(vec3(1.0), vec3(0.72, 0.88, 0.96), deepSubmerged);
                underwaterColor += vec3(0.07, 0.11, 0.09) * waterline * 0.45;
                color = underwaterColor;
            }

            float fogSpan = max(uFogFar - uFogNear, 0.001);
            float fogT = clamp((distance(vWorldPos, uCameraPos) - uFogNear) / fogSpan, 0.0, 1.0);
            float fogAmount = pow(fogT, 1.25) * clamp(uFogStrength, 0.0, 1.0);
            color = mix(color, uFogColor, fogAmount);

            FragColor = vec4(color, alpha);
        }
    )";

    const unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, infoLog.data());
        throw std::runtime_error("Program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

unsigned int createSkydomeProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;

        uniform mat4 uMvp;
        out vec3 vLocalPos;

        void main() {
            vLocalPos = aPos;
            gl_Position = uMvp * vec4(aPos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;

        in vec3 vLocalPos;

        uniform vec3 uHorizonColor;
        uniform vec3 uZenithColor;
        uniform vec3 uCloudColor;
        uniform float uCloudAmount;
        uniform float uCloudScale;
        uniform vec3 uSunDir;
        uniform float uSunDiscSize;
        uniform float uSunIntensity;
        uniform vec3 uSunColor;
        uniform float uSunHeatStrength;
        uniform float uDustAmount;
        uniform vec3 uDustColor;
        uniform float uSunRayStrength;
        uniform float uLensFlareStrength;
        uniform float uTime;
        uniform sampler2D uSkyTex;
        uniform int uUseTexture;

        float hash21(vec2 p) {
            p = fract(p * vec2(127.1, 311.7));
            p += dot(p, p + 184.75);
            return fract(p.x * p.y);
        }

        float noise2Sky(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            f = f * f * (3.0 - 2.0 * f);
            float a = hash21(i);
            float b = hash21(i + vec2(1.0, 0.0));
            float c = hash21(i + vec2(0.0, 1.0));
            float d = hash21(i + vec2(1.0, 1.0));
            return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
        }

        float fbm4Sky(vec2 p) {
            float v = 0.0;
            float a = 0.55;
            mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
            for (int i = 0; i < 4; ++i) {
                v += a * noise2Sky(p);
                p = rot * p * 2.02 + vec2(1.37, -0.91);
                a *= 0.5;
            }
            return v;
        }

        void main() {
            float h = clamp(vLocalPos.y * 0.5 + 0.5, 0.0, 1.0);

            // ── Directions (needed early for cloud sun-shading) ───────────────
            vec3 skyDir = normalize(vLocalPos);
            vec3 sunDir = normalize(uSunDir);
            float sunDot = max(dot(skyDir, sunDir), 0.0);

            // ── Procedural cloud layers ──────────────────────────────────────
            // Use a clamped plane projection: divide by max(y, minY) so that
            // clouds fill the upper sky hemisphere and stay visually close together.
            // Higher minY clamp = clouds pulled toward zenith (tighter clustering).
            float yGuard   = max(skyDir.y, 0.12);
            vec2  cloudPlane = skyDir.xz / yGuard;
            float cloudSc  = 0.038 * uCloudScale;
            vec2  uv0      = cloudPlane * cloudSc;

            vec2 windA = vec2( 0.018, -0.011) * uTime;
            vec2 windB = vec2(-0.009,  0.015) * uTime;
            vec2 windC = vec2( 0.013, -0.021) * uTime;

            // Domain warp: mild warp so clouds stay clumped but have organic edges.
            float warpX = fbm4Sky(uv0 * 0.55 + windB        + vec2(7.8, 3.1));
            float warpY = fbm4Sky(uv0 * 0.55 + windB * 1.3  + vec2(2.3, 8.4));
            vec2  warped = uv0 + (vec2(warpX, warpY) - 0.5) * 0.22;

            // Cumulus layer: large base shape + fine surface detail.
            // Lower base frequency = broader, more connected cloud masses.
            float cumBase   = fbm4Sky(warped + windA);
            float cumDetail = fbm4Sky(warped * 2.10 + windC + vec2(4.1, 2.7));
            float cumDens   = cumBase * 0.75 + cumDetail * 0.25;
            // Threshold: lower floor so clouds form more readily and stay together.
            float cumThresh = mix(0.44, 0.58, 1.0 - clamp(uCloudAmount, 0.0, 1.0));
            float cumAlpha  = smoothstep(cumThresh, cumThresh + 0.14, cumDens);

            // Cirrus layer: slightly larger scale so cirrus bands are continuous.
            vec2  uvCi    = cloudPlane * cloudSc * 0.55 + windA * 1.75;
            float cirA    = fbm4Sky(uvCi              + vec2(3.7, 5.2));
            float cirB    = fbm4Sky(uvCi * 1.60       + vec2(8.1, 1.9));
            float cirAlpha = smoothstep(0.48, 0.64, cirA * 0.60 + cirB * 0.40) * 0.52;

            // Fade both layers away near the horizon to prevent hard skyline edge.
            float horizFade = smoothstep(0.0, 0.22, skyDir.y);
            cumAlpha  *= horizFade;
            cirAlpha  *= horizFade;

            // ── Cloud shading ────────────────────────────────────────────────
            // Sun-facing side of clouds is up to ~40 % brighter
            float sunFacing   = dot(skyDir, sunDir) * 0.5 + 0.5;
            float cloudBright = mix(0.74, 1.14, sunFacing);
            // Thick cumulus self-shadows its own base
            float selfShadow  = 1.0 - cumAlpha * 0.30;

            // Silver lining: thin bright edge where cloud backlights against sun
            float sunVisibility = smoothstep(0.01, 0.14, sunDir.y);
            float silverEntry = pow(sunDot, 20.0);
            float silverMask  = smoothstep(0.04, 0.26, cumAlpha) * (1.0 - cumAlpha * 0.78);
            float silver      = silverEntry * silverMask * clamp(sunDir.y * 4.0, 0.0, 1.0) * sunVisibility;

            vec3  cloudLit    = uCloudColor * cloudBright * selfShadow;
            cloudLit         += uSunColor * uSunIntensity * silver * 0.28;
            // Cirrus has a slightly blue-grey tint from zenith colour bleed
            vec3  cirrusColor = mix(uCloudColor, uZenithColor * 1.10, 0.40);

            float cloudAmt = clamp(uCloudAmount, 0.0, 1.5);

            // ── Sky gradient + cloud composite ───────────────────────────────
            vec3 color = mix(uHorizonColor, uZenithColor, pow(h, 0.62));
            color = mix(color, cloudLit,    cumAlpha  * cloudAmt);
            color = mix(color, cirrusColor, cirAlpha  * cloudAmt);

            // Sun disc, halo, rays, lens flare
            float upperHemisphereMask = smoothstep(0.02, 0.30, h) * smoothstep(-0.02, 0.18, skyDir.y);
            vec3 refAxis = (abs(sunDir.y) > 0.96) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
            vec3 sunRight = normalize(cross(refAxis, sunDir));
            vec3 sunUp = normalize(cross(sunDir, sunRight));
            vec2 sunPlane = vec2(dot(skyDir, sunRight), dot(skyDir, sunUp));
            float sunPlaneLen = length(sunPlane);
            float sunAngle = atan(sunPlane.y, sunPlane.x);
            float sizeN = clamp((uSunDiscSize - 0.2) / 7.8, 0.0, 1.0);
            float discOuter = mix(0.99978, 0.99908, sizeN);
            float discInner = mix(0.99995, 0.99935, sizeN);
            float sunDisc = smoothstep(discOuter, discInner, sunDot);

            float heatZone = smoothstep(0.90, 0.999, sunDot) * upperHemisphereMask * sunVisibility;

            float haloNarrow = pow(sunDot, mix(42.0, 28.0, sizeN)) * 0.14;
            float haloWide = pow(sunDot, mix(16.0, 9.0, sizeN)) * 0.04;
            float sunHalo = haloNarrow + haloWide;
            float rayCore = pow(clamp(1.0 - sunPlaneLen * mix(15.0, 9.0, sizeN), 0.0, 1.0), 2.8);
            float crossH = pow(clamp(1.0 - abs(sunPlane.y) * 38.0, 0.0, 1.0), 7.5);
            float crossV = pow(clamp(1.0 - abs(sunPlane.x) * 42.0, 0.0, 1.0), 8.0);
            float diagA = pow(clamp(1.0 - abs(sunPlane.x + sunPlane.y) * 28.0, 0.0, 1.0), 9.0);
            float diagB = pow(clamp(1.0 - abs(sunPlane.x - sunPlane.y) * 28.0, 0.0, 1.0), 9.0);
            float raySpark = pow(abs(cos(sunAngle * 8.0)), 24.0) * 0.04;
            float flarePulse = 0.86 + 0.14 * sin(uTime * 0.90 + sunAngle * 2.0);
            float flareTwinkle = 0.70 + 0.30 * sin(uTime * 1.70 + sunAngle * 11.0 + sunPlaneLen * 120.0);
            float sunRays = rayCore * (crossH * 0.07 + crossV * 0.06 + diagA * 0.03 + diagB * 0.03 + raySpark) * flareTwinkle;
            sunRays *= clamp(uSunRayStrength, 0.0, 2.5);

            float streakH = pow(clamp(1.0 - abs(sunPlane.y) * 44.0, 0.0, 1.0), 6.0) * pow(clamp(1.0 - sunPlaneLen * 2.4, 0.0, 1.0), 2.2);
            float streakV = pow(clamp(1.0 - abs(sunPlane.x) * 50.0, 0.0, 1.0), 8.0) * pow(clamp(1.0 - sunPlaneLen * 2.8, 0.0, 1.0), 2.8);
            float lensRingOuter = smoothstep(0.24, 0.05, sunPlaneLen) * (1.0 - smoothstep(0.10, 0.02, sunPlaneLen));
            float lensRingInner = smoothstep(0.14, 0.03, sunPlaneLen) * (1.0 - smoothstep(0.055, 0.010, sunPlaneLen));
            float lensHalo = pow(clamp(1.0 - sunPlaneLen * 4.0, 0.0, 1.0), 4.5) * 0.08;
            float lensShimmer = pow(clamp(1.0 - sunPlaneLen * 5.2, 0.0, 1.0), 5.0) * (0.55 + 0.45 * sin(uTime * 1.25 + sunAngle * 9.0));
            float lensRingPulse = (0.72 + 0.28 * sin(uTime * 0.75 + sunPlaneLen * 36.0));
            float lensGain = clamp(uLensFlareStrength, 0.0, 2.5);
            float chromaRing = smoothstep(0.22, 0.11, sunPlaneLen) * (1.0 - smoothstep(0.14, 0.06, sunPlaneLen));
            float petalMask = pow(abs(cos(sunAngle * 6.0)), 6.0) * pow(clamp(1.0 - sunPlaneLen * 6.0, 0.0, 1.0), 2.5);
            float anamorphic = pow(clamp(1.0 - abs(sunPlane.y) * 28.0, 0.0, 1.0), 5.5) * pow(clamp(1.0 - sunPlaneLen * 2.6, 0.0, 1.0), 2.0);

            vec3 lensColor = vec3(uSunColor.r, uSunColor.g * 0.88, uSunColor.b * 1.05);
            vec3 chromaColor = vec3(uSunColor.r * 1.10, uSunColor.g * 0.92, uSunColor.b * 1.18);
            vec3 petalColor = mix(vec3(0.90, 0.97, 1.0), uSunColor, 0.45);

            float sunFxMask = upperHemisphereMask * sunVisibility;
            color += uSunColor * uSunIntensity * sunFxMask * (sunDisc * 0.85 + (sunHalo + sunRays) * flarePulse + streakH * 0.05 + streakV * 0.03);
            color += lensColor * uSunIntensity * sunFxMask * ((lensRingOuter * 0.08 + lensRingInner * 0.05) * lensRingPulse + lensHalo + lensShimmer * 0.16) * lensGain;
            color += chromaColor * uSunIntensity * sunFxMask * chromaRing * (0.05 * lensGain);
            color += petalColor * uSunIntensity * sunFxMask * petalMask * (0.06 * lensGain);
            color += vec3(1.0, 0.96, 0.90) * uSunIntensity * sunFxMask * anamorphic * (0.04 * lensGain);

            float horizon = 1.0 - h;
            float dustForward = smoothstep(0.0, 0.98, sunDot);
            float dustNoise = hash21(vLocalPos.xz * 120.0 + vec2(uTime * 0.08, -uTime * 0.06));
            float dustSpeck = smoothstep(0.985, 1.0, dustNoise) * dustForward;
            float dust = clamp(uDustAmount * (horizon * (0.35 + dustForward * 0.65) + dustSpeck * 0.20), 0.0, 0.95);

            if (uUseTexture == 1) {
                vec3 dir = normalize(vLocalPos);
                float u = atan(dir.z, dir.x) * 0.159154943 + 0.5;
                float v = acos(clamp(dir.y, -1.0, 1.0)) * 0.318309886;
                // Use only the upper hemisphere of the texture so the dome contains sky only.
                float vSky = clamp(v * 0.5, 0.0, 0.5);
                vec3 texColor = texture(uSkyTex, vec2(u, vSky)).rgb;
                color = mix(color, texColor, 0.88);
            }

            // Apply atmospheric dust after sky composition so UI dust controls remain visible.
            color = mix(color, uDustColor, dust);
            color += uSunColor * (uSunHeatStrength * 0.035) * heatZone;

            FragColor = vec4(color, 1.0);
        }
    )";

    const unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, infoLog.data());
        throw std::runtime_error("Skydome program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

unsigned int createTerrainProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;

        uniform mat4 uMvp;
        uniform mat4 uModel;
        uniform float uPatchScale;
        uniform float uRoughness;
        out vec3 vWorldPos;
        out vec3 vLocalPos;

        float terrainHeightOffset(vec2 positionXZ, float patchScale, float roughness) {
            float scaleA = max(patchScale, 0.001) * 0.14;
            float scaleB = max(patchScale, 0.001) * 0.33;
            float ridge = sin(positionXZ.x * scaleA) * cos(positionXZ.y * scaleA * 1.17);
            float swell = sin(positionXZ.x * scaleB + 1.3) * sin(positionXZ.y * scaleB * 0.87 - 0.8);
            return ridge * (1.35 * roughness) + swell * (0.42 * roughness);
        }

        void main() {
            vec3 localPos = aPos;
            localPos.y += terrainHeightOffset(localPos.xz, uPatchScale, uRoughness);
            vec4 worldPos = uModel * vec4(localPos, 1.0);
            vWorldPos = worldPos.xyz;
            vLocalPos = localPos;
            gl_Position = uMvp * vec4(localPos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;

        in vec3 vWorldPos;
        in vec3 vLocalPos;

        uniform vec3 uBaseA;
        uniform vec3 uBaseB;
        uniform float uPatchScale;
        uniform float uRoughness;
        uniform vec3 uLightDir;
        uniform sampler2D uTerrainTex;
        uniform int uUseTexture;
        uniform int uShadowEnabled;
        uniform vec2 uShadowCenterXZ;
        uniform vec2 uShadowDirXZ;
        uniform float uShadowMajorRadius;
        uniform float uShadowMinorRadius;
        uniform float uShadowStrength;
        uniform float uShadowSoftness;
        uniform vec3 uCameraPos;
        uniform vec3 uFogColor;
        uniform float uFogNear;
        uniform float uFogFar;
        uniform float uFogStrength;

        float hash21(vec2 p) {
            p = fract(p * vec2(123.456, 789.012));
            p += dot(p, p + 45.123);
            return fract(p.x * p.y);
        }

        float noise(vec2 p) {
            vec2 i = floor(p);
            vec2 f = fract(p);
            f = f * f * (3.0 - 2.0 * f);
            float n0 = mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x);
            float n1 = mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x);
            return mix(n0, n1, f.y);
        }

        float terrainHeightOffset(vec2 positionXZ, float patchScale, float roughness) {
            float scaleA = max(patchScale, 0.001) * 0.14;
            float scaleB = max(patchScale, 0.001) * 0.33;
            float ridge = sin(positionXZ.x * scaleA) * cos(positionXZ.y * scaleA * 1.17);
            float swell = sin(positionXZ.x * scaleB + 1.3) * sin(positionXZ.y * scaleB * 0.87 - 0.8);
            return ridge * (1.35 * roughness) + swell * (0.42 * roughness);
        }

        void main() {
            vec2 localXZ = vLocalPos.xz * uPatchScale * 0.25;
            float n0 = noise(localXZ * 0.5);
            float n1 = noise(localXZ * 2.0);
            float n2 = noise(localXZ * 8.0);
            float patchNoise = n0 * 0.5 + n1 * 0.3 + n2 * 0.2;
            float t = clamp(patchNoise, 0.0, 1.0);
            vec3 color = mix(uBaseA, uBaseB, t);

            if (uUseTexture == 1) {
                vec2 uv = vLocalPos.xz * 0.035;
                vec3 texColor = texture(uTerrainTex, uv).rgb;
                color *= mix(vec3(1.0), texColor, 0.70);
            }

            float ndl = max(dot(vec3(0.0, 1.0, 0.0), normalize(uLightDir)), 0.0);
            color *= mix(0.70, 1.15 + uRoughness * 0.12, ndl);

            // Terrain self-shadowing from hills/ridges along light direction.
            // Use the actual sun ray slope to avoid column/banding artifacts at high sun elevations.
            vec3 lightN = normalize(uLightDir);
            vec2 lightXZVec = vec2(lightN.x, lightN.z);
            float lightXZLen = length(lightXZVec);
            if (lightXZLen > 0.0001) {
                vec2 lightXZ = lightXZVec / lightXZLen;
                float raySlope = lightN.y / lightXZLen;
                float currentH = vLocalPos.y;
                float occlusion = 0.0;
                for (int i = 1; i <= 4; ++i) {
                    float dist = float(i) * 3.2;
                    vec2 sampleXZ = vLocalPos.xz + lightXZ * dist;
                    float sampleH = terrainHeightOffset(sampleXZ, uPatchScale, uRoughness);
                    float rayH = currentH + dist * raySlope;
                    float block = smoothstep(rayH + 0.02, rayH + 0.55, sampleH);
                    occlusion = max(occlusion, block * (1.0 - float(i - 1) * 0.18));
                }
                float shadowWeight = clamp(1.0 - lightN.y, 0.0, 1.0);
                color *= (1.0 - clamp(occlusion * 0.30 * shadowWeight, 0.0, 0.35));
            }

            if (uShadowEnabled == 1) {
                vec2 rel = vWorldPos.xz - uShadowCenterXZ;
                float along = dot(rel, uShadowDirXZ);
                vec2 perpVec = rel - uShadowDirXZ * along;
                float perp = length(perpVec);
                float majorRadius = max(uShadowMajorRadius, 0.1);
                float minorRadius = max(uShadowMinorRadius, 0.1);
                float ellipse = (along * along) / (majorRadius * majorRadius) + (perp * perp) / (minorRadius * minorRadius);
                float soft = clamp(uShadowSoftness, 0.1, 3.0);
                float outer = 1.0 + soft * 0.06;          // edge of ellipse (soft edge bleeds slightly outside)
                float inner = max(0.05, 1.0 - soft * 0.4); // inner full-dark zone
                float edgeSoft = smoothstep(outer, inner, ellipse);
                float shadowAmount = clamp(edgeSoft * uShadowStrength, 0.0, 0.92);
                color *= (1.0 - shadowAmount);
            }

            float fogSpan = max(uFogFar - uFogNear, 0.001);
            float fogT = clamp((distance(vWorldPos, uCameraPos) - uFogNear) / fogSpan, 0.0, 1.0);
            float fogAmount = pow(fogT, 1.25) * clamp(uFogStrength, 0.0, 1.0);
            color = mix(color, uFogColor, fogAmount);

            FragColor = vec4(color, 1.0);
        }
    )";

    const unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, infoLog.data());
        throw std::runtime_error("Terrain program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

unsigned int createWaterProgram() {
    // Realistic ocean shader – 6 Gerstner wave trains, Beckmann specular, SSS, layered foam.
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout(location = 0) in vec3 aPos;

        uniform mat4  uMvp;
        uniform float uTime;
        uniform float uWaveAmplitude;
        uniform float uWaveFrequency;

        out vec3  vWorldPos;
        out vec3  vNormal;
        out float vWaveHeight;
        out float vSteepFoam;

        const float PI = 3.14159265359;

        // Gerstner wave: accumulates displacement into pos and normal.
        // Q = peak steepness [0..1], wlen = wavelength, cspeed = phase-speed scale.
        void addGerstner(vec2 P, vec2 D, float amp, float wlen, float cspeed, float Q,
                         float t, inout vec3 pos, inout vec3 nrm) {
            float k   = 2.0 * PI / max(wlen, 0.0001);
            float w   = sqrt(9.81 * k) * cspeed;
            float phi = k * dot(D, P) + w * t;
            float s   = sin(phi);
            float c   = cos(phi);
            float Qa  = Q * amp;
            pos.x += Qa * D.x * c;
            pos.z += Qa * D.y * c;
            pos.y += amp * s;
            nrm.x -= k * amp * D.x * c;
            nrm.z -= k * amp * D.y * c;
            nrm.y -= Q * k * amp * s;
        }

        void main() {
            float amp = max(uWaveAmplitude, 0.001);
            float wf  = max(uWaveFrequency, 0.01);
            float t   = uTime;
            float bl  = 30.0 / wf;   // base wavelength

            vec3 pos = aPos;
            vec3 nrm = vec3(0.0, 1.0, 0.0);

            // 6 wave trains spanning ocean swell → wind chop spectrum
            addGerstner(pos.xz, normalize(vec2( 1.00,  0.42)), amp*1.00, bl*1.00, 0.88, 0.50, t, pos, nrm);
            addGerstner(pos.xz, normalize(vec2(-0.55,  1.00)), amp*0.68, bl*0.65, 0.95, 0.42, t, pos, nrm);
            addGerstner(pos.xz, normalize(vec2( 0.80, -0.62)), amp*0.38, bl*0.40, 1.10, 0.30, t, pos, nrm);
            addGerstner(pos.xz, normalize(vec2(-0.90,  0.45)), amp*0.28, bl*0.30, 1.22, 0.28, t, pos, nrm);
            addGerstner(pos.xz, normalize(vec2( 0.40,  1.00)), amp*0.14, bl*0.16, 1.30, 0.18, t, pos, nrm);
            addGerstner(pos.xz, normalize(vec2( 1.00, -0.22)), amp*0.10, bl*0.12, 1.45, 0.14, t, pos, nrm);

            vWorldPos   = pos;
            vNormal     = normalize(nrm);
            vWaveHeight = pos.y - aPos.y;

            // Steepness foam: sharp band at wave crests
            vSteepFoam = smoothstep(amp * 0.30, amp * 0.72, vWaveHeight);

            gl_Position = uMvp * vec4(pos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        in vec3  vWorldPos;
        in vec3  vNormal;
        in float vWaveHeight;
        in float vSteepFoam;

        uniform vec3  uWaterColor;
        uniform float uWaterOpacity;
        uniform vec3  uCameraPos;
        uniform vec3  uLightDir;
        uniform float uSunIntensity;
        uniform vec3  uSunColor;
        uniform float uTime;
        uniform float uWaveAmplitude;
        uniform vec2  uInteractionCenter;
        uniform float uInteractionRadius;
        uniform float uInteractionAmount;
        uniform vec3  uFogColor;
        uniform float uFogNear;
        uniform float uFogFar;
        uniform float uFogStrength;

        out vec4 fragColor;

        const float PI = 3.14159265359;

        // ── Noise helpers ────────────────────────────────────────────────────
        float hash21(vec2 p) {
            p = fract(p * vec2(127.1, 311.7));
            p += dot(p, p + 184.75);
            return fract(p.x * p.y);
        }
        float vnoiseWater(vec2 p) {
            vec2 i = floor(p), f = fract(p);
            f = f * f * (3.0 - 2.0 * f);
            return mix(mix(hash21(i),           hash21(i+vec2(1,0)), f.x),
                       mix(hash21(i+vec2(0,1)), hash21(i+vec2(1,1)), f.x), f.y);
        }
        float fbm3Water(vec2 p) {
            return vnoiseWater(p)*0.500 + vnoiseWater(p*2.03+1.7)*0.250 + vnoiseWater(p*4.11+3.2)*0.125;
        }
        float fbm5Water(vec2 p) {
            return vnoiseWater(p)*0.500
                 + vnoiseWater(p*2.03+1.70)*0.250
                 + vnoiseWater(p*4.11+3.20)*0.125
                 + vnoiseWater(p*8.17+5.90)*0.0625
                 + vnoiseWater(p*16.3+11.3)*0.03125;
        }
        float glitterMask(vec2 xz, float t) {
            vec2 uvA = xz * 9.5 + vec2(t * 0.18, -t * 0.15);
            vec2 uvB = xz * 18.0 + vec2(-t * 0.32, t * 0.26) + vec2(4.7, 1.9);
            float sparkle = fbm3Water(uvA) * 0.65 + fbm3Water(uvB) * 0.35;
            return smoothstep(0.64, 0.90, sparkle);
        }

        // ── Detail normal map (two FBM layers scrolling at different angles) ─
        vec3 detailNormal(vec3 geoN, vec2 xz, float t) {
            float sc = 2.0;
            vec2 uvA = xz * sc + vec2( t*0.055,  t*0.038);
            vec2 uvB = xz * sc * 0.72 + vec2(-t*0.032, t*0.060);
            float eps = 0.12;
            float hAx = fbm3Water(uvA+vec2(eps,0)) - fbm3Water(uvA-vec2(eps,0));
            float hAz = fbm3Water(uvA+vec2(0,eps)) - fbm3Water(uvA-vec2(0,eps));
            float hBx = fbm3Water(uvB+vec2(eps,0)) - fbm3Water(uvB-vec2(eps,0));
            float hBz = fbm3Water(uvB+vec2(0,eps)) - fbm3Water(uvB-vec2(0,eps));
            vec3 bA = vec3(-hAx*0.28, 1.0, -hAz*0.28);
            vec3 bB = vec3(-hBx*0.18, 1.0, -hBz*0.18);
            vec3 bump = normalize(bA + bB);
            // Orient bump into world space along geoN
            vec3 T = normalize(cross(geoN, vec3(0,0,1)));
            vec3 B = normalize(cross(geoN, T));
            return normalize(T*bump.x + B*bump.z + geoN*bump.y);
        }

        // ── Multi-layer foam mask ────────────────────────────────────────────
        // Produces whitecap sheets + trailing streaks + fine-scale turbulence
        float foamMask(vec2 xz, float t, float steepFoam) {
            // Sheet: slow wide cells (whitecap patches)
            vec2 uv1 = xz * 0.30 + vec2( t*0.028, -t*0.022);
            float sheet = smoothstep(0.64, 0.86, fbm5Water(uv1));

            // Streak: faster, narrower tendrils trailing from crests
            vec2 uv2 = xz * 0.55 + vec2(-t*0.040,  t*0.036);
            float streak = smoothstep(0.60, 0.82, fbm5Water(uv2 + vec2(2.4, 4.1)));

            // Fine: high-frequency salt-and-pepper at crest tips
            vec2 uv3 = xz * 1.20 + vec2( t*0.070, -t*0.065);
            float fine = smoothstep(0.66, 0.86, fbm3Water(uv3 + vec2(7.3, 1.9)));

            // Crest-driven base foam
            float base  = steepFoam * (0.12 + 0.34 * sheet * streak);
            // Trailing streaks that extend behind breaking crests
            float trail = sheet * streak * smoothstep(0.18, 0.42, steepFoam);
            // Fine detail only where there is already foam
            float detail = fine * 0.10 * steepFoam;

            return clamp(base + trail + detail, 0.0, 1.0);
        }

        float interactionFoam(vec2 xz, float t) {
            if (uInteractionAmount <= 0.0001 || uInteractionRadius <= 0.0001) {
                return 0.0;
            }

            vec2 delta = xz - uInteractionCenter;
            float dist = length(delta);
            float radius = uInteractionRadius;
            float inner = 1.0 - smoothstep(0.0, radius * 0.85, dist);
            float ring = 1.0 - smoothstep(radius * 0.10, radius * 0.55, abs(dist - radius * 0.92));
            float ripple = 0.5 + 0.5 * sin(dist * 8.0 - t * 4.5);
            return uInteractionAmount * clamp(inner * 0.22 + ring * ripple, 0.0, 1.0);
        }

        void main() {
            vec3 V    = normalize(uCameraPos - vWorldPos);
            vec3 geoN = normalize(vNormal);
            vec3 N    = detailNormal(geoN, vWorldPos.xz, uTime);

            // ── Lighting vectors ─────────────────────────────────────────────
            vec3  L    = normalize(uLightDir);
            vec3  H    = normalize(L + V);
            float NdV  = max(dot(N, V), 0.0);
            float NdL  = max(dot(N, L), 0.0);
            float NdH  = max(dot(N, H), 0.0);

            // ── Beckmann specular – produces a tight sun diamond on water ────
            float m    = 0.055;
            float mSq  = m * m;
            float cosSq = NdH * NdH;
            float tanSq = (1.0 - cosSq) / max(cosSq, 0.0001);
            float beckm = exp(-tanSq / mSq) / (PI * mSq * cosSq * cosSq + 0.0001);
            float spec  = clamp(beckm * 0.06, 0.0, 6.0);

            // ── Fresnel (Schlick) ─────────────────────────────────────────────
            float F0      = 0.040;
            float fresnel = F0 + (1.0 - F0) * pow(1.0 - NdV, 5.0);

            // ── Sky reflection gradient ───────────────────────────────────────
            vec3 R      = reflect(-V, N);
            float skyT  = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
            vec3 skyZen = vec3(0.14, 0.36, 0.72);
            vec3 skyHor = vec3(0.52, 0.76, 0.95);
            vec3 skyCol = mix(skyHor, skyZen, skyT * skyT);
            float sunReflect = max(dot(R, L), 0.0);
            float sunHalo = (pow(sunReflect, 24.0) * 0.42 + pow(sunReflect, 320.0) * 3.4) * uSunIntensity;
            skyCol += uSunColor * sunHalo;

            // ── Water body colour (depth-based) ──────────────────────────────
            float depth   = smoothstep(-uWaveAmplitude, uWaveAmplitude, vWaveHeight);
            vec3 shallow  = vec3(0.03, 0.66, 0.62);     // teal crest
            vec3 deep     = vec3(0.01, 0.18, 0.38);     // deep ocean
            vec3 bodyCol  = mix(deep, mix(uWaterColor, shallow, depth*0.80), 0.72 + depth*0.28);

            // ── Subsurface scattering at crests ───────────────────────────────
            // Sun light transmits through thin wave tips → bright blue-green glow
            float sss    = pow(max(dot(V, -L + N*0.1), 0.0), 3.5) * 1.80
                         * smoothstep(-0.1, 0.2, vWaveHeight / max(uWaveAmplitude, 0.001));
            vec3 sssCol  = vec3(0.04, 0.60, 0.55) * sss;
            float transmit = pow(max(dot(-V, normalize(-L + N * 0.55)), 0.0), 2.8)
                           * (1.0 - fresnel)
                           * mix(0.25, 1.0, depth);
            float refractGlow = pow(max(dot(-V, normalize(-L + N * 0.18)), 0.0), 1.8)
                              * (1.0 - fresnel)
                              * (0.35 + 0.65 * depth);
            vec3 refractCol = mix(vec3(0.16, 0.58, 0.52), uSunColor, 0.42)
                            * (transmit * 0.70 + refractGlow * 0.90)
                            * uSunIntensity;

            // Soft diffuse (water is not Lambertian but receives ambient + soft sun)
            float diff   = mix(0.10, 0.55, NdL);
            bodyCol     *= diff;
            bodyCol     += sssCol;
            bodyCol     += refractCol;

            // Blend water body with sky reflection
            vec3 waterCol = mix(bodyCol, skyCol, fresnel * 0.55);

            // Sun specular
            waterCol += uSunColor * spec * 1.35 * uSunIntensity;
            float broadTrack = pow(sunReflect, 5.0) * pow(1.0 - NdV, 1.9) * (0.20 + 0.80 * NdL) * uSunIntensity;
            float hotTrack = pow(sunReflect, 18.0) * (0.32 + 0.68 * (1.0 - NdV)) * uSunIntensity;
            float glitter = pow(sunReflect, 120.0) * glitterMask(vWorldPos.xz, uTime) * (0.6 + 0.4 * NdL) * uSunIntensity;
            waterCol += mix(vec3(1.00, 0.92, 0.70), uSunColor, 0.55) * broadTrack * 1.15;
            waterCol += uSunColor * hotTrack * 0.95;
            waterCol += mix(vec3(1.00, 0.98, 0.84), uSunColor, 0.35) * glitter * 2.10;

            // ── Foam ─────────────────────────────────────────────────────────
            float fm = foamMask(vWorldPos.xz, uTime, vSteepFoam);
            float contact = interactionFoam(vWorldPos.xz, uTime);
            fm = clamp(fm + contact * 0.85, 0.0, 1.0);
            // Foam colour: lit faces bright white, shadowed slight blue-grey
            vec3 foamLit   = mix(vec3(0.78, 0.83, 0.92), vec3(0.97, 0.98, 1.00), NdL);
            float foamAlpha = clamp(fm * 1.15, 0.0, 1.0);
            waterCol        = mix(waterCol, foamLit, foamAlpha * 0.94);
            waterCol += vec3(0.04, 0.08, 0.07) * contact;

            // Opacity: foam patches are nearly opaque; open water uses uWaterOpacity
            float opacity = mix(uWaterOpacity, 0.98, foamAlpha * 0.55);
            opacity       = max(opacity, 0.35 + fresnel * 0.45);

            float fogSpan = max(uFogFar - uFogNear, 0.001);
            float fogT = clamp((distance(vWorldPos, uCameraPos) - uFogNear) / fogSpan, 0.0, 1.0);
            float fogAmount = pow(fogT, 1.30) * clamp(uFogStrength, 0.0, 1.0);
            waterCol = mix(waterCol, uFogColor, fogAmount);

            fragColor = vec4(waterCol, opacity);
        }
    )";

    const unsigned int vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &kVertexShader, nullptr);
    glCompileShader(vertexShader);

    int success = 0;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetShaderiv(vertexShader, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(vertexShader, length, nullptr, infoLog.data());
        throw std::runtime_error("Water vertex shader compile failed: " + infoLog);
    }

    const unsigned int fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &kFragmentShader, nullptr);
    glCompileShader(fragmentShader);

    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetShaderiv(fragmentShader, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetShaderInfoLog(fragmentShader, length, nullptr, infoLog.data());
        throw std::runtime_error("Water fragment shader compile failed: " + infoLog);
    }

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, infoLog.data());
        throw std::runtime_error("Water program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

unsigned int createCloudProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in float aCardId;

        uniform mat4 uMvp;
        uniform mat4 uModel;
        uniform float uCubeSpread;
        out vec3 vLocalPos;
        out vec3 vWorldPos;
        out vec2 vCardUv;
        flat out float vCardId;

        void main() {
            vec3 localPos = aPos * uCubeSpread;
            vLocalPos = localPos;
            vec4 worldPos = uModel * vec4(localPos, 1.0);
            vWorldPos = worldPos.xyz;
            vCardUv = aUv;
            vCardId = aCardId;
            gl_Position = uMvp * vec4(localPos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;

        in vec3 vLocalPos;
        in vec3 vWorldPos;
        in vec2 vCardUv;
        flat in float vCardId;

        uniform vec3 uCloudColor;
        uniform float uCloudOpacity;
        uniform float uCloudSoftness;
        uniform float uCloudDetail;
        uniform float uPlaneFade;
        uniform vec3 uCameraWorld;
        uniform vec3 uLightDir;
        uniform float uGlowStrength;
        uniform float uTime;
        uniform float uCloudMotionSpeed;
        uniform int uCloudType;

        float hash31(vec3 p) {
            p = fract(p * 0.1031);
            p += dot(p, p.yzx + 33.33);
            return fract((p.x + p.y) * p.z);
        }

        float valueNoise3(vec3 x) {
            vec3 i = floor(x);
            vec3 f = fract(x);
            f = f * f * (3.0 - 2.0 * f);

            float n000 = hash31(i + vec3(0.0, 0.0, 0.0));
            float n100 = hash31(i + vec3(1.0, 0.0, 0.0));
            float n010 = hash31(i + vec3(0.0, 1.0, 0.0));
            float n110 = hash31(i + vec3(1.0, 1.0, 0.0));
            float n001 = hash31(i + vec3(0.0, 0.0, 1.0));
            float n101 = hash31(i + vec3(1.0, 0.0, 1.0));
            float n011 = hash31(i + vec3(0.0, 1.0, 1.0));
            float n111 = hash31(i + vec3(1.0, 1.0, 1.0));

            float nx00 = mix(n000, n100, f.x);
            float nx10 = mix(n010, n110, f.x);
            float nx01 = mix(n001, n101, f.x);
            float nx11 = mix(n011, n111, f.x);
            float nxy0 = mix(nx00, nx10, f.y);
            float nxy1 = mix(nx01, nx11, f.y);
            return mix(nxy0, nxy1, f.z);
        }

        float fbm(vec3 p) {
            float sum = 0.0;
            float amp = 0.5;
            for (int i = 0; i < 4; ++i) {
                sum += valueNoise3(p) * amp;
                p = p * 2.03 + vec3(17.17, 9.43, 5.79);
                amp *= 0.5;
            }
            return sum;
        }

        float fbmFast(vec3 p) {
            float sum = 0.0;
            float amp = 0.5;
            for (int i = 0; i < 3; ++i) {
                sum += valueNoise3(p) * amp;
                p = p * 2.08 + vec3(11.13, 7.41, 3.27);
                amp *= 0.5;
            }
            return sum;
        }

        float hash12(vec2 p) {
            vec3 p3 = fract(vec3(p.xyx) * 0.1031);
            p3 += dot(p3, p3.yzx + 33.33);
            return fract((p3.x + p3.y) * p3.z);
        }

        vec2 rotate2(vec2 v, float angle) {
            float c = cos(angle);
            float s = sin(angle);
            return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
        }

        void main() {
            int cloudType = clamp(uCloudType, 0, 3);
            float softness = clamp(uCloudSoftness, 0.2, 0.98);
            float detail = clamp(uCloudDetail, 0.3, 4.5);
            float detailAmount = clamp((detail - 0.3) / 4.2, 0.0, 1.0);
            float motionSpeed = clamp(uCloudMotionSpeed, 0.0, 8.0);
            float t = uTime * (0.06 + motionSpeed * 0.24);

            vec2 uv = vCardUv;
            vec2 stretch = vec2(0.94, 1.06);
            if (cloudType == 1) {
                stretch = vec2(1.20, 0.86);
            } else if (cloudType == 2) {
                stretch = vec2(0.76, 1.32);
            } else if (cloudType == 3) {
                stretch = vec2(1.85, 0.58);
            }

            vec2 uvShaped = uv * stretch;
            float radius = length(uvShaped);
            float core = exp(-radius * radius * 1.40);

            // Lumpy cloud silhouette made from overlapping soft lobes.
            float lobeA = exp(-length((uv - vec2(-0.34, 0.02)) * vec2(1.18, 0.98)) * 2.9);
            float lobeB = exp(-length((uv - vec2( 0.30, 0.08)) * vec2(1.24, 1.08)) * 3.0);
            float lobeC = exp(-length((uv - vec2( 0.00,-0.24)) * vec2(0.96, 1.30)) * 2.8);
            float lobeD = exp(-length((uv - vec2( 0.02, 0.34)) * vec2(1.08, 1.38)) * 3.2);
            float silhouetteBase = core * 0.70 + (lobeA + lobeB + lobeC + lobeD) * 0.42;

            float driftA = fbm(vec3(uv * 2.45 + vec2(t * 0.36, -t * 0.20), 1.9));
            float driftB = fbmFast(vec3(uv * 4.10 + vec2(-t * 0.52, t * 0.31), 6.3));
            float edgeNoise = mix(driftA, driftB, 0.60);
            if (cloudType == 3) {
                float streak = fbmFast(vec3(uv.x * 7.2 + t * 1.6, uv.y * 1.45 - t * 0.45, 12.3));
                edgeNoise = mix(edgeNoise, streak, 0.78);
            }

            float edgeShape = silhouetteBase + (edgeNoise - 0.5) * mix(0.14, 0.30, detailAmount);
            float edgeStart = mix(0.10, 0.22, softness);
            if (cloudType == 3) {
                edgeShape *= 0.93;
                edgeStart = mix(0.22, 0.34, softness);
            }
            float alphaShape = smoothstep(edgeStart, 0.95, edgeShape);

            float interiorMask = smoothstep(0.06, 0.70, core);
            float puffMask = smoothstep(0.26, 0.88, driftA) * interiorMask;
            float wispyMask = smoothstep(0.40, 0.92, driftB) * interiorMask;
            float detailGain = mix(0.86, 1.22, puffMask) * mix(0.90, 1.18, wispyMask * detailAmount);
            if (cloudType == 3) {
                float filament = smoothstep(0.46, 0.93, edgeNoise);
                detailGain *= mix(0.70, 1.24, filament);
            }

            float density = clamp(alphaShape * detailGain, 0.0, 1.0);

            // Opacity slider should feel direct and obvious.
            float alpha = density * pow(clamp(uCloudOpacity, 0.0, 1.0), 0.78);

            // Fade edge-on card based on geometric facing to reduce visible plane artifacts.
            vec3 faceN = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
            vec3 viewDir = normalize(uCameraWorld - vWorldPos);
            float facing = abs(dot(faceN, viewDir));

            // Hard cull: discard entirely when the card is nearly edge-on to the camera.
            if (facing < 0.12) {
                discard;
            }

            float fadeStrength = clamp(uPlaneFade, 0.0, 1.0);
            float facingMin = mix(0.12, 0.22, fadeStrength);
            float facingMax = mix(0.20, 0.72, fadeStrength);
            float facingFade = smoothstep(facingMin, facingMax, facing);
            alpha *= facingFade;

            if (alpha <= 0.01) {
                discard;
            }

            // Soft daylight lighting for fluffy white cloud look.
            vec3 lightDir = normalize(uLightDir);
            vec3 pseudoNormal = normalize(vec3(uv * 0.82, sqrt(max(1.0 - dot(uv * 0.86, uv * 0.86), 0.001))));
            float ndl = max(dot(pseudoNormal, lightDir), 0.0);

            float lighting = mix(0.92, 1.14, pow(ndl, 0.88));
            vec3 highlightTint = mix(vec3(0.98, 1.00, 1.02), vec3(1.08, 1.10, 1.12), pow(ndl, 0.72));
            vec3 coolShadowTint = vec3(0.90, 0.95, 1.00);
            if (cloudType == 3) {
                lighting = mix(0.94, 1.10, pow(ndl, 0.90));
                highlightTint = mix(vec3(0.97, 1.00, 1.03), vec3(1.04, 1.08, 1.12), pow(ndl, 0.76));
                coolShadowTint = vec3(0.91, 0.96, 1.00);
            }
            vec3 cloudTint = mix(coolShadowTint, highlightTint, smoothstep(0.0, 0.70, ndl));

            float rimGeom = pow(1.0 - abs(dot(pseudoNormal, viewDir)), 2.9);
            float backLit = pow(max(dot(-viewDir, lightDir), 0.0), 1.20);
            float silverLining = rimGeom * backLit;

            float glow = clamp(uGlowStrength, 0.0, 1.0);
            float glowBoost = glow * glow;
            float viewRim = max(1.0 - abs(dot(pseudoNormal, viewDir)), 0.0);

            float bloomRim = pow(viewRim, 1.35);
            float bloomBack = pow(max(dot(-viewDir, lightDir), 0.0), 1.16) * pow(viewRim, 1.05);
            float bloomVolume = smoothstep(0.94, 0.12, radius) * (0.28 + 0.46 * viewRim);

            vec3 color = uCloudColor * cloudTint * lighting;
            color += vec3(1.14, 1.14, 1.12) * silverLining * (0.45 + glowBoost * 1.10);
            color += vec3(1.07, 1.08, 1.08) * pow(core, 1.7) * (0.14 + 0.26 * detailAmount);

            vec3 bloomTint = mix(uCloudColor, vec3(1.12, 1.15, 1.18), 0.70);
            color += bloomTint * bloomRim * glowBoost * 1.10;
            color += vec3(1.18, 1.20, 1.16) * bloomBack * glowBoost * 1.30;
            color += bloomTint * bloomVolume * glowBoost * 0.78;

            // Keep all regions bright and sky-tinted (no dark cloud shadows).
            vec3 lightBlueFloor = vec3(0.86, 0.92, 0.99);
            vec3 ambientSky = lightBlueFloor * (0.36 + 0.28 * interiorMask);
            color += ambientSky;
            color = max(color, lightBlueFloor * 0.58);

            // Keep alpha bounded but slightly lifted so bright zones read as glowing.
            alpha = min(1.0, alpha * (1.0 + glow * 0.18));
            FragColor = vec4(color, alpha);
        }
    )";

    const unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, kVertexShader);
    const unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string infoLog(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(program, length, nullptr, infoLog.data());
        throw std::runtime_error("Cloud program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

// ── Underwater post-process program ─────────────────────────────────────────
// Renders a fullscreen triangle (gl_VertexID trick, no VBO) and applies:
//   wave-distorted UV sampling, chromatic aberration, animated FBM caustics,
//   depth-based colour tint/fog, and a vignette.
unsigned int createUnderwaterProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        out vec2 vUv;
        void main() {
            // Produce a full-screen triangle from 3 gl_VertexID values
            float x = float((gl_VertexID & 1) << 2) - 1.0;
            float y = float((gl_VertexID & 2) << 1) - 1.0;
            vUv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5);
            gl_Position = vec4(x, y, 0.0, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        in vec2 vUv;
        out vec4 fragColor;

        uniform sampler2D uSceneTex;
        uniform float     uTime;
        uniform float     uDepth;       // metres below the water surface (>= 0)
        uniform vec3      uWaterTint;   // configurable base water colour
        uniform vec2      uSunUv;
        uniform float     uSunVisible;
        uniform int       uUnderwaterEnabled;
        uniform int       uCinematicEnabled;

        // ── Noise helpers (caustics) ─────────────────────────────────────────
        float hash21(vec2 p) {
            p = fract(p * vec2(127.1, 311.7));
            p += dot(p, p + 184.75);
            return fract(p.x * p.y);
        }
        float vnoise(vec2 p) {
            vec2 i = floor(p), f = fract(p);
            f = f * f * (3.0 - 2.0 * f);
            return mix(mix(hash21(i),           hash21(i + vec2(1,0)), f.x),
                       mix(hash21(i + vec2(0,1)), hash21(i + vec2(1,1)), f.x), f.y);
        }
        float fbm3Water(vec2 p) {
            return vnoise(p)*0.500
                 + vnoise(p*2.10 + 1.70)*0.250
                 + vnoise(p*4.20 + 3.10)*0.125;
        }
        float godRayMask(vec2 uv, vec2 origin, vec2 dir, float t) {
            vec2 rel = uv - origin;
            float along = dot(rel, dir);
            vec2 perpDir = vec2(-dir.y, dir.x);
            float across = dot(rel, perpDir);
            float beamBands = sin(along * 18.0 - t * 0.90) * 0.5 + 0.5;
            float beamNoise = fbm3Water(vec2(along * 3.2, across * 9.0) + vec2(0.0, t * 0.12));
            float shaft = smoothstep(0.22, 0.02, abs(across))
                        * smoothstep(-0.08, 0.18, along)
                        * smoothstep(1.15, 0.20, along)
                        * smoothstep(0.38, 0.82, beamBands * 0.65 + beamNoise * 0.35);
            return shaft;
        }

        void main() {
            float t     = uTime;
            float depth = max(uDepth, 0.0);

            vec3 scene = texture(uSceneTex, vUv).rgb;
            vec3 outColor = scene;

            if (uUnderwaterEnabled == 1) {

            // ── Wave distortion of the scene (refraction effect) ──────────────
            // Strength scales softly from zero to a maximum of 0.014 as depth grows
            float distStr = clamp(depth * 0.025, 0.0, 0.0045);
            vec2 distort;
            distort.x = sin(vUv.y * 4.5 + t * 0.55) * sin(vUv.x * 3.0 + t * 0.32);
            distort.y = cos(vUv.x * 4.0 + t * 0.48) * cos(vUv.y * 3.2 + t * 0.40);
            distort *= distStr;

            // ── Chromatic aberration (colour channels sample at slight offsets) ─
            float aberr = clamp(depth * 0.0018, 0.0, 0.0030);
            vec2 uv = vUv + distort;
            float sceneR = texture(uSceneTex, uv + vec2( aberr,  0.0)).r;
            float sceneG = texture(uSceneTex, uv).g;
            float sceneB = texture(uSceneTex, uv - vec2( aberr,  0.0)).b;
            scene = vec3(sceneR, sceneG, sceneB);

            // ── Caustics – bright animated light patches from sun above waves ──
            // Fade caustics away as camera descends deeper
            float causticStr = clamp(1.0 - depth * 0.28, 0.0, 1.0);
            vec2 cuv = vUv * 2.80;
            float cA = fbm3Water(cuv + vec2( t * 0.18,  t * 0.12));
            float cB = fbm3Water(cuv + vec2(-t * 0.12,  t * 0.16) + vec2(3.4, 1.2));
            float cC = fbm3Water(cuv * 0.72 + vec2( t * 0.08, -t * 0.09) + vec2(7.1, 5.4));
            float caustic = smoothstep(0.74, 0.96, (cA + cB) * 0.50 + cC * 0.25)
                          * causticStr * 0.30;

            // ── Colour tint – deeper water is colder and darker ───────────────
            float tintAmt = clamp(depth * 0.24, 0.0, 0.82);
            vec3 deepColor = vec3(0.01, 0.07, 0.34);
            // Near surface is nudged slightly toward cyan-blue, then transitions
            // to a deeper ocean blue as depth increases.
            vec3 nearBlue = mix(uWaterTint, vec3(0.08, 0.50, 0.74), 0.42);
            vec3 tint = mix(nearBlue, deepColor, clamp(depth * 0.075, 0.0, 1.0));
            vec3 tinted = mix(scene, tint, tintAmt);

            // Add caustic shimmer
            tinted += vec3(0.17, 0.38, 0.56) * caustic;

            // Directional underwater god rays from the visible sun in screen space.
            vec2 sunToScene = vec2(0.5, 0.10) - uSunUv;
            vec2 sunDir2 = sunToScene / max(length(sunToScene), 0.0001);
            float rayA = godRayMask(vUv, uSunUv, sunDir2, t);
            float rayB = godRayMask(vUv, uSunUv + vec2(0.035, -0.010), normalize(sunDir2 + vec2(0.12, 0.06)), t + 1.7);
            float godRays = (rayA * 0.72 + rayB * 0.46)
                          * clamp(1.0 - depth * 0.22, 0.0, 1.0)
                          * uSunVisible;
            tinted += vec3(0.16, 0.38, 0.62) * godRays;

            // ── Vignette ─────────────────────────────────────────────────────
            vec2 vigUv = vUv * 2.0 - 1.0;
            float vign = 1.0 - clamp(dot(vigUv * 0.58, vigUv * 0.58), 0.0, 1.0);
            vign = mix(0.22, 1.0, vign * vign);
            tinted *= vign;

            // ── Depth fog – exponential darkening/murk as camera goes deeper ─
            float fog = exp(-depth * 0.11);
            tinted = mix(tint * 0.10, tinted, fog);

            // ── Near-surface brightness flicker when barely submerged ─────────
            float surfaceGlow = smoothstep(1.0, 0.0, depth) * 0.10
                              * (0.85 + 0.15 * sin(t * 3.1 + vUv.x * 6.0));
            tinted += vec3(0.42, 0.68, 0.92) * surfaceGlow;

            outColor = tinted;
            }

            if (uCinematicEnabled == 1) {
                float luma = dot(outColor, vec3(0.2126, 0.7152, 0.0722));
                vec3 graded = pow(max(outColor, vec3(0.0)), vec3(0.96));
                graded = mix(vec3(luma), graded, 1.10);
                graded += vec3(0.020, 0.012, -0.006) * (1.0 - luma);
                graded += vec3(-0.005, 0.000, 0.012) * luma;

                float grain = hash21(vUv * vec2(1920.0, 1080.0) + vec2(t * 17.0, t * 23.0)) - 0.5;
                graded += grain * 0.030;

                vec2 c = vUv * 2.0 - 1.0;
                c.x *= 1.10;
                float vignette = smoothstep(1.12, 0.30, length(c));
                graded *= mix(0.72, 1.0, vignette);

                float topBar = 1.0 - smoothstep(0.0, 0.060, vUv.y);
                float bottomBar = smoothstep(0.940, 1.0, vUv.y);
                float barMask = clamp(topBar + bottomBar, 0.0, 1.0);
                graded = mix(graded, vec3(0.0), barMask);

                outColor = max(graded, vec3(0.0));
            }

            fragColor = vec4(outColor, 1.0);
        }
    )";

    const unsigned int vs = compileShader(GL_VERTEX_SHADER,   kVertexShader);
    const unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    const unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    int success = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(length), '\0');
        glGetProgramInfoLog(prog, length, nullptr, log.data());
        throw std::runtime_error("Underwater program link failed: " + log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

}  // namespace

namespace sparks::render {

Renderer::~Renderer() {
    destroyFramebuffer();
    m_weatherRenderer.shutdown();

    if (m_skydomeTexture != 0) {
        glDeleteTextures(1, &m_skydomeTexture);
    }

    if (m_terrainTexture != 0) {
        glDeleteTextures(1, &m_terrainTexture);
    }

    if (m_skydomeEbo != 0) {
        glDeleteBuffers(1, &m_skydomeEbo);
    }
    if (m_skydomeVbo != 0) {
        glDeleteBuffers(1, &m_skydomeVbo);
    }
    if (m_skydomeVao != 0) {
        glDeleteVertexArrays(1, &m_skydomeVao);
    }
    if (m_skydomeProgram != 0) {
        glDeleteProgram(m_skydomeProgram);
    }

    if (m_terrainEbo != 0) {
        glDeleteBuffers(1, &m_terrainEbo);
    }
    if (m_terrainVbo != 0) {
        glDeleteBuffers(1, &m_terrainVbo);
    }
    if (m_terrainVao != 0) {
        glDeleteVertexArrays(1, &m_terrainVao);
    }
    if (m_terrainProgram != 0) {
        glDeleteProgram(m_terrainProgram);
    }

    if (m_waterEbo != 0) {
        glDeleteBuffers(1, &m_waterEbo);
    }
    if (m_waterVbo != 0) {
        glDeleteBuffers(1, &m_waterVbo);
    }
    if (m_waterVao != 0) {
        glDeleteVertexArrays(1, &m_waterVao);
    }
    if (m_waterProgram != 0) {
        glDeleteProgram(m_waterProgram);
    }

    if (m_fullscreenVao != 0) {
        glDeleteVertexArrays(1, &m_fullscreenVao);
    }
    if (m_underwaterProgram != 0) {
        glDeleteProgram(m_underwaterProgram);
    }

    if (m_cloudProgram != 0) {
        glDeleteProgram(m_cloudProgram);
    }
    if (m_cloudSortedEbo != 0) {
        glDeleteBuffers(1, &m_cloudSortedEbo);
    }
    if (m_cloudVao != 0) {
        glDeleteVertexArrays(1, &m_cloudVao);
    }

    if (m_shaderProgram != 0) {
        glDeleteProgram(m_shaderProgram);
    }

    if (m_gridVbo != 0) {
        glDeleteBuffers(1, &m_gridVbo);
    }

    if (m_gridVao != 0) {
        glDeleteVertexArrays(1, &m_gridVao);
    }

    if (m_importTexture != 0) {
        glDeleteTextures(1, &m_importTexture);
    }

    if (m_importEbo != 0) {
        glDeleteBuffers(1, &m_importEbo);
    }

    if (m_importVbo != 0) {
        glDeleteBuffers(1, &m_importVbo);
    }

    if (m_importVao != 0) {
        glDeleteVertexArrays(1, &m_importVao);
    }

    if (m_texturedProgram != 0) {
        glDeleteProgram(m_texturedProgram);
    }
}

void Renderer::initialize() {
    m_shaderProgram = createProgram();
    m_texturedProgram = createTexturedProgram();
    m_skydomeProgram = createSkydomeProgram();
    m_terrainProgram = createTerrainProgram();
    m_waterProgram = createWaterProgram();
    m_cloudProgram = createCloudProgram();
    m_underwaterProgram = createUnderwaterProgram();
    // Fullscreen triangle VAO – no buffers, vertex positions generated from gl_VertexID
    glGenVertexArrays(1, &m_fullscreenVao);
    m_cloudMvpLoc = glGetUniformLocation(m_cloudProgram, "uMvp");
    m_cloudModelLoc = glGetUniformLocation(m_cloudProgram, "uModel");
    m_cloudColorLoc = glGetUniformLocation(m_cloudProgram, "uCloudColor");
    m_cloudOpacityLoc = glGetUniformLocation(m_cloudProgram, "uCloudOpacity");
    m_cloudSoftnessLoc = glGetUniformLocation(m_cloudProgram, "uCloudSoftness");
    m_cloudDetailLoc = glGetUniformLocation(m_cloudProgram, "uCloudDetail");
    m_cloudPlaneFadeLoc = glGetUniformLocation(m_cloudProgram, "uPlaneFade");
    m_cloudSpreadLoc = glGetUniformLocation(m_cloudProgram, "uCubeSpread");
    m_cloudCameraLoc = glGetUniformLocation(m_cloudProgram, "uCameraWorld");
    m_cloudLightDirLoc = glGetUniformLocation(m_cloudProgram, "uLightDir");
    m_cloudGlowLoc = glGetUniformLocation(m_cloudProgram, "uGlowStrength");
    m_cloudTimeLoc = glGetUniformLocation(m_cloudProgram, "uTime");
    m_cloudMotionSpeedLoc = glGetUniformLocation(m_cloudProgram, "uCloudMotionSpeed");
    m_cloudTypeLoc = glGetUniformLocation(m_cloudProgram, "uCloudType");
    m_weatherRenderer.initialize();
    createEnvironmentResources();

    const std::filesystem::path terrainTexturePath = std::filesystem::path("assets") / "textures" / "terrain_diffuse.jpg";
    m_terrainTexture = loadTexture2DFromFile(terrainTexturePath, false);
    m_skydomeTexture = 0;
    m_hasSkydomeTexture = false;
    m_hasTerrainTexture = (m_terrainTexture != 0);

    createGridResources();
    createFramebuffer();
}

void Renderer::setImportedModel(const ImportedModelData& model) {
    if (m_importTexture == 0) {
        glGenTextures(1, &m_importTexture);
    }
    if (m_importVao == 0) {
        glGenVertexArrays(1, &m_importVao);
    }
    if (m_importVbo == 0) {
        glGenBuffers(1, &m_importVbo);
    }
    if (m_importEbo == 0) {
        glGenBuffers(1, &m_importEbo);
    }

    glBindVertexArray(m_importVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_importVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(model.vertices.size() * sizeof(float)),
        model.vertices.data(),
        GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_importEbo);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<long long>(model.indices.size() * sizeof(unsigned int)),
        model.indices.data(),
        GL_STATIC_DRAW);

    constexpr int stride = 8 * static_cast<int>(sizeof(float));
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindTexture(GL_TEXTURE_2D, m_importTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        model.textureWidth,
        model.textureHeight,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        model.textureRgba.data());
    glGenerateMipmap(GL_TEXTURE_2D);

    glBindVertexArray(0);

    m_importIndexCount = static_cast<int>(model.indices.size());
    m_importOpacity = model.opacity;
    m_importAlphaBlend = model.alphaBlend;
    m_importAlphaCutoff = model.alphaCutoff;
    m_importUnlitShading = model.unlitShading;
    m_importDiffuseColor = model.diffuseColor;
    m_importEmissive = model.emissiveColor;
    m_importDimensions = model.dimensions;
    setImportedModelTransform(model.position, model.rotationEulerDegrees, model.scale);
}

void Renderer::setImportedModelTransform(const glm::vec3& position, const glm::vec3& rotationEulerDegrees, const glm::vec3& scale) {
    m_importPosition = position;
    m_importRotationEuler = rotationEulerDegrees;
    m_importScale = scale;
}

void Renderer::setEnvironmentSettings(const EnvironmentSettings& settings) {
    m_environmentSettings = settings;
    m_environmentSettings.skydomeRadius = glm::clamp(m_environmentSettings.skydomeRadius, 20.0f, 4000.0f);
    m_environmentSettings.skyCloudAmount = glm::clamp(m_environmentSettings.skyCloudAmount, 0.0f, 1.5f);
    m_environmentSettings.skyCloudScale = glm::clamp(m_environmentSettings.skyCloudScale, 0.1f, 8.0f);
    m_environmentSettings.sunDiscSize = glm::clamp(m_environmentSettings.sunDiscSize, 0.2f, 8.0f);
    m_environmentSettings.sunIntensity = glm::clamp(m_environmentSettings.sunIntensity, 0.0f, 4.0f);
    m_environmentSettings.waterSunStrength = glm::clamp(m_environmentSettings.waterSunStrength, 0.0f, 6.0f);
    m_environmentSettings.sunHeatStrength = glm::clamp(m_environmentSettings.sunHeatStrength, 0.0f, 3.0f);
    m_environmentSettings.dustAmount = glm::clamp(m_environmentSettings.dustAmount, 0.0f, 1.5f);
    m_environmentSettings.sunRayStrength = glm::clamp(m_environmentSettings.sunRayStrength, 0.0f, 2.5f);
    m_environmentSettings.lensFlareStrength = glm::clamp(m_environmentSettings.lensFlareStrength, 0.0f, 2.5f);
    m_environmentSettings.fogNear = glm::clamp(m_environmentSettings.fogNear, 1.0f, 500.0f);
    m_environmentSettings.fogFar = glm::clamp(m_environmentSettings.fogFar, m_environmentSettings.fogNear + 1.0f, 900.0f);
    m_environmentSettings.fogStrength = glm::clamp(m_environmentSettings.fogStrength, 0.0f, 1.0f);
    m_environmentSettings.terrainSize = glm::clamp(m_environmentSettings.terrainSize, 20.0f, 4000.0f);
    m_environmentSettings.terrainHeight = glm::clamp(m_environmentSettings.terrainHeight, -20.0f, 20.0f);
    m_environmentSettings.terrainPatchScale = glm::clamp(m_environmentSettings.terrainPatchScale, 0.01f, 4.0f);
    m_environmentSettings.terrainRoughness = glm::clamp(m_environmentSettings.terrainRoughness, 0.0f, 3.0f);
    for (auto& cloud : m_environmentSettings.cloudObjects) {
        cloud.scale = glm::max(cloud.scale, glm::vec3(0.05f));
        cloud.opacity = glm::clamp(cloud.opacity, 0.0f, 1.0f);
        cloud.softness = glm::clamp(cloud.softness, 0.2f, 0.98f);
        cloud.detail = glm::clamp(cloud.detail, 0.3f, 4.5f);
        cloud.planeFade = glm::clamp(cloud.planeFade, 0.0f, 1.0f);
        cloud.planeCount = std::clamp(cloud.planeCount, 1, 28);
        cloud.cubeSpread = glm::clamp(cloud.cubeSpread, 0.35f, 2.50f);
        cloud.motionSpeed = glm::clamp(cloud.motionSpeed, 0.0f, 8.0f);
        cloud.cloudType = std::clamp(cloud.cloudType, 0, 2);
        if (cloud.planeSelectionSeed == 0u) {
            cloud.planeSelectionSeed = 1u;
        }
    }
    if (glm::length(m_environmentSettings.terrainLightDirection) < 0.001f) {
        m_environmentSettings.terrainLightDirection = glm::vec3(0.30f, 0.72f, -0.46f);
    }
}

void Renderer::setViewportSize(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (width == m_viewportWidth && height == m_viewportHeight) {
        return;
    }

    m_viewportWidth = width;
    m_viewportHeight = height;
    rebuildFramebufferIfNeeded(width, height);
}

void Renderer::setWeatherRain(
    const std::vector<glm::vec3>& lineVertices,
    const std::vector<glm::vec4>& splashPoints,
    const std::vector<glm::vec4>& dropletPoints,
    const std::vector<glm::vec4>& ripplePoints,
    const int rainConcept,
    const bool useCustomVisualProfile,
    const glm::vec3& rainTint,
    const glm::vec3& splashTint,
    const glm::vec3& dropletTint,
    const glm::vec3& rippleTint,
    const float rainStyleBoost,
    const float particleStyleBoost,
    const float intensity,
    const bool enabled,
    const float rainLineWidth,
    const float splashPointSize,
    const float dropletPointSize,
    const float ripplePointSize,
    const float rainOpacityScale,
    const float splashOpacityScale,
    const float dropletOpacityScale,
    const float rippleOpacityScale) {
    m_rainConcept = std::clamp(rainConcept, 0, 5);
    m_useCustomVisualProfile = useCustomVisualProfile;
    m_rainTint = glm::clamp(rainTint, glm::vec3(0.0f), glm::vec3(2.0f));
    m_splashTint = glm::clamp(splashTint, glm::vec3(0.0f), glm::vec3(2.0f));
    m_dropletTint = glm::clamp(dropletTint, glm::vec3(0.0f), glm::vec3(2.0f));
    m_rippleTint = glm::clamp(rippleTint, glm::vec3(0.0f), glm::vec3(2.0f));
    m_rainStyleBoost = glm::clamp(rainStyleBoost, 0.40f, 2.50f);
    m_particleStyleBoost = glm::clamp(particleStyleBoost, 0.40f, 2.50f);
    m_rainIntensity = glm::clamp(intensity, 0.0f, 1.0f);
    m_weatherEnabled = enabled;
    m_rainLineWidth = glm::clamp(rainLineWidth, 0.5f, 4.0f);
    m_splashPointSize = glm::clamp(splashPointSize, 1.0f, 20.0f);
    m_dropletPointSize = glm::clamp(dropletPointSize, 1.0f, 14.0f);
    m_ripplePointSize = glm::clamp(ripplePointSize, 2.0f, 48.0f);
    m_rainOpacityScale = glm::clamp(rainOpacityScale, 0.0f, 2.5f);
    m_splashOpacityScale = glm::clamp(splashOpacityScale, 0.0f, 2.5f);
    m_dropletOpacityScale = glm::clamp(dropletOpacityScale, 0.0f, 2.5f);
    m_rippleOpacityScale = glm::clamp(rippleOpacityScale, 0.0f, 2.5f);
    m_weatherRenderer.updateRainGeometry(lineVertices);
    m_weatherRenderer.updateSplashGeometry(splashPoints);
    m_weatherRenderer.updateDropletGeometry(dropletPoints);
    m_weatherRenderer.updateRippleGeometry(ripplePoints);
}

void Renderer::render(const ViewControls& viewControls) {
    static const auto sRenderStart = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - sRenderStart).count();

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_viewportWidth, m_viewportHeight);
    glEnable(GL_DEPTH_TEST);

    glClearColor(0.28f, 0.40f, 0.58f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_shaderProgram);

    const float aspectRatio = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
    const glm::vec3 cameraTarget(viewControls.panOffset.x, viewControls.panOffset.y, viewControls.orbitTargetZ);
    const glm::vec3 cameraPos(cameraTarget.x, cameraTarget.y, cameraTarget.z + viewControls.zoomDistance);
    const glm::mat4 view = glm::lookAt(
        cameraPos,
        cameraTarget,
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, kCameraFarPlane);

    glm::mat4 world(1.0f);
    // Orbit around the active camera target instead of always rotating around world origin.
    world = glm::translate(world, cameraTarget);
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    world = glm::translate(world, -cameraTarget);

    if (m_environmentSettings.enableSkydome && m_skydomeProgram != 0 && m_skydomeVao != 0 && m_skydomeIndexCount > 0) {
        glUseProgram(m_skydomeProgram);

        glm::mat4 skydomeModel(1.0f);
        skydomeModel = glm::scale(skydomeModel, glm::vec3(m_environmentSettings.skydomeRadius));
        const glm::mat4 skyMvp = projection * view * world * skydomeModel;

        const int skyMvpLoc = glGetUniformLocation(m_skydomeProgram, "uMvp");
        const int skyHorizonLoc = glGetUniformLocation(m_skydomeProgram, "uHorizonColor");
        const int skyZenithLoc = glGetUniformLocation(m_skydomeProgram, "uZenithColor");
        const int skyCloudColorLoc = glGetUniformLocation(m_skydomeProgram, "uCloudColor");
        const int skyCloudAmountLoc = glGetUniformLocation(m_skydomeProgram, "uCloudAmount");
        const int skyCloudScaleLoc = glGetUniformLocation(m_skydomeProgram, "uCloudScale");
        const int skySunDirLoc = glGetUniformLocation(m_skydomeProgram, "uSunDir");
        const int skySunDiscSizeLoc = glGetUniformLocation(m_skydomeProgram, "uSunDiscSize");
        const int skySunIntensityLoc = glGetUniformLocation(m_skydomeProgram, "uSunIntensity");
        const int skySunColorLoc = glGetUniformLocation(m_skydomeProgram, "uSunColor");
        const int skySunHeatLoc = glGetUniformLocation(m_skydomeProgram, "uSunHeatStrength");
        const int skyDustAmountLoc = glGetUniformLocation(m_skydomeProgram, "uDustAmount");
        const int skyDustColorLoc = glGetUniformLocation(m_skydomeProgram, "uDustColor");
        const int skySunRayStrengthLoc = glGetUniformLocation(m_skydomeProgram, "uSunRayStrength");
        const int skyLensFlareStrengthLoc = glGetUniformLocation(m_skydomeProgram, "uLensFlareStrength");
        const int skyTimeLoc = glGetUniformLocation(m_skydomeProgram, "uTime");
        const int skyTexLoc = glGetUniformLocation(m_skydomeProgram, "uSkyTex");
        const int skyUseTexLoc = glGetUniformLocation(m_skydomeProgram, "uUseTexture");
        glUniformMatrix4fv(skyMvpLoc, 1, GL_FALSE, glm::value_ptr(skyMvp));
        glUniform3f(skyHorizonLoc, m_environmentSettings.skyHorizonColor.r, m_environmentSettings.skyHorizonColor.g, m_environmentSettings.skyHorizonColor.b);
        glUniform3f(skyZenithLoc, m_environmentSettings.skyZenithColor.r, m_environmentSettings.skyZenithColor.g, m_environmentSettings.skyZenithColor.b);
        glUniform3f(skyCloudColorLoc, m_environmentSettings.skyCloudColor.r, m_environmentSettings.skyCloudColor.g, m_environmentSettings.skyCloudColor.b);
        glUniform1f(skyCloudAmountLoc, m_environmentSettings.enableCloudObjects ? m_environmentSettings.skyCloudAmount : m_environmentSettings.skyCloudAmount * 0.68f);
        glUniform1f(skyCloudScaleLoc, m_environmentSettings.skyCloudScale);
        glUniform3f(skySunDirLoc, m_environmentSettings.terrainLightDirection.x, m_environmentSettings.terrainLightDirection.y, m_environmentSettings.terrainLightDirection.z);
        glUniform1f(skySunDiscSizeLoc, m_environmentSettings.sunDiscSize);
        glUniform1f(skySunIntensityLoc, m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f);
        glUniform3f(skySunColorLoc, m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
        glUniform1f(skySunHeatLoc, m_environmentSettings.sunHeatStrength);
        glUniform1f(skyDustAmountLoc, m_environmentSettings.dustAmount);
        glUniform3f(skyDustColorLoc, m_environmentSettings.dustColor.r, m_environmentSettings.dustColor.g, m_environmentSettings.dustColor.b);
        glUniform1f(skySunRayStrengthLoc, m_environmentSettings.sunRayStrength);
        glUniform1f(skyLensFlareStrengthLoc, m_environmentSettings.lensFlareStrength);
        glUniform1f(skyTimeLoc, elapsedSeconds);
        glUniform1i(skyTexLoc, 0);
        glUniform1i(skyUseTexLoc, m_hasSkydomeTexture ? 1 : 0);

        if (m_hasSkydomeTexture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_skydomeTexture);
        }

        glDepthMask(GL_FALSE);
        glBindVertexArray(m_skydomeVao);
        glDrawElements(GL_TRIANGLES, m_skydomeIndexCount, GL_UNSIGNED_INT, nullptr);
        glDepthMask(GL_TRUE);
    }

    if (m_environmentSettings.enableTerrain && m_terrainProgram != 0 && m_terrainVao != 0 && m_terrainIndexCount > 0) {
        glUseProgram(m_terrainProgram);

        glm::mat4 terrainModel(1.0f);
        terrainModel = glm::translate(terrainModel, glm::vec3(0.0f, m_environmentSettings.terrainHeight, 0.0f));
        terrainModel = glm::scale(terrainModel, glm::vec3(m_environmentSettings.terrainSize / 220.0f, 1.0f, m_environmentSettings.terrainSize / 220.0f));
        const glm::mat4 terrainWorldModel = world * terrainModel;
        const glm::mat4 terrainMvp = projection * view * terrainWorldModel;

        const int terrainMvpLoc = glGetUniformLocation(m_terrainProgram, "uMvp");
        const int terrainModelLoc = glGetUniformLocation(m_terrainProgram, "uModel");
        const int terrainBaseALoc = glGetUniformLocation(m_terrainProgram, "uBaseA");
        const int terrainBaseBLoc = glGetUniformLocation(m_terrainProgram, "uBaseB");
        const int terrainPatchScaleLoc = glGetUniformLocation(m_terrainProgram, "uPatchScale");
        const int terrainRoughnessLoc = glGetUniformLocation(m_terrainProgram, "uRoughness");
        const int terrainLightDirLoc = glGetUniformLocation(m_terrainProgram, "uLightDir");
        const int terrainTexLoc = glGetUniformLocation(m_terrainProgram, "uTerrainTex");
        const int terrainUseTexLoc = glGetUniformLocation(m_terrainProgram, "uUseTexture");
        const int terrainShadowEnabledLoc = glGetUniformLocation(m_terrainProgram, "uShadowEnabled");
        const int terrainShadowCenterLoc = glGetUniformLocation(m_terrainProgram, "uShadowCenterXZ");
        const int terrainShadowDirLoc = glGetUniformLocation(m_terrainProgram, "uShadowDirXZ");
        const int terrainShadowMajorRadiusLoc = glGetUniformLocation(m_terrainProgram, "uShadowMajorRadius");
        const int terrainShadowMinorRadiusLoc = glGetUniformLocation(m_terrainProgram, "uShadowMinorRadius");
        const int terrainShadowStrengthLoc = glGetUniformLocation(m_terrainProgram, "uShadowStrength");
        const int terrainShadowSoftnessLoc = glGetUniformLocation(m_terrainProgram, "uShadowSoftness");
        const int terrainCameraPosLoc = glGetUniformLocation(m_terrainProgram, "uCameraPos");
        const int terrainFogColorLoc = glGetUniformLocation(m_terrainProgram, "uFogColor");
        const int terrainFogNearLoc = glGetUniformLocation(m_terrainProgram, "uFogNear");
        const int terrainFogFarLoc = glGetUniformLocation(m_terrainProgram, "uFogFar");
        const int terrainFogStrengthLoc = glGetUniformLocation(m_terrainProgram, "uFogStrength");
        glUniformMatrix4fv(terrainMvpLoc, 1, GL_FALSE, glm::value_ptr(terrainMvp));
        glUniformMatrix4fv(terrainModelLoc, 1, GL_FALSE, glm::value_ptr(terrainWorldModel));
        glUniform3f(terrainBaseALoc, m_environmentSettings.terrainColorA.r, m_environmentSettings.terrainColorA.g, m_environmentSettings.terrainColorA.b);
        glUniform3f(terrainBaseBLoc, m_environmentSettings.terrainColorB.r, m_environmentSettings.terrainColorB.g, m_environmentSettings.terrainColorB.b);
        glUniform1f(terrainPatchScaleLoc, m_environmentSettings.terrainPatchScale);
        glUniform1f(terrainRoughnessLoc, m_environmentSettings.terrainRoughness);
        glUniform3f(terrainLightDirLoc, m_environmentSettings.terrainLightDirection.r, m_environmentSettings.terrainLightDirection.g, m_environmentSettings.terrainLightDirection.b);
        glUniform1i(terrainTexLoc, 1);
        glUniform1i(terrainUseTexLoc, m_hasTerrainTexture ? 1 : 0);
        glUniform3f(terrainCameraPosLoc, cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform3f(terrainFogColorLoc, m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
        glUniform1f(terrainFogNearLoc, m_environmentSettings.fogNear);
        glUniform1f(terrainFogFarLoc, m_environmentSettings.fogFar);
        glUniform1f(terrainFogStrengthLoc, m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);

        const bool hasImportShadow = (m_importVao != 0 && m_importIndexCount > 0);
        if (hasImportShadow) {
            const glm::vec3 scaledDimensions = glm::abs(m_importDimensions * m_importScale);
            const float heightFactor = glm::clamp(scaledDimensions.y * 0.10f, 0.0f, 1.4f);
            glm::vec3 lightN = glm::normalize(m_environmentSettings.terrainLightDirection);
            if (glm::length(lightN) < 0.001f) {
                lightN = glm::vec3(0.30f, 0.72f, -0.46f);
            }
            glm::vec2 shadowDir = glm::normalize(glm::vec2(-lightN.x, -lightN.z));
            if (glm::length(shadowDir) < 0.001f) {
                shadowDir = glm::vec2(0.0f, 1.0f);
            }
            const glm::vec2 shadowPerp(-shadowDir.y, shadowDir.x);
            const float yawRad = glm::radians(m_importRotationEuler.y);
            const glm::vec2 axisX(std::cos(yawRad), std::sin(yawRad));
            const glm::vec2 axisZ(-std::sin(yawRad), std::cos(yawRad));
            const float halfX = scaledDimensions.x * 0.5f;
            const float halfY = scaledDimensions.y * 0.5f;
            const float halfZ = scaledDimensions.z * 0.5f;

            // Project all 8 OBB corners onto the ground plane (y=0) along the sun ray direction.
            // Shadow displacement per unit of world-Y: shift_along_shadowDir = y * |lightXZ| / lightY
            const float sunXZLen = glm::length(glm::vec2(lightN.x, lightN.z));
            const float invLightY = 1.0f / std::max(lightN.y, 0.06f);
            const float shiftPerY = sunXZLen * invLightY; // world-space shift along shadowDir per unit height

            float shadowAlongMin =  1e9f, shadowAlongMax = -1e9f;
            float shadowPerpMin  =  1e9f, shadowPerpMax  = -1e9f;
            for (int sx : {-1, 1}) {
                for (int sy : {-1, 1}) {
                    for (int sz : {-1, 1}) {
                        glm::vec2 cornerXZ = glm::vec2(m_importPosition.x, m_importPosition.z)
                            + axisX * (sx * halfX)
                            + axisZ * (sz * halfZ);
                        // Clamp cornerY to 0 — underground parts don't cast shadow
                        const float cornerY = std::max(0.0f, m_importPosition.y + sy * halfY);
                        // Project onto ground: shadow moves along shadowDir by cornerY * shiftPerY
                        const float projAlong = glm::dot(cornerXZ, shadowDir) + cornerY * shiftPerY;
                        const float projPerp  = glm::dot(cornerXZ, shadowPerp);
                        shadowAlongMin = std::min(shadowAlongMin, projAlong);
                        shadowAlongMax = std::max(shadowAlongMax, projAlong);
                        shadowPerpMin  = std::min(shadowPerpMin,  projPerp);
                        shadowPerpMax  = std::max(shadowPerpMax,  projPerp);
                    }
                }
            }

            const float centerAlong = (shadowAlongMin + shadowAlongMax) * 0.5f;
            const float centerPerp  = (shadowPerpMin  + shadowPerpMax)  * 0.5f;
            const glm::vec2 centerXZ = shadowDir * centerAlong + shadowPerp * centerPerp;
            const float majorRadius  = (shadowAlongMax - shadowAlongMin) * 0.5f;
            const float minorRadius  = (shadowPerpMax  - shadowPerpMin)  * 0.5f;
            const float shadowStrength = glm::clamp(0.34f + heightFactor * 0.25f, 0.22f, 0.62f);

            glUniform1i(terrainShadowEnabledLoc, 1);
            glUniform2f(terrainShadowCenterLoc, centerXZ.x, centerXZ.y);
            glUniform2f(terrainShadowDirLoc, shadowDir.x, shadowDir.y);
            glUniform1f(terrainShadowMajorRadiusLoc, std::max(majorRadius, 0.2f));
            glUniform1f(terrainShadowMinorRadiusLoc, std::max(minorRadius, 0.1f));
            glUniform1f(terrainShadowStrengthLoc, shadowStrength);
            glUniform1f(terrainShadowSoftnessLoc, m_environmentSettings.fbxShadowSoftness);
        } else {
            glUniform1i(terrainShadowEnabledLoc, 0);
            glUniform2f(terrainShadowCenterLoc, 0.0f, 0.0f);
            glUniform2f(terrainShadowDirLoc, 0.0f, 1.0f);
            glUniform1f(terrainShadowMajorRadiusLoc, 1.0f);
            glUniform1f(terrainShadowMinorRadiusLoc, 0.7f);
            glUniform1f(terrainShadowStrengthLoc, 0.0f);
            glUniform1f(terrainShadowSoftnessLoc, m_environmentSettings.fbxShadowSoftness);
        }

        if (m_hasTerrainTexture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_terrainTexture);
        }

        glBindVertexArray(m_terrainVao);
        glDrawElements(GL_TRIANGLES, m_terrainIndexCount, GL_UNSIGNED_INT, nullptr);
    }

    // Water plane rendering
    if (m_environmentSettings.enableWater && m_waterProgram != 0 && m_waterVao != 0 && m_waterIndexCount > 0) {
        glUseProgram(m_waterProgram);

        // The model matrix only translates to waterLevel; waves are displaced in the vertex shader.
        glm::mat4 waterModel(1.0f);
        waterModel = glm::translate(waterModel, glm::vec3(0.0f, m_environmentSettings.waterLevel, 0.0f));
        const glm::mat4 waterWorldModel = world * waterModel;
        const glm::mat4 waterMvp = projection * view * waterWorldModel;

        glUniformMatrix4fv(glGetUniformLocation(m_waterProgram, "uMvp"),         1, GL_FALSE, glm::value_ptr(waterMvp));
        glUniform1f(glGetUniformLocation(m_waterProgram, "uTime"),              static_cast<float>(glfwGetTime()));
        glUniform1f(glGetUniformLocation(m_waterProgram, "uWaveAmplitude"),     m_environmentSettings.waveAmplitude);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uWaveFrequency"),     m_environmentSettings.waveFrequency);
        glUniform3f(glGetUniformLocation(m_waterProgram, "uWaterColor"),        m_environmentSettings.waterColor.r, m_environmentSettings.waterColor.g, m_environmentSettings.waterColor.b);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uWaterOpacity"),      m_environmentSettings.waterOpacity);
        glUniform3f(glGetUniformLocation(m_waterProgram, "uCameraPos"),         cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform3f(glGetUniformLocation(m_waterProgram, "uLightDir"),          m_environmentSettings.terrainLightDirection.x, m_environmentSettings.terrainLightDirection.y, m_environmentSettings.terrainLightDirection.z);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uSunIntensity"),      m_environmentSettings.enableSun ? (m_environmentSettings.sunIntensity * m_environmentSettings.waterSunStrength) : 0.0f);
        glUniform3f(glGetUniformLocation(m_waterProgram, "uSunColor"),          m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
        glUniform3f(glGetUniformLocation(m_waterProgram, "uFogColor"),          m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uFogNear"),           m_environmentSettings.fogNear);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uFogFar"),            m_environmentSettings.fogFar);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uFogStrength"),       m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);

        float interactionAmount = 0.0f;
        float interactionRadius = 0.0f;
        glm::vec2 interactionCenter(0.0f);
        if (m_importIndexCount > 0) {
            const glm::vec3 scaledDimensions = glm::abs(m_importDimensions * m_importScale);
            const float halfHeight = scaledDimensions.y * 0.5f;
            const float waterSurface = sampleOceanWaveHeight(m_environmentSettings, glm::vec2(m_importPosition.x, m_importPosition.z), static_cast<float>(glfwGetTime()));
            const float objectBottom = m_importPosition.y - halfHeight;
            const float objectTop = m_importPosition.y + halfHeight;
            if (objectBottom <= waterSurface + 0.08f && objectTop >= waterSurface - 0.10f) {
                interactionCenter = glm::vec2(m_importPosition.x, m_importPosition.z);
                interactionRadius = std::max(0.45f, std::max(scaledDimensions.x, scaledDimensions.z) * 0.38f);
                const float immersion = glm::clamp((waterSurface - objectBottom) / std::max(scaledDimensions.y, 0.001f), 0.0f, 1.0f);
                interactionAmount = glm::smoothstep(0.02f, 0.30f, immersion);
            }
        }
        glUniform2f(glGetUniformLocation(m_waterProgram, "uInteractionCenter"), interactionCenter.x, interactionCenter.y);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uInteractionRadius"), interactionRadius);
        glUniform1f(glGetUniformLocation(m_waterProgram, "uInteractionAmount"), interactionAmount);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glBindVertexArray(m_waterVao);
        glDrawElements(GL_TRIANGLES, m_waterIndexCount, GL_UNSIGNED_INT, nullptr);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    // Switch back to main shader for grid rendering
    glUseProgram(m_shaderProgram);

    const int mvpLoc = glGetUniformLocation(m_shaderProgram, "uMvp");
    const int modelLoc = glGetUniformLocation(m_shaderProgram, "uModel");
    const int colorLoc = glGetUniformLocation(m_shaderProgram, "uColor");
    const int alphaLoc = glGetUniformLocation(m_shaderProgram, "uAlpha");
    const int gradientLoc = glGetUniformLocation(m_shaderProgram, "uGradientStrength");
    const int lightPosLoc = glGetUniformLocation(m_shaderProgram, "uLightPos");

    const glm::vec3 lightPos(5.5f, 6.5f, 4.0f);
    glUniform3f(lightPosLoc, lightPos.x, lightPos.y, lightPos.z);

    const glm::mat4 gridMvp = projection * view * world;
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(gridMvp));
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(world));
    glBindVertexArray(m_gridVao);
    glLineWidth(1.0f);
    glUniform1f(alphaLoc, 1.0f);
    glUniform1f(gradientLoc, 0.0f);
    glUniform3f(colorLoc, 0.24f, 0.24f, 0.26f);
    glDrawArrays(GL_LINES, 0, m_gridRegularCount);
    glUniform3f(colorLoc, 0.53f, 0.11f, 0.11f);
    glDrawArrays(GL_LINES, m_gridAxisXStart, 2);
    glUniform3f(colorLoc, 0.11f, 0.43f, 0.11f);
    glDrawArrays(GL_LINES, m_gridAxisYStart, 2);

    if (m_importVao != 0 && m_importTexture != 0 && m_importIndexCount > 0) {
        glUseProgram(m_texturedProgram);

        const int mvpLocT = glGetUniformLocation(m_texturedProgram, "uMvp");
        const int modelLocT = glGetUniformLocation(m_texturedProgram, "uModel");
        const int lightPosLocT = glGetUniformLocation(m_texturedProgram, "uLightPos");
        const int texLocT = glGetUniformLocation(m_texturedProgram, "uTex");
        const int opacityLocT = glGetUniformLocation(m_texturedProgram, "uOpacity");
        const int alphaCutoffLocT = glGetUniformLocation(m_texturedProgram, "uAlphaCutoff");
        const int unlitShadingLocT = glGetUniformLocation(m_texturedProgram, "uUnlitShading");
        const int shadowPassLocT = glGetUniformLocation(m_texturedProgram, "uShadowPass");
        const int diffuseColorLocT = glGetUniformLocation(m_texturedProgram, "uDiffuseColor");
        const int emissiveLocT = glGetUniformLocation(m_texturedProgram, "uEmissive");
        const int sunColorLocT = glGetUniformLocation(m_texturedProgram, "uSunColor");
        const int sunGlowStrengthLocT = glGetUniformLocation(m_texturedProgram, "uSunGlowStrength");
        const int waterEnabledLocT = glGetUniformLocation(m_texturedProgram, "uWaterEnabled");
        const int waterLevelLocT = glGetUniformLocation(m_texturedProgram, "uWaterLevel");
        const int waterTintLocT = glGetUniformLocation(m_texturedProgram, "uWaterTint");
        const int cameraPosLocT = glGetUniformLocation(m_texturedProgram, "uCameraPos");
        const int fogColorLocT = glGetUniformLocation(m_texturedProgram, "uFogColor");
        const int fogNearLocT = glGetUniformLocation(m_texturedProgram, "uFogNear");
        const int fogFarLocT = glGetUniformLocation(m_texturedProgram, "uFogFar");
        const int fogStrengthLocT = glGetUniformLocation(m_texturedProgram, "uFogStrength");

        glm::mat4 model(1.0f);
        model = glm::translate(model, m_importPosition);
        const glm::vec3 rotRad = glm::radians(m_importRotationEuler);
        model = glm::rotate(model, rotRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, m_importScale);

        const glm::mat4 worldModel = world * model;
        const glm::mat4 mvp = projection * view * worldModel;

        glUniformMatrix4fv(mvpLocT, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(modelLocT, 1, GL_FALSE, glm::value_ptr(worldModel));
        glUniform3f(lightPosLocT, 5.5f, 6.5f, 4.0f);
        glUniform1i(texLocT, 0);
        glUniform1f(opacityLocT, m_importOpacity);
        glUniform1f(alphaCutoffLocT, m_importAlphaCutoff);
        glUniform1i(unlitShadingLocT, m_importUnlitShading ? 1 : 0);
        glUniform1i(shadowPassLocT, 0);
        glUniform4f(diffuseColorLocT, m_importDiffuseColor.r, m_importDiffuseColor.g, m_importDiffuseColor.b, m_importDiffuseColor.a);
        glUniform3f(emissiveLocT, m_importEmissive.r, m_importEmissive.g, m_importEmissive.b);
        glUniform3f(sunColorLocT, m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
        glUniform1f(sunGlowStrengthLocT, (m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f) * m_environmentSettings.objectSunGlowStrength);
        glUniform1i(waterEnabledLocT, m_environmentSettings.enableWater ? 1 : 0);
        glUniform1f(waterLevelLocT, m_environmentSettings.waterLevel);
        glUniform3f(waterTintLocT, 0.10f, 0.42f, 0.52f);
        glUniform3f(cameraPosLocT, cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform3f(fogColorLocT, m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
        glUniform1f(fogNearLocT, m_environmentSettings.fogNear);
        glUniform1f(fogFarLocT, m_environmentSettings.fogFar);
        glUniform1f(fogStrengthLocT, m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);

        if (m_importAlphaBlend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_importTexture);
        glBindVertexArray(m_importVao);
        glDrawElements(GL_TRIANGLES, m_importIndexCount, GL_UNSIGNED_INT, nullptr);

        if (m_importAlphaBlend) {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

    }

    if (m_environmentSettings.enableCloudObjects && !m_environmentSettings.cloudObjects.empty() && m_cloudProgram != 0 && m_cloudVao != 0 && m_cloudIndexCount > 0) {
        glUseProgram(m_cloudProgram);

        glUniform3f(m_cloudCameraLoc, cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform1f(m_cloudTimeLoc, elapsedSeconds);
        glUniform3f(
            m_cloudLightDirLoc,
            m_environmentSettings.terrainLightDirection.x,
            m_environmentSettings.terrainLightDirection.y,
            m_environmentSettings.terrainLightDirection.z);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        glBindVertexArray(m_cloudVao);
        std::vector<int> cloudDrawOrder;
        cloudDrawOrder.reserve(m_environmentSettings.cloudObjects.size());
        for (int cloudIndex = 0; cloudIndex < static_cast<int>(m_environmentSettings.cloudObjects.size()); ++cloudIndex) {
            if (m_environmentSettings.cloudObjects[cloudIndex].enabled) {
                cloudDrawOrder.push_back(cloudIndex);
            }
        }

        std::sort(cloudDrawOrder.begin(), cloudDrawOrder.end(), [&](const int lhs, const int rhs) {
            const glm::vec3 lhsCenterWorld = glm::vec3(world * glm::vec4(m_environmentSettings.cloudObjects[lhs].position, 1.0f));
            const glm::vec3 rhsCenterWorld = glm::vec3(world * glm::vec4(m_environmentSettings.cloudObjects[rhs].position, 1.0f));
            const float lhsDistSq = glm::dot(lhsCenterWorld - cameraPos, lhsCenterWorld - cameraPos);
            const float rhsDistSq = glm::dot(rhsCenterWorld - cameraPos, rhsCenterWorld - cameraPos);
            return lhsDistSq > rhsDistSq;
        });

        for (const int cloudIndex : cloudDrawOrder) {
            const auto& cloud = m_environmentSettings.cloudObjects[cloudIndex];
            if (!cloud.enabled) {
                continue;
            }

            glm::mat4 cloudModel(1.0f);
            cloudModel = glm::translate(cloudModel, cloud.position);
            // Always orient the cloud plane toward world origin.
            glm::vec3 forward = -cloud.position;
            if (glm::dot(forward, forward) < 0.000001f) {
                forward = glm::vec3(0.0f, 0.0f, -1.0f);
            } else {
                forward = glm::normalize(forward);
            }
            glm::vec3 upRef(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(forward, upRef)) > 0.98f) {
                upRef = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            const glm::vec3 right = glm::normalize(glm::cross(upRef, forward));
            const glm::vec3 up = glm::normalize(glm::cross(forward, right));
            glm::mat4 lookAtOriginRotation(1.0f);
            lookAtOriginRotation[0] = glm::vec4(right, 0.0f);
            lookAtOriginRotation[1] = glm::vec4(up, 0.0f);
            lookAtOriginRotation[2] = glm::vec4(forward, 0.0f);
            cloudModel *= lookAtOriginRotation;
            cloudModel = glm::scale(cloudModel, cloud.scale);
            const glm::mat4 cloudWorldModel = world * cloudModel;
            const glm::mat4 cloudMvp = projection * view * cloudWorldModel;

            glUniformMatrix4fv(m_cloudMvpLoc, 1, GL_FALSE, glm::value_ptr(cloudMvp));
            glUniformMatrix4fv(m_cloudModelLoc, 1, GL_FALSE, glm::value_ptr(cloudWorldModel));
            glUniform3f(m_cloudColorLoc, cloud.color.r, cloud.color.g, cloud.color.b);
            glUniform1f(m_cloudOpacityLoc, cloud.opacity);
            glUniform1f(m_cloudSoftnessLoc, cloud.softness);
            glUniform1f(m_cloudDetailLoc, cloud.detail);
            glUniform1f(m_cloudPlaneFadeLoc, cloud.planeFade);
            glUniform1f(m_cloudSpreadLoc, cloud.cubeSpread);
            glUniform1f(m_cloudGlowLoc, cloud.glowStrength);
            glUniform1f(m_cloudMotionSpeedLoc, cloud.motionSpeed);
            glUniform1i(m_cloudTypeLoc, cloud.cloudType);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    m_weatherRenderer.renderRain(
        projection,
        view,
        world,
        m_rainConcept,
        m_useCustomVisualProfile,
        m_rainTint,
        m_rainStyleBoost,
        m_rainIntensity,
        m_weatherEnabled,
        m_rainLineWidth,
        m_rainOpacityScale);
    m_weatherRenderer.renderSplashes(
        projection,
        view,
        world,
        m_rainConcept,
        m_useCustomVisualProfile,
        m_splashTint,
        m_particleStyleBoost,
        m_rainIntensity,
        m_weatherEnabled,
        m_splashPointSize,
        m_splashOpacityScale);
    m_weatherRenderer.renderDroplets(
        projection,
        view,
        world,
        m_rainConcept,
        m_useCustomVisualProfile,
        m_dropletTint,
        m_particleStyleBoost,
        m_rainIntensity,
        m_weatherEnabled,
        m_dropletPointSize,
        m_dropletOpacityScale);
    m_weatherRenderer.renderRipples(
        projection,
        view,
        world,
        m_rainConcept,
        m_useCustomVisualProfile,
        m_rippleTint,
        m_particleStyleBoost,
        m_rainIntensity,
        m_weatherEnabled,
        m_ripplePointSize,
        m_rippleOpacityScale);

    // Reset GL line width after weather rendering
    glLineWidth(1.0f);

    glBindVertexArray(0);

    // ── Underwater post-process pass ─────────────────────────────────────────
    // Detect whether the effective camera position in scene-local space is
    // below the flat water plane. The renderer orbits by rotating the world,
    // so comparing against the pre-rotation camera position produces the wrong
    // result once the scene is tilted.
    m_cameraUnderwater = false;
    m_usePostProcessed = false;
    if (m_underwaterProgram != 0 && m_postFbo != 0 && m_postColorTexture != 0 && m_fullscreenVao != 0) {
        const bool wantCinematic = m_environmentSettings.enableCinematic;
        bool enableUnderwater = false;
        float underwaterDepth = 0.0f;

        if (m_environmentSettings.enableWater) {
            const glm::mat4 inverseWorld = glm::inverse(world);
            const glm::vec3 sceneLocalCameraPos = glm::vec3(inverseWorld * glm::vec4(cameraPos, 1.0f));
            underwaterDepth = m_environmentSettings.waterLevel - sceneLocalCameraPos.y;
            if (underwaterDepth > 0.01f) {
                enableUnderwater = true;
                m_cameraUnderwater = true;
            }
        }

        if (enableUnderwater || wantCinematic) {
            m_usePostProcessed = true;

            glm::vec2 sunUv(0.5f, -0.2f);
            float sunVisible = 0.0f;
            if (enableUnderwater) {
                const glm::vec3 lightDirLocal = glm::normalize(m_environmentSettings.terrainLightDirection);
                const float skyRadius = std::max(m_environmentSettings.skydomeRadius, 1.0f);
                const glm::vec3 sunLocal = lightDirLocal * (skyRadius * 0.92f);
                const glm::vec4 sunClip = projection * view * world * glm::vec4(sunLocal, 1.0f);
                if (sunClip.w > 0.0001f) {
                    const glm::vec3 sunNdc = glm::vec3(sunClip) / sunClip.w;
                    sunUv = glm::vec2(sunNdc.x * 0.5f + 0.5f, sunNdc.y * 0.5f + 0.5f);
                    const float xVis = 1.0f - glm::clamp((std::abs(sunNdc.x) - 1.0f) * 2.2f, 0.0f, 1.0f);
                    const float yVis = 1.0f - glm::clamp((std::abs(sunNdc.y) - 1.0f) * 2.2f, 0.0f, 1.0f);
                    const float zVis = (sunNdc.z >= -1.0f && sunNdc.z <= 1.0f) ? 1.0f : 0.0f;
                    sunVisible = xVis * yVis * zVis * (m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f);
                }
            }

            glBindFramebuffer(GL_FRAMEBUFFER, m_postFbo);
            glViewport(0, 0, m_viewportWidth, m_viewportHeight);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);

            glUseProgram(m_underwaterProgram);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_colorTexture);
            glUniform1i(glGetUniformLocation(m_underwaterProgram, "uSceneTex"), 0);
            glUniform1f(glGetUniformLocation(m_underwaterProgram, "uTime"), static_cast<float>(glfwGetTime()));
            glUniform1f(glGetUniformLocation(m_underwaterProgram, "uDepth"), enableUnderwater ? underwaterDepth : 0.0f);
            glUniform3f(glGetUniformLocation(m_underwaterProgram, "uWaterTint"),
                m_environmentSettings.waterColor.r,
                m_environmentSettings.waterColor.g,
                m_environmentSettings.waterColor.b);
            glUniform2f(glGetUniformLocation(m_underwaterProgram, "uSunUv"), sunUv.x, sunUv.y);
            glUniform1f(glGetUniformLocation(m_underwaterProgram, "uSunVisible"), sunVisible);
            glUniform1i(glGetUniformLocation(m_underwaterProgram, "uUnderwaterEnabled"), enableUnderwater ? 1 : 0);
            glUniform1i(glGetUniformLocation(m_underwaterProgram, "uCinematicEnabled"), wantCinematic ? 1 : 0);

            glBindVertexArray(m_fullscreenVao);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glBindVertexArray(0);

            glEnable(GL_DEPTH_TEST);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::createGridResources() {
    constexpr int halfGrid = 20;
    constexpr float spacing = 1.0f;
    constexpr float y = -0.75f;

    std::vector<float> vertices;
    vertices.reserve(halfGrid * 2 * 12 + 12);

    // Regular grid lines: skip the center axis lines, drawn separately with colors.
    for (int i = -halfGrid; i <= halfGrid; ++i) {
        if (i == 0) {
            continue;
        }

        const float p = static_cast<float>(i) * spacing;

        vertices.push_back(static_cast<float>(-halfGrid) * spacing);
        vertices.push_back(y);
        vertices.push_back(p);
        vertices.push_back(static_cast<float>(halfGrid) * spacing);
        vertices.push_back(y);
        vertices.push_back(p);

        vertices.push_back(p);
        vertices.push_back(y);
        vertices.push_back(static_cast<float>(-halfGrid) * spacing);
        vertices.push_back(p);
        vertices.push_back(y);
        vertices.push_back(static_cast<float>(halfGrid) * spacing);
    }

    m_gridRegularCount = static_cast<int>(vertices.size() / 3);

    m_gridAxisXStart = m_gridRegularCount;
    vertices.push_back(static_cast<float>(-halfGrid) * spacing);
    vertices.push_back(y);
    vertices.push_back(0.0f);
    vertices.push_back(static_cast<float>(halfGrid) * spacing);
    vertices.push_back(y);
    vertices.push_back(0.0f);

    m_gridAxisYStart = m_gridRegularCount + 2;
    vertices.push_back(0.0f);
    vertices.push_back(y);
    vertices.push_back(static_cast<float>(-halfGrid) * spacing);
    vertices.push_back(0.0f);
    vertices.push_back(y);
    vertices.push_back(static_cast<float>(halfGrid) * spacing);

    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);

    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Renderer::createEnvironmentResources() {
    // Skydome sphere geometry.
    constexpr int lonSegments = 48;
    constexpr int latSegments = 24;
    std::vector<float> skyVertices;
    std::vector<unsigned int> skyIndices;
    skyVertices.reserve(static_cast<std::size_t>((lonSegments + 1) * (latSegments + 1)) * 3);
    skyIndices.reserve(static_cast<std::size_t>(lonSegments * latSegments) * 6);

    for (int y = 0; y <= latSegments; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(latSegments);
        const float theta = v * 3.1415926535f;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);

        for (int x = 0; x <= lonSegments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(lonSegments);
            const float phi = u * 6.283185307f;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            skyVertices.push_back(cosPhi * sinTheta);
            skyVertices.push_back(cosTheta);
            skyVertices.push_back(sinPhi * sinTheta);
        }
    }

    for (int y = 0; y < latSegments; ++y) {
        for (int x = 0; x < lonSegments; ++x) {
            const int rowA = y * (lonSegments + 1);
            const int rowB = (y + 1) * (lonSegments + 1);

            skyIndices.push_back(static_cast<unsigned int>(rowA + x));
            skyIndices.push_back(static_cast<unsigned int>(rowB + x));
            skyIndices.push_back(static_cast<unsigned int>(rowA + x + 1));

            skyIndices.push_back(static_cast<unsigned int>(rowA + x + 1));
            skyIndices.push_back(static_cast<unsigned int>(rowB + x));
            skyIndices.push_back(static_cast<unsigned int>(rowB + x + 1));
        }
    }

    glGenVertexArrays(1, &m_skydomeVao);
    glGenBuffers(1, &m_skydomeVbo);
    glGenBuffers(1, &m_skydomeEbo);
    glBindVertexArray(m_skydomeVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_skydomeVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(skyVertices.size() * sizeof(float)), skyVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_skydomeEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(skyIndices.size() * sizeof(unsigned int)), skyIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);
    m_skydomeIndexCount = static_cast<int>(skyIndices.size());

    // Cloud geometry: single rectangular card centered at origin.
    std::vector<float> cloudVertices;
    std::vector<unsigned int> cloudIndices;
    cloudVertices.reserve(4 * 6);
    cloudIndices.reserve(6);

    const glm::vec3 points[4] = {
        glm::vec3(-0.72f, -0.48f, 0.0f),
        glm::vec3( 0.72f, -0.48f, 0.0f),
        glm::vec3( 0.72f,  0.48f, 0.0f),
        glm::vec3(-0.72f,  0.48f, 0.0f),
    };
    const glm::vec2 uvs[4] = {
        glm::vec2(-1.0f, -1.0f),
        glm::vec2( 1.0f, -1.0f),
        glm::vec2( 1.0f,  1.0f),
        glm::vec2(-1.0f,  1.0f),
    };
    for (int i = 0; i < 4; ++i) {
        cloudVertices.push_back(points[i].x);
        cloudVertices.push_back(points[i].y);
        cloudVertices.push_back(points[i].z);
        cloudVertices.push_back(uvs[i].x);
        cloudVertices.push_back(uvs[i].y);
        cloudVertices.push_back(0.0f);
    }
    cloudIndices = {0u, 1u, 2u, 2u, 3u, 0u};

    glGenVertexArrays(1, &m_cloudVao);
    glGenBuffers(1, &m_cloudVbo);
    glGenBuffers(1, &m_cloudSortedEbo);
    glBindVertexArray(m_cloudVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cloudVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(cloudVertices.size() * sizeof(float)), cloudVertices.data(), GL_STATIC_DRAW);
    // Static index buffer for a single cloud plane.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cloudSortedEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<long long>(cloudIndices.size() * sizeof(unsigned int)),
        cloudIndices.data(),
        GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), reinterpret_cast<void*>(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    m_cloudIndexCount = static_cast<int>(cloudIndices.size());

    // Subdivided terrain grid so vertex displacement produces actual height variation.
    {
        constexpr int kTerrainN = 160;
        constexpr float kTerrainSize = 220.0f;
        const float terrainStep = kTerrainSize * 2.0f / static_cast<float>(kTerrainN);
        std::vector<float> terrainVertices;
        std::vector<unsigned int> terrainIndices;
        terrainVertices.reserve(static_cast<std::size_t>((kTerrainN + 1) * (kTerrainN + 1)) * 3);
        terrainIndices.reserve(static_cast<std::size_t>(kTerrainN * kTerrainN) * 6);

        for (int row = 0; row <= kTerrainN; ++row) {
            for (int col = 0; col <= kTerrainN; ++col) {
                terrainVertices.push_back(-kTerrainSize + static_cast<float>(col) * terrainStep);
                terrainVertices.push_back(0.0f);
                terrainVertices.push_back(-kTerrainSize + static_cast<float>(row) * terrainStep);
            }
        }

        for (int row = 0; row < kTerrainN; ++row) {
            for (int col = 0; col < kTerrainN; ++col) {
                const unsigned int base = static_cast<unsigned int>(row * (kTerrainN + 1) + col);
                terrainIndices.push_back(base);
                terrainIndices.push_back(base + 1u);
                terrainIndices.push_back(base + static_cast<unsigned int>(kTerrainN + 1));

                terrainIndices.push_back(base + 1u);
                terrainIndices.push_back(base + static_cast<unsigned int>(kTerrainN + 2));
                terrainIndices.push_back(base + static_cast<unsigned int>(kTerrainN + 1));
            }
        }

        glGenVertexArrays(1, &m_terrainVao);
        glGenBuffers(1, &m_terrainVbo);
        glGenBuffers(1, &m_terrainEbo);
        glBindVertexArray(m_terrainVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_terrainVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(terrainVertices.size() * sizeof(float)), terrainVertices.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrainEbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(terrainIndices.size() * sizeof(unsigned int)), terrainIndices.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
        glEnableVertexAttribArray(0);
        m_terrainIndexCount = static_cast<int>(terrainIndices.size());
    }

    // Water plane – 128×128 subdivided grid for Gerstner wave geometry
    {
        constexpr int   kWaterN    = 128;
        constexpr float kWaterSize = 240.0f;
        constexpr float kStep      = kWaterSize * 2.0f / static_cast<float>(kWaterN);

        std::vector<float>        wVerts;
        std::vector<unsigned int> wIdx;
        wVerts.reserve(static_cast<std::size_t>((kWaterN + 1) * (kWaterN + 1)) * 3);
        wIdx.reserve(static_cast<std::size_t>(kWaterN * kWaterN) * 6);

        for (int row = 0; row <= kWaterN; ++row) {
            for (int col = 0; col <= kWaterN; ++col) {
                wVerts.push_back(-kWaterSize + static_cast<float>(col) * kStep);
                wVerts.push_back(0.0f);
                wVerts.push_back(-kWaterSize + static_cast<float>(row) * kStep);
            }
        }
        for (int row = 0; row < kWaterN; ++row) {
            for (int col = 0; col < kWaterN; ++col) {
                const unsigned int base = static_cast<unsigned int>(row * (kWaterN + 1) + col);
                wIdx.push_back(base);
                wIdx.push_back(base + 1u);
                wIdx.push_back(base + static_cast<unsigned int>(kWaterN + 2));
                wIdx.push_back(base);
                wIdx.push_back(base + static_cast<unsigned int>(kWaterN + 2));
                wIdx.push_back(base + static_cast<unsigned int>(kWaterN + 1));
            }
        }

        glGenVertexArrays(1, &m_waterVao);
        glGenBuffers(1, &m_waterVbo);
        glGenBuffers(1, &m_waterEbo);
        glBindVertexArray(m_waterVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_waterVbo);
        glBufferData(GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(wVerts.size() * sizeof(float)),
            wVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_waterEbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(wIdx.size() * sizeof(unsigned int)),
            wIdx.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
        glEnableVertexAttribArray(0);
        m_waterIndexCount = static_cast<int>(wIdx.size());
    }

    glBindVertexArray(0);
}

void Renderer::createFramebuffer() {
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        m_viewportWidth,
        m_viewportHeight,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    glGenRenderbuffers(1, &m_depthStencilRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_viewportWidth, m_viewportHeight);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        m_depthStencilRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Framebuffer is not complete.");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Post-process FBO (underwater effect ping-pong target) ──────────────
    glGenFramebuffers(1, &m_postFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_postFbo);

    glGenTextures(1, &m_postColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_postColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_viewportWidth, m_viewportHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_postColorTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Post-process framebuffer is not complete.");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroyFramebuffer() {
    if (m_depthStencilRbo != 0) {
        glDeleteRenderbuffers(1, &m_depthStencilRbo);
        m_depthStencilRbo = 0;
    }

    if (m_colorTexture != 0) {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }

    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    if (m_postColorTexture != 0) {
        glDeleteTextures(1, &m_postColorTexture);
        m_postColorTexture = 0;
    }

    if (m_postFbo != 0) {
        glDeleteFramebuffers(1, &m_postFbo);
        m_postFbo = 0;
    }
}

void Renderer::rebuildFramebufferIfNeeded(const int width, const int height) {
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);

    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    // Resize post-process colour texture to match
    glBindTexture(GL_TEXTURE_2D, m_postColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

}  // namespace sparks::render
