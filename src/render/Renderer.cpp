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
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stb_image.h>

namespace {

constexpr float kCameraFarPlane = 1000.0f;

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
                FragColor = vec4(base.rgb + uEmissive, alpha);
                return;
            }

            float ndl = max(dot(n, l), 0.0);
            float lit = mix(0.35, 1.25, pow(ndl, 0.9));
            FragColor = vec4(base.rgb * lit + uEmissive, alpha);
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
        uniform sampler2D uSkyTex;
        uniform int uUseTexture;

        void main() {
            float h = clamp(vLocalPos.y * 0.5 + 0.5, 0.0, 1.0);
            float cloudNoise = sin(vLocalPos.x * 6.0 * uCloudScale) * cos(vLocalPos.z * 7.0 * uCloudScale);
            float cloud = smoothstep(0.35, 0.85, cloudNoise * 0.5 + 0.5) * (1.0 - h) * uCloudAmount;
            vec3 color = mix(uHorizonColor, uZenithColor, pow(h, 0.62));
            color = mix(color, uCloudColor, cloud);

            if (uUseTexture == 1) {
                vec3 dir = normalize(vLocalPos);
                float u = atan(dir.z, dir.x) * 0.159154943 + 0.5;
                float v = acos(clamp(dir.y, -1.0, 1.0)) * 0.318309886;
                // Use only the upper hemisphere of the texture so the dome contains sky only.
                float vSky = clamp(v * 0.5, 0.0, 0.5);
                vec3 texColor = texture(uSkyTex, vec2(u, vSky)).rgb;
                color = mix(color, texColor, 0.88);
            }

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
        out vec3 vWorldPos;
        out vec3 vLocalPos;

        void main() {
            vec4 worldPos = uModel * vec4(aPos, 1.0);
            vWorldPos = worldPos.xyz;
            vLocalPos = aPos;
            gl_Position = uMvp * vec4(aPos, 1.0);
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

        void main() {
            float patchNoise = sin(vWorldPos.x * uPatchScale * 0.92) * cos(vWorldPos.z * uPatchScale * 1.08);
            float t = clamp(patchNoise * 0.5 + 0.5, 0.0, 1.0);
            vec3 color = mix(uBaseA, uBaseB, t);

            if (uUseTexture == 1) {
                vec2 uv = vLocalPos.xz * 0.035;
                vec3 texColor = texture(uTerrainTex, uv).rgb;
                color *= mix(vec3(1.0), texColor, 0.70);
            }

            float ndl = max(dot(vec3(0.0, 1.0, 0.0), normalize(uLightDir)), 0.0);
            color *= mix(0.68, 1.12 + uRoughness * 0.15, ndl);

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
            for (int i = 0; i < 5; ++i) {
                sum += valueNoise3(p) * amp;
                p = p * 2.03 + vec3(17.17, 9.43, 5.79);
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
            float softness = clamp(uCloudSoftness, 0.2, 0.98);
            float detail = clamp(uCloudDetail, 0.3, 4.5);
            float detailAmount = clamp((detail - 0.3) / 4.2, 0.0, 1.0);
            float motionSpeed = clamp(uCloudMotionSpeed, 0.0, 8.0);
            float motionGain = motionSpeed * (0.65 + motionSpeed * 0.60);

            // Non-spherical anisotropic core shape.
            vec3 q = vec3(vLocalPos.x * 0.72, vLocalPos.y * 1.72, vLocalPos.z * 0.84);
            float radial = length(q);

            // Make softness highly responsive: low softness = hard puff, high softness = airy edge.
            float edgeStart = mix(0.32, 0.86, softness);
            float baseShape = 1.0 - smoothstep(edgeStart, 1.0, radial);

            // Randomize each card's local mask so repeated card structure is harder to perceive.
            float cardNoise = hash12(vec2(vCardId * 1.37, vCardId * 2.11));
            float cardAngle = (cardNoise - 0.5) * 1.2;
            vec2 cardUv = rotate2(vCardUv, cardAngle);
            vec2 cardScale = mix(vec2(0.82, 1.18), vec2(1.18, 0.84), cardNoise);

            // Feather card edges smoothly to avoid a visible circular rim.
            float uvRadius = length(cardUv * cardScale);
            float cardFeatherStart = mix(0.66, 0.74, cardNoise);
            float cardFeatherEnd = mix(1.02, 1.10, cardNoise);
            float cardEdgeFade = 1.0 - smoothstep(cardFeatherStart, cardFeatherEnd, uvRadius);
            cardEdgeFade = pow(max(cardEdgeFade, 0.0), 1.35);

            float verticalFade = 1.0 - smoothstep(0.55, 1.0, abs(vLocalPos.y));
            float t = uTime * 0.17 * motionGain;

            // Soft radial blob profile similar to the reference image.
            float sigma = mix(0.62, 0.36, softness);
            float gaussian = exp(-((uvRadius * uvRadius) / max(2.0 * sigma * sigma, 0.02)));
            float radialBlob = smoothstep(0.02, 0.92, gaussian);

            // Keep detail inside the card center so it never reaches borders.
            float detailInteriorMask = 1.0 - smoothstep(0.18, 0.68, uvRadius);
            vec3 windDrift = vec3(t * 14.0, sin(t * 0.9 + vCardId * 0.61) * 1.3, t * 7.2);
            vec3 domain = (vWorldPos + windDrift) * (0.05 + detail * 0.08);
            float detailNoiseA = fbm(domain + vec3(8.3, 4.7, 2.1));
            float detailNoiseB = fbm(domain.zxy * 1.63 + vec3(13.7 - t * 4.3, 2.8 + t * 1.5, 5.1 + t * 2.2));
            float detailNoise = clamp(mix(detailNoiseA, detailNoiseB, 0.45), 0.0, 1.0);
            float detailPuff = smoothstep(0.20, 0.88, detailNoise);
            float detailContrast = mix(1.0, 1.95, detailAmount);
            float detailBand = clamp((detailPuff - 0.5) * detailContrast + 0.5, 0.0, 1.0);
            float detailMod = mix(1.0, mix(0.60, 1.50, detailBand), detailAmount * detailInteriorMask);

            // Two advection layers so the motion reads clearly even at medium speed.
            float advect = fbm(vec3(cardUv * 3.0 + vec2(t * 0.95, -t * 0.42), vCardId * 0.19));
            float advectFast = fbm(vec3(cardUv * 6.2 + vec2(-t * 1.8, t * 1.2), 3.1 + vCardId * 0.41));
            float advectionMask = clamp(
                smoothstep(0.16, 0.86, advect) * 0.62 +
                smoothstep(0.20, 0.84, advectFast) * 0.55,
                0.0,
                1.0);
            detailMod *= mix(0.72, 1.34, advectionMask * detailAmount);

            float motionPulse = mix(0.92, 1.20, smoothstep(0.24, 0.88, advectFast));
            detailMod *= mix(1.0, motionPulse, 0.58 * detailAmount);

            float density = clamp(baseShape * cardEdgeFade * radialBlob * detailMod * mix(0.55, 1.0, verticalFade), 0.0, 1.0);

            // Opacity slider should feel direct and obvious.
            float alpha = density * pow(clamp(uCloudOpacity, 0.0, 1.0), 0.78);

            // Fade edge-on cards based on geometric facing to reduce visible plane artifacts.
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

            // Warm cumulus lighting: soft highlights and pocket shadows.
            vec3 lightDir = normalize(uLightDir);
            vec3 pseudoNormal = normalize(vec3(cardUv * 0.92, sqrt(max(1.0 - dot(cardUv, cardUv), 0.001))));
            float ndl = max(dot(pseudoNormal, lightDir), 0.0);

            float cavity = (1.0 - detailPuff) * detailInteriorMask;
            float lighting = mix(0.62, 1.18, pow(ndl, 0.84));
            lighting *= mix(1.0, 0.82, cavity * 0.75);

            vec3 warmTint = mix(vec3(0.90, 0.94, 0.98), vec3(1.05, 1.08, 1.10), pow(ndl, 0.70));
            float rimGeom = pow(1.0 - abs(dot(pseudoNormal, viewDir)), 3.2);
            float backLit = pow(max(dot(-viewDir, lightDir), 0.0), 1.25);
            float silverLining = rimGeom * backLit;

            float glow = clamp(uGlowStrength, 0.0, 1.0);
            float glowBoost = glow * glow;
            float viewRim = max(1.0 - abs(dot(pseudoNormal, viewDir)), 0.0);

            // Soft bloom components tuned for bright daylight cumulus.
            float bloomRim = pow(viewRim, 1.45);
            float bloomBack = pow(max(dot(-viewDir, lightDir), 0.0), 1.2) * pow(viewRim, 1.15);
            float bloomVolume = smoothstep(0.94, 0.12, uvRadius) * (0.28 + 0.52 * viewRim);

            vec3 color = uCloudColor * warmTint * lighting;
            color += vec3(1.12, 1.12, 1.10) * silverLining * (0.52 + glowBoost * 1.25);

            vec3 bloomTint = mix(uCloudColor, vec3(1.12, 1.15, 1.18), 0.70);
            color += bloomTint * bloomRim * glowBoost * 1.25;
            color += vec3(1.18, 1.20, 1.16) * bloomBack * glowBoost * 1.55;
            color += bloomTint * bloomVolume * glowBoost * 0.95;

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

    if (m_cloudProgram != 0) {
        glDeleteProgram(m_cloudProgram);
    }
    if (m_cloudEbo != 0) {
        glDeleteBuffers(1, &m_cloudEbo);
    }
    if (m_cloudVbo != 0) {
        glDeleteBuffers(1, &m_cloudVbo);
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
    m_cloudProgram = createCloudProgram();
    m_weatherRenderer.initialize();
    createEnvironmentResources();

    const std::filesystem::path skyTexturePath = std::filesystem::path("assets") / "textures" / "sky_hdri.hdr";
    const std::filesystem::path terrainTexturePath = std::filesystem::path("assets") / "textures" / "terrain_diffuse.jpg";
    m_skydomeTexture = loadTexture2DFromFile(skyTexturePath, true);
    m_terrainTexture = loadTexture2DFromFile(terrainTexturePath, false);
    m_hasSkydomeTexture = (m_skydomeTexture != 0);
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
    m_environmentSettings.skydomePitchDegrees = glm::clamp(m_environmentSettings.skydomePitchDegrees, -180.0f, 180.0f);
    m_environmentSettings.skydomeYawDegrees = glm::clamp(m_environmentSettings.skydomeYawDegrees, -180.0f, 180.0f);
    m_environmentSettings.skyCloudAmount = glm::clamp(m_environmentSettings.skyCloudAmount, 0.0f, 1.0f);
    m_environmentSettings.skyCloudScale = glm::clamp(m_environmentSettings.skyCloudScale, 0.1f, 6.0f);
    m_environmentSettings.terrainSize = glm::clamp(m_environmentSettings.terrainSize, 20.0f, 4000.0f);
    m_environmentSettings.terrainHeight = glm::clamp(m_environmentSettings.terrainHeight, -20.0f, 20.0f);
    m_environmentSettings.terrainPatchScale = glm::clamp(m_environmentSettings.terrainPatchScale, 0.01f, 4.0f);
    m_environmentSettings.terrainRoughness = glm::clamp(m_environmentSettings.terrainRoughness, 0.0f, 3.0f);
    for (auto& cloud : m_environmentSettings.cloudObjects) {
        cloud.scale = glm::max(cloud.scale, glm::vec3(0.05f));
        cloud.opacity = glm::clamp(cloud.opacity, 0.0f, 1.0f);
        cloud.softness = glm::clamp(cloud.softness, 0.2f, 0.98f);
        cloud.detail = glm::clamp(cloud.detail, 0.3f, 3.0f);
        cloud.planeFade = glm::clamp(cloud.planeFade, 0.0f, 1.0f);
        cloud.planeCount = std::clamp(cloud.planeCount, 1, 28);
        cloud.cubeSpread = glm::clamp(cloud.cubeSpread, 0.35f, 2.50f);
    }
    if (glm::length(m_environmentSettings.terrainLightDirection) < 0.001f) {
        m_environmentSettings.terrainLightDirection = glm::vec3(0.35f, 1.0f, 0.24f);
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

    glClearColor(0.10f, 0.11f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_shaderProgram);

    const float aspectRatio = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
    const glm::vec3 cameraTarget(viewControls.panOffset.x, viewControls.panOffset.y, 0.0f);
    const glm::vec3 cameraPos(cameraTarget.x, cameraTarget.y, viewControls.zoomDistance);
    const glm::mat4 view = glm::lookAt(
        cameraPos,
        cameraTarget,
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, kCameraFarPlane);

    glm::mat4 world(1.0f);
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));

    if (m_environmentSettings.enableSkydome && m_skydomeProgram != 0 && m_skydomeVao != 0 && m_skydomeIndexCount > 0) {
        glUseProgram(m_skydomeProgram);

        glm::mat4 skydomeModel(1.0f);
        skydomeModel = glm::rotate(skydomeModel, glm::radians(m_environmentSettings.skydomePitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
        skydomeModel = glm::rotate(skydomeModel, glm::radians(m_environmentSettings.skydomeYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        skydomeModel = glm::scale(skydomeModel, glm::vec3(m_environmentSettings.skydomeRadius));
        const glm::mat4 skyMvp = projection * view * world * skydomeModel;

        const int skyMvpLoc = glGetUniformLocation(m_skydomeProgram, "uMvp");
        const int skyHorizonLoc = glGetUniformLocation(m_skydomeProgram, "uHorizonColor");
        const int skyZenithLoc = glGetUniformLocation(m_skydomeProgram, "uZenithColor");
        const int skyCloudLoc = glGetUniformLocation(m_skydomeProgram, "uCloudColor");
        const int skyCloudAmountLoc = glGetUniformLocation(m_skydomeProgram, "uCloudAmount");
        const int skyCloudScaleLoc = glGetUniformLocation(m_skydomeProgram, "uCloudScale");
        const int skyTexLoc = glGetUniformLocation(m_skydomeProgram, "uSkyTex");
        const int skyUseTexLoc = glGetUniformLocation(m_skydomeProgram, "uUseTexture");
        glUniformMatrix4fv(skyMvpLoc, 1, GL_FALSE, glm::value_ptr(skyMvp));
        glUniform3f(skyHorizonLoc, m_environmentSettings.skyHorizonColor.r, m_environmentSettings.skyHorizonColor.g, m_environmentSettings.skyHorizonColor.b);
        glUniform3f(skyZenithLoc, m_environmentSettings.skyZenithColor.r, m_environmentSettings.skyZenithColor.g, m_environmentSettings.skyZenithColor.b);
        glUniform3f(skyCloudLoc, m_environmentSettings.skyCloudColor.r, m_environmentSettings.skyCloudColor.g, m_environmentSettings.skyCloudColor.b);
        glUniform1f(skyCloudAmountLoc, m_environmentSettings.skyCloudAmount);
        glUniform1f(skyCloudScaleLoc, m_environmentSettings.skyCloudScale);
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
        terrainModel = glm::translate(terrainModel, glm::vec3(0.0f, m_environmentSettings.terrainHeight + 0.76f, 0.0f));
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
        glUniformMatrix4fv(terrainMvpLoc, 1, GL_FALSE, glm::value_ptr(terrainMvp));
        glUniformMatrix4fv(terrainModelLoc, 1, GL_FALSE, glm::value_ptr(terrainWorldModel));
        glUniform3f(terrainBaseALoc, m_environmentSettings.terrainColorA.r, m_environmentSettings.terrainColorA.g, m_environmentSettings.terrainColorA.b);
        glUniform3f(terrainBaseBLoc, m_environmentSettings.terrainColorB.r, m_environmentSettings.terrainColorB.g, m_environmentSettings.terrainColorB.b);
        glUniform1f(terrainPatchScaleLoc, m_environmentSettings.terrainPatchScale);
        glUniform1f(terrainRoughnessLoc, m_environmentSettings.terrainRoughness);
        glUniform3f(terrainLightDirLoc, m_environmentSettings.terrainLightDirection.r, m_environmentSettings.terrainLightDirection.g, m_environmentSettings.terrainLightDirection.b);
        glUniform1i(terrainTexLoc, 1);
        glUniform1i(terrainUseTexLoc, m_hasTerrainTexture ? 1 : 0);

        if (m_hasTerrainTexture) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_terrainTexture);
        }

        glBindVertexArray(m_terrainVao);
        glDrawElements(GL_TRIANGLES, m_terrainIndexCount, GL_UNSIGNED_INT, nullptr);
    }

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
    glUniform1f(alphaLoc, 1.0f);
    glUniform1f(gradientLoc, 0.0f);
    glUniform3f(colorLoc, 0.24f, 0.24f, 0.26f);
    glDrawArrays(GL_LINES, 0, m_gridRegularCount);
    glUniform3f(colorLoc, 0.53f, 0.11f, 0.11f);
    glDrawArrays(GL_LINES, m_gridAxisXStart, 2);
    glUniform3f(colorLoc, 0.11f, 0.43f, 0.11f);
    glDrawArrays(GL_LINES, m_gridAxisYStart, 2);

    // Planar projected shadows onto the grid plane.
    constexpr float kShadowPlaneY = -0.749f;
    const glm::vec4 shadowPlane(0.0f, 1.0f, 0.0f, -kShadowPlaneY);
    const glm::vec3 lightDir3 = glm::normalize(glm::vec3(-0.65f, 1.0f, -0.45f));
    const glm::vec4 lightDir(lightDir3, 0.0f);

    const float dot = shadowPlane.x * lightDir.x + shadowPlane.y * lightDir.y + shadowPlane.z * lightDir.z + shadowPlane.w * lightDir.w;
    glm::mat4 shadowProj(0.0f);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const float identity = (row == col) ? dot : 0.0f;
            shadowProj[col][row] = identity - lightDir[row] * shadowPlane[col];
        }
    }

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

        glm::mat4 model(1.0f);
        model = glm::translate(model, m_importPosition);
        const glm::vec3 rotRad = glm::radians(m_importRotationEuler);
        model = glm::rotate(model, rotRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, m_importScale);

        const glm::mat4 worldModel = world * model;
        const glm::mat4 mvp = projection * view * worldModel;

        // Draw projected FBX shadow onto the grid first.
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        const glm::mat4 shadowWorldModel = world * (shadowProj * model);
        const glm::mat4 shadowMvp = projection * view * shadowWorldModel;
        glUniformMatrix4fv(mvpLocT, 1, GL_FALSE, glm::value_ptr(shadowMvp));
        glUniformMatrix4fv(modelLocT, 1, GL_FALSE, glm::value_ptr(shadowWorldModel));
        glUniform3f(lightPosLocT, 5.5f, 6.5f, 4.0f);
        glUniform1i(texLocT, 0);
        glUniform1f(opacityLocT, 0.0f);
        glUniform1f(alphaCutoffLocT, 1.0f);
        glUniform1i(unlitShadingLocT, 0);
        glUniform1i(shadowPassLocT, 1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(m_importVao);
        glDrawElements(GL_TRIANGLES, m_importIndexCount, GL_UNSIGNED_INT, nullptr);

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);

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

        const int cloudMvpLoc = glGetUniformLocation(m_cloudProgram, "uMvp");
        const int cloudModelLoc = glGetUniformLocation(m_cloudProgram, "uModel");
        const int cloudColorLoc = glGetUniformLocation(m_cloudProgram, "uCloudColor");
        const int cloudOpacityLoc = glGetUniformLocation(m_cloudProgram, "uCloudOpacity");
        const int cloudSoftnessLoc = glGetUniformLocation(m_cloudProgram, "uCloudSoftness");
        const int cloudDetailLoc = glGetUniformLocation(m_cloudProgram, "uCloudDetail");
        const int cloudPlaneFadeLoc = glGetUniformLocation(m_cloudProgram, "uPlaneFade");
        const int cloudSpreadLoc = glGetUniformLocation(m_cloudProgram, "uCubeSpread");
        const int cloudCameraLoc = glGetUniformLocation(m_cloudProgram, "uCameraWorld");
        const int cloudLightDirLoc = glGetUniformLocation(m_cloudProgram, "uLightDir");
        const int cloudGlowLoc = glGetUniformLocation(m_cloudProgram, "uGlowStrength");
        const int cloudTimeLoc = glGetUniformLocation(m_cloudProgram, "uTime");
        const int cloudMotionSpeedLoc = glGetUniformLocation(m_cloudProgram, "uCloudMotionSpeed");

        glUniform3f(cloudCameraLoc, cameraPos.x, cameraPos.y, cameraPos.z);
        glUniform1f(cloudTimeLoc, elapsedSeconds);
        glUniform3f(
            cloudLightDirLoc,
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
            const glm::vec3 cloudRotRad = glm::radians(cloud.rotationEulerDegrees);
            cloudModel = glm::rotate(cloudModel, cloudRotRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
            cloudModel = glm::rotate(cloudModel, cloudRotRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
            cloudModel = glm::rotate(cloudModel, cloudRotRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
            cloudModel = glm::scale(cloudModel, cloud.scale);
            const glm::mat4 cloudWorldModel = world * cloudModel;
            const glm::mat4 cloudMvp = projection * view * cloudWorldModel;

            glUniformMatrix4fv(cloudMvpLoc, 1, GL_FALSE, glm::value_ptr(cloudMvp));
            glUniformMatrix4fv(cloudModelLoc, 1, GL_FALSE, glm::value_ptr(cloudWorldModel));
            glUniform3f(cloudColorLoc, cloud.color.r, cloud.color.g, cloud.color.b);
            glUniform1f(cloudOpacityLoc, cloud.opacity);
            glUniform1f(cloudSoftnessLoc, cloud.softness);
            glUniform1f(cloudDetailLoc, cloud.detail);
            glUniform1f(cloudPlaneFadeLoc, cloud.planeFade);
            glUniform1f(cloudSpreadLoc, cloud.cubeSpread);
            glUniform1f(cloudGlowLoc, cloud.glowStrength);
            glUniform1f(cloudMotionSpeedLoc, cloud.motionSpeed);

            const int visiblePlaneCount = std::min(cloud.planeCount, static_cast<int>(m_cloudCardCenters.size()));
            std::vector<int> drawOrder(static_cast<std::size_t>(visiblePlaneCount));
            for (int cardIndex = 0; cardIndex < visiblePlaneCount; ++cardIndex) {
                drawOrder[cardIndex] = cardIndex;
            }

            std::sort(drawOrder.begin(), drawOrder.end(), [&](const int lhs, const int rhs) {
                const glm::vec3 lhsWorld = glm::vec3(cloudWorldModel * glm::vec4(m_cloudCardCenters[static_cast<std::size_t>(lhs)] * cloud.cubeSpread, 1.0f));
                const glm::vec3 rhsWorld = glm::vec3(cloudWorldModel * glm::vec4(m_cloudCardCenters[static_cast<std::size_t>(rhs)] * cloud.cubeSpread, 1.0f));
                const float lhsDistSq = glm::dot(lhsWorld - cameraPos, lhsWorld - cameraPos);
                const float rhsDistSq = glm::dot(rhsWorld - cameraPos, rhsWorld - cameraPos);
                return lhsDistSq > rhsDistSq;
            });

            for (const int cardIndex : drawOrder) {
                const std::uintptr_t offset = static_cast<std::uintptr_t>(cardIndex * 6 * static_cast<int>(sizeof(unsigned int)));
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, reinterpret_cast<const void*>(offset));
            }
        }

        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    m_weatherRenderer.renderRain(
        projection,
        view,
        world,
        m_rainIntensity,
        m_weatherEnabled,
        m_rainLineWidth,
        m_rainOpacityScale);
    m_weatherRenderer.renderSplashes(
        projection,
        view,
        world,
        m_rainIntensity,
        m_weatherEnabled,
        m_splashPointSize,
        m_splashOpacityScale);
    m_weatherRenderer.renderDroplets(
        projection,
        view,
        world,
        m_rainIntensity,
        m_weatherEnabled,
        m_dropletPointSize,
        m_dropletOpacityScale);
    m_weatherRenderer.renderRipples(
        projection,
        view,
        world,
        m_rainIntensity,
        m_weatherEnabled,
        m_ripplePointSize,
        m_rippleOpacityScale);

    glBindVertexArray(0);
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

    // Cloud geometry: scattered rectangular cards inside an invisible cube volume.
    std::vector<float> cloudVertices;
    std::vector<unsigned int> cloudIndices;
    constexpr int kCloudCardCapacity = 28;
    cloudVertices.reserve(4 * kCloudCardCapacity * 6);
    cloudIndices.reserve(kCloudCardCapacity * 6);
    m_cloudCardCenters.clear();
    m_cloudCardCenters.reserve(kCloudCardCapacity);
    unsigned int cloudCardId = 0;

    auto appendCloudQuad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
        const unsigned int base = static_cast<unsigned int>(cloudVertices.size() / 6);
        const glm::vec3 points[4] = {a, b, c, d};
        const glm::vec2 uvs[4] = {
            glm::vec2(-1.0f, -1.0f),
            glm::vec2( 1.0f, -1.0f),
            glm::vec2( 1.0f,  1.0f),
            glm::vec2(-1.0f,  1.0f),
        };
        for (int i = 0; i < 4; ++i) {
            const glm::vec3& p = points[i];
            cloudVertices.push_back(p.x);
            cloudVertices.push_back(p.y);
            cloudVertices.push_back(p.z);
            cloudVertices.push_back(uvs[i].x);
            cloudVertices.push_back(uvs[i].y);
            cloudVertices.push_back(static_cast<float>(cloudCardId));
        }
        cloudIndices.push_back(base + 0);
        cloudIndices.push_back(base + 1);
        cloudIndices.push_back(base + 2);
        cloudIndices.push_back(base + 2);
        cloudIndices.push_back(base + 3);
        cloudIndices.push_back(base + 0);
        m_cloudCardCenters.push_back((a + b + c + d) * 0.25f);
        ++cloudCardId;
    };

    std::mt19937 rng(93721u);
    std::uniform_real_distribution<float> cubeDist(-0.52f, 0.52f);
    std::uniform_real_distribution<float> sizeXDist(0.18f, 0.55f);
    std::uniform_real_distribution<float> sizeYDist(0.16f, 0.58f);
    std::uniform_real_distribution<float> yawDist(0.0f, 360.0f);
    std::uniform_real_distribution<float> pitchDist(-28.0f, 28.0f);
    std::uniform_real_distribution<float> rollDist(-22.0f, 22.0f);

    for (int i = 0; i < kCloudCardCapacity; ++i) {
        glm::vec3 center(cubeDist(rng), cubeDist(rng) * 0.72f, cubeDist(rng));
        const float halfWidth = sizeXDist(rng);
        const float halfHeight = sizeYDist(rng);

        glm::mat4 rot(1.0f);
        rot = glm::rotate(rot, glm::radians(pitchDist(rng)), glm::vec3(1.0f, 0.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(yawDist(rng)), glm::vec3(0.0f, 1.0f, 0.0f));
        rot = glm::rotate(rot, glm::radians(rollDist(rng)), glm::vec3(0.0f, 0.0f, 1.0f));
        const glm::mat3 rot3(rot);

        const glm::vec3 a = center + rot3 * glm::vec3(-halfWidth, -halfHeight, 0.0f);
        const glm::vec3 b = center + rot3 * glm::vec3( halfWidth, -halfHeight, 0.0f);
        const glm::vec3 c = center + rot3 * glm::vec3( halfWidth,  halfHeight, 0.0f);
        const glm::vec3 d = center + rot3 * glm::vec3(-halfWidth,  halfHeight, 0.0f);
        appendCloudQuad(a, b, c, d);
    }

    glGenVertexArrays(1, &m_cloudVao);
    glGenBuffers(1, &m_cloudVbo);
    glGenBuffers(1, &m_cloudEbo);
    glBindVertexArray(m_cloudVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cloudVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(cloudVertices.size() * sizeof(float)), cloudVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cloudEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(cloudIndices.size() * sizeof(unsigned int)), cloudIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), reinterpret_cast<void*>(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    m_cloudIndexCount = static_cast<int>(cloudIndices.size());

    // Default terrain plane.
    static constexpr float terrainY = -0.76f;
    static constexpr float terrainSize = 220.0f;
    const float terrainVertices[] = {
        -terrainSize, terrainY, -terrainSize,
         terrainSize, terrainY, -terrainSize,
         terrainSize, terrainY,  terrainSize,
        -terrainSize, terrainY,  terrainSize,
    };
    const unsigned int terrainIndices[] = {
        0, 1, 2,
        2, 3, 0,
    };

    glGenVertexArrays(1, &m_terrainVao);
    glGenBuffers(1, &m_terrainVbo);
    glGenBuffers(1, &m_terrainEbo);
    glBindVertexArray(m_terrainVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_terrainVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(terrainVertices), terrainVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrainEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(terrainIndices), terrainIndices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);
    m_terrainIndexCount = 6;

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
}

}  // namespace sparks::render
