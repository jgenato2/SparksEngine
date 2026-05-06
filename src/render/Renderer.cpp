#include <ctime>
#include <cmath>
#include <cstdio>


#include <stdexcept>
#include "sparks/render/terrain/DiamondSquareTerrain.hpp"
#include "sparks/render/Renderer.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "sparks/render/CloudShadowMap.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace sparks::render {


// Struct to hold precomputed star data (must match header)
#include "sparks/render/Renderer.hpp" // Ensure StarData is declared before use

// Utility: hash function for deterministic randomness (matches shader)
static float hash21(float x, float y) {
    glm::vec2 p = glm::fract(glm::vec2(x, y) * glm::vec2(127.1f, 311.7f));
    return glm::fract(p.x * p.y);
}

// Generate star list (tile-based, matches shader logic)
std::vector<StarData> generateStarList(int tileU = 16, int tileV = 12) {
    std::vector<StarData> stars;
    const float tileUf = static_cast<float>(tileU);
    const float tileVf = static_cast<float>(tileV);
    for (int y = 0; y < tileV; ++y) {
        for (int x = 0; x < tileU; ++x) {
            const float xf = static_cast<float>(x);
            const float yf = static_cast<float>(y);
            // Avoid seam: skip tiles near u=0 or u=1
            float u = (xf + 0.5f) / tileUf;
            if (u < 0.04f || u > 0.96f) continue;
            // Hash to get star position within tile, avoid edge
            float starU = (hash21(xf + 0.1f, yf) * 0.6f + 0.2f) / tileUf + xf / tileUf;
            float starV = (hash21(xf + 0.7f, yf) * 0.6f + 0.2f) / tileVf + yf / tileVf;
            float phi = starU * 2.0f * float(M_PI);
            float theta = starV * float(M_PI);
            glm::vec3 dir = glm::vec3(
                std::cos(phi) * std::sin(theta),
                std::cos(theta),
                std::sin(phi) * std::sin(theta)
            );
            // Color variation per star
            float colorSeed = hash21(xf + 17.0f, yf);
            glm::vec3 color = glm::mix(glm::vec3(1.0f, 0.95f, 0.95f), glm::vec3(0.7f, 0.85f, 1.0f), colorSeed);
            // Twinkle
            float twinklePhase = hash21(xf + 5.7f, yf) * 2.0f * float(M_PI);
            float shineRand = hash21(xf + 9.7f, yf);
            float twinkleAmp = glm::mix(0.5f, 1.0f, shineRand);
            // Size
            float sizeRand = hash21(xf + 8.3f, yf);
            float radius = glm::mix(0.00004f, 0.00010f, sizeRand);
            stars.push_back(StarData{dir, color, twinklePhase, twinkleAmp, radius});
        }
    }
    return stars;
}

// Simple sun position calculation (not as accurate as SPA/NOAA, but sufficient for visual realism)
// Returns sun direction in world space (Y up)
glm::vec3 computeSunDirection(float latitude, float longitude, float utcTime, int dayOfYear) {
    // Convert degrees to radians
    const float latRad = glm::radians(latitude);
    // Fractional year (in radians)
    float gamma = 2.0f * static_cast<float>(M_PI) / 365.0f * (dayOfYear - 1 + (utcTime - 12.0f) / 24.0f);
    // Equation of time and declination (approximate)
    float eqTime = 229.18f * (0.000075f + 0.001868f * cos(gamma) - 0.032077f * sin(gamma)
        - 0.014615f * cos(2.0f * gamma) - 0.040849f * sin(2.0f * gamma));
    float decl = 0.006918f - 0.399912f * cos(gamma) + 0.070257f * sin(gamma)
        - 0.006758f * cos(2.0f * gamma) + 0.000907f * sin(2.0f * gamma)
        - 0.002697f * cos(3.0f * gamma) + 0.00148f * sin(3.0f * gamma);
    // Time offset
    float timeOffset = eqTime + 4.0f * longitude - 60.0f * 0.0f; // 0.0 = timezone offset
    // True solar time (in minutes)
    float tst = utcTime * 60.0f + timeOffset;
    // Hour angle
    float ha = (tst / 4.0f) - 180.0f;
    float haRad = glm::radians(ha);
    // Solar elevation
    float elevation = asinf(sinf(latRad) * sinf(decl) + cosf(latRad) * cosf(decl) * cosf(haRad));
    // Solar azimuth
    float azimuth = atan2f(-sinf(haRad), cosf(latRad) * tanf(decl) - sinf(latRad) * cosf(haRad));
    // Convert to direction vector (Y up)
    float y = sinf(elevation);
    float x = cosf(elevation) * sinf(azimuth);
    float z = cosf(elevation) * cosf(azimuth);
    return glm::normalize(glm::vec3(x, y, z));
}

    void Renderer::initializeCloudShadowMap(int size)
    {
        m_cloudShadowMapSize = size;
        if (m_cloudShadowFbo != 0)
        {
            glDeleteFramebuffers(1, &m_cloudShadowFbo);
            m_cloudShadowFbo = 0;
        }
        if (m_cloudShadowTex != 0)
        {
            glDeleteTextures(1, &m_cloudShadowTex);
            m_cloudShadowTex = 0;
        }
        glGenFramebuffers(1, &m_cloudShadowFbo);
        glGenTextures(1, &m_cloudShadowTex);
        glBindTexture(GL_TEXTURE_2D, m_cloudShadowTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size, size, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, m_cloudShadowFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_cloudShadowTex, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            throw std::runtime_error("Cloud shadow framebuffer incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

} // namespace sparks::render

#include <algorithm>
#include <type_traits>
#include <iterator>
#include <limits>
#include <utility>
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

constexpr float kCameraFarPlane = 1000.0f;

#include "sparks/render/GLUtils.hpp"
#include "sparks/render/ShaderFacade.hpp"

unsigned int createCloudProgram()
{
    static constexpr const char *kVertexShader = R"(
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

    static constexpr const char *kFragmentShader = R"(
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

    return GLUtils::createProgram(kVertexShader, kFragmentShader);
}

// ── Underwater post-process program ─────────────────────────────────────────
// Renders a fullscreen triangle (gl_VertexID trick, no VBO) and applies:
//   wave-distorted UV sampling, chromatic aberration, animated FBM caustics,
//   depth-based colour tint/fog, and a vignette.
unsigned int createUnderwaterProgram()
{
    static constexpr const char *kVertexShader = R"(
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

    static constexpr const char *kFragmentShader = R"(
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

    return GLUtils::createProgram(kVertexShader, kFragmentShader);
}

namespace sparks::render
{

    Renderer::~Renderer()
    {
        destroyFramebuffer();
        m_weatherRenderer.shutdown();

        if (m_skydomeTexture != 0)
        {
            glDeleteTextures(1, &m_skydomeTexture);
        }

        if (m_terrainTexture != 0)
        {
            glDeleteTextures(1, &m_terrainTexture);
        }

        if (m_skydomeEbo != 0)
        {
            glDeleteBuffers(1, &m_skydomeEbo);
        }
        if (m_skydomeVbo != 0)
        {
            glDeleteBuffers(1, &m_skydomeVbo);
        }
        if (m_skydomeVao != 0)
        {
            glDeleteVertexArrays(1, &m_skydomeVao);
        }
        if (m_skydomeProgram != 0)
        {
            glDeleteProgram(m_skydomeProgram);
        }

        if (m_terrainEbo != 0)
        {
            glDeleteBuffers(1, &m_terrainEbo);
        }
        if (m_terrainVbo != 0)
        {
            glDeleteBuffers(1, &m_terrainVbo);
        }
        if (m_terrainVao != 0)
        {
            glDeleteVertexArrays(1, &m_terrainVao);
        }
        if (m_terrainProgram != 0)
        {
            glDeleteProgram(m_terrainProgram);
        }

        if (m_waterEbo != 0)
        {
            glDeleteBuffers(1, &m_waterEbo);
        }
        if (m_waterVbo != 0)
        {
            glDeleteBuffers(1, &m_waterVbo);
        }
        if (m_waterVao != 0)
        {
            glDeleteVertexArrays(1, &m_waterVao);
        }
        if (m_waterProgram != 0)
        {
            glDeleteProgram(m_waterProgram);
        }

        if (m_fullscreenVao != 0)
        {
            glDeleteVertexArrays(1, &m_fullscreenVao);
        }
        if (m_underwaterProgram != 0)
        {
            glDeleteProgram(m_underwaterProgram);
        }

        if (m_cloudProgram != 0)
        {
            glDeleteProgram(m_cloudProgram);
        }
        if (m_cloudSortedEbo != 0)
        {
            glDeleteBuffers(1, &m_cloudSortedEbo);
        }
        if (m_cloudVao != 0)
        {
            glDeleteVertexArrays(1, &m_cloudVao);
        }

        if (m_shaderProgram != 0)
        {
            glDeleteProgram(m_shaderProgram);
        }

        if (m_gridVbo != 0)
        {
            glDeleteBuffers(1, &m_gridVbo);
        }

        if (m_gridVao != 0)
        {
            glDeleteVertexArrays(1, &m_gridVao);
        }

        if (m_importTexture != 0)
        {
            glDeleteTextures(1, &m_importTexture);
        }

        if (m_importEbo != 0)
        {
            glDeleteBuffers(1, &m_importEbo);
        }

        if (m_importVbo != 0)
        {
            glDeleteBuffers(1, &m_importVbo);
        }

        if (m_importVao != 0)
        {
            glDeleteVertexArrays(1, &m_importVao);
        }

        if (m_texturedProgram != 0)
        {
            glDeleteProgram(m_texturedProgram);
        }
    }

    void Renderer::initialize()
    {
        // Main shader program: pass vertex and fragment shader sources
        static constexpr const char *kVertexShader = R"(
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

        static constexpr const char *kFragmentShader = R"(
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

        m_shaderProgram = GLUtils::createProgram(kVertexShader, kFragmentShader);
        m_texturedProgram = ShaderFacade::CreateTexturedProgram();
        m_skydomeProgram = ShaderFacade::CreateSkydomeProgram();
        m_terrainProgram = ShaderFacade::CreateTerrainProgram();
        m_waterProgram = ShaderFacade::CreateWaterProgram();
        m_cloudProgram = ShaderFacade::CreateCloudProgram();
        m_underwaterProgram = ShaderFacade::CreateUnderwaterProgram();

        // Only disable cloud objects, allow procedural cloud formation
        m_environmentSettings.enableCloudObjects = false;
        if (!m_environmentSettings.cloudObjects.empty()) {
            for (auto& cloud : m_environmentSettings.cloudObjects) {
                cloud.enabled = false;
            }
        }
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
        m_terrainTexture = GLUtils::loadTexture2DFromFile(terrainTexturePath, false);
        m_skydomeTexture = 0;
        m_hasSkydomeTexture = false;
        m_hasTerrainTexture = (m_terrainTexture != 0);

        createGridResources();
        createFramebuffer();
    }

    void Renderer::setImportedModel(const ImportedModelData &model)
    {
        if (m_importTexture == 0)
        {
            glGenTextures(1, &m_importTexture);
        }
        if (m_importVao == 0)
        {
            glGenVertexArrays(1, &m_importVao);
        }
        if (m_importVbo == 0)
        {
            glGenBuffers(1, &m_importVbo);
        }
        if (m_importEbo == 0)
        {
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
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(6 * sizeof(float)));
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

    void Renderer::setImportedModelTransform(const glm::vec3 &position, const glm::vec3 &rotationEulerDegrees, const glm::vec3 &scale)
    {
        m_importPosition = position;
        m_importRotationEuler = rotationEulerDegrees;
        m_importScale = scale;
    }

    void Renderer::setEnvironmentSettings(const EnvironmentSettings &settings)
    {

        // Detect if terrain or water settings changed
        bool terrainChanged =
            settings.terrainSize != m_environmentSettings.terrainSize ||
            settings.terrainHeight != m_environmentSettings.terrainHeight ||
            settings.terrainPatchScale != m_environmentSettings.terrainPatchScale ||
            settings.terrainRoughness != m_environmentSettings.terrainRoughness;

        bool waterChanged =
            settings.enableWater != m_environmentSettings.enableWater ||
            settings.waterLevel != m_environmentSettings.waterLevel ||
            settings.waterColor != m_environmentSettings.waterColor ||
            settings.waterOpacity != m_environmentSettings.waterOpacity ||
            settings.waveAmplitude != m_environmentSettings.waveAmplitude ||
            settings.waveFrequency != m_environmentSettings.waveFrequency ||
            settings.waterFoamIntensity != m_environmentSettings.waterFoamIntensity;

        m_environmentSettings = settings;
        // Only disable cloud objects, allow procedural cloud formation
        m_environmentSettings.enableCloudObjects = false;
        if (!m_environmentSettings.cloudObjects.empty()) {
            for (auto& cloud : m_environmentSettings.cloudObjects) {
                cloud.enabled = false;
            }
        }
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

        // Clamp fogNear and fogFar with safety
        m_environmentSettings.fogNear = glm::clamp(m_environmentSettings.fogNear, 1.0f, 500.0f);
        float fogFarMin = m_environmentSettings.fogNear + 1.0f;
        float fogFarMax = 900.0f;
        if (fogFarMin > fogFarMax) {
            m_environmentSettings.fogFar = fogFarMin;
        } else {
            m_environmentSettings.fogFar = glm::clamp(m_environmentSettings.fogFar, fogFarMin, fogFarMax);
        }
        m_environmentSettings.fogStrength = glm::clamp(m_environmentSettings.fogStrength, 0.0f, 1.0f);

        // Clamp terrain
        float terrainSizeMin = 20.0f, terrainSizeMax = 4000.0f;
        if (terrainSizeMin > terrainSizeMax) {
            m_environmentSettings.terrainSize = terrainSizeMin;
        } else {
            m_environmentSettings.terrainSize = glm::clamp(m_environmentSettings.terrainSize, terrainSizeMin, terrainSizeMax);
        }
        float terrainHeightMin = -20.0f, terrainHeightMax = 20.0f;
        if (terrainHeightMin > terrainHeightMax) {
            m_environmentSettings.terrainHeight = terrainHeightMin;
        } else {
            m_environmentSettings.terrainHeight = glm::clamp(m_environmentSettings.terrainHeight, terrainHeightMin, terrainHeightMax);
        }
        float terrainPatchScaleMin = 0.01f, terrainPatchScaleMax = 4.0f;
        if (terrainPatchScaleMin > terrainPatchScaleMax) {
            m_environmentSettings.terrainPatchScale = terrainPatchScaleMin;
        } else {
            m_environmentSettings.terrainPatchScale = glm::clamp(m_environmentSettings.terrainPatchScale, terrainPatchScaleMin, terrainPatchScaleMax);
        }
        float terrainRoughnessMin = 0.0f, terrainRoughnessMax = 3.0f;
        if (terrainRoughnessMin > terrainRoughnessMax) {
            m_environmentSettings.terrainRoughness = terrainRoughnessMin;
        } else {
            m_environmentSettings.terrainRoughness = glm::clamp(m_environmentSettings.terrainRoughness, terrainRoughnessMin, terrainRoughnessMax);
        }
        for (auto &cloud : m_environmentSettings.cloudObjects)
        {
            cloud.scale = glm::max(cloud.scale, glm::vec3(0.05f));
            cloud.opacity = glm::clamp(cloud.opacity, 0.0f, 1.0f);
            cloud.softness = glm::clamp(cloud.softness, 0.2f, 0.98f);
            cloud.detail = glm::clamp(cloud.detail, 0.3f, 4.5f);
            cloud.planeFade = glm::clamp(cloud.planeFade, 0.0f, 1.0f);
            cloud.planeCount = std::max(1, std::min(cloud.planeCount, 28));
            cloud.cubeSpread = glm::clamp(cloud.cubeSpread, 0.35f, 2.50f);
            cloud.motionSpeed = glm::clamp(cloud.motionSpeed, 0.0f, 8.0f);
            cloud.cloudType = std::max(0, std::min(cloud.cloudType, 2));
            if (cloud.planeSelectionSeed == 0u)
            {
                cloud.planeSelectionSeed = 1u;
            }
        }
        if (glm::length(m_environmentSettings.terrainLightDirection) < 0.001f)
        {
            m_environmentSettings.terrainLightDirection = glm::vec3(0.30f, 0.72f, -0.46f);
        }
        if (terrainChanged || waterChanged) {
            createEnvironmentResources();
        }
    }

    void Renderer::setViewportSize(const int width, const int height)
    {
        if (width <= 0 || height <= 0)
        {
            return;
        }

        if (width == m_viewportWidth && height == m_viewportHeight)
        {
            return;
        }

        m_viewportWidth = width;
        m_viewportHeight = height;
        rebuildFramebufferIfNeeded(width, height);
    }

    void Renderer::setWeatherRain(
        const std::vector<glm::vec3> &lineVertices,
        const std::vector<glm::vec4> &splashPoints,
        const std::vector<glm::vec4> &dropletPoints,
        const std::vector<glm::vec4> &ripplePoints,
        const int rainConcept,
        const bool useCustomVisualProfile,
        const glm::vec3 &rainTint,
        const glm::vec3 &splashTint,
        const glm::vec3 &dropletTint,
        const glm::vec3 &rippleTint,
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
        const float rippleOpacityScale)
    {
        m_rainConcept = std::max(0, std::min(rainConcept, 5));
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

    void Renderer::render(const ViewControls &viewControls)
    {
        // (Moved: Set uStarDensity after binding skydome program)
        // --- Update sun direction from real-world parameters ---
        // Get current day of year
        std::time_t t = std::time(nullptr);
        std::tm tm_now;
#ifdef _WIN32
        localtime_s(&tm_now, &t);
#else
        tm_now = *std::localtime(&t);
#endif
        int dayOfYear = tm_now.tm_yday + 1;
        // Use environment settings for sun position
        if (m_environmentSettings.latitude != 0.0 || m_environmentSettings.longitude != 0.0) {
            glm::vec3 sunDir = computeSunDirection(
                m_environmentSettings.latitude,
                m_environmentSettings.longitude,
                m_environmentSettings.utcTime,
                dayOfYear);
            m_environmentSettings.terrainLightDirection = sunDir;
        }

        static const auto sRenderStart = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        const float elapsedSeconds = std::chrono::duration<float>(now - sRenderStart).count();

        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
        glViewport(0, 0, m_viewportWidth, m_viewportHeight);
        glEnable(GL_DEPTH_TEST);

        glClearColor(0.28f, 0.40f, 0.58f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        enum class GpuProfileSlot : int
        {
            Skydome = 0,
            Terrain,
            Water,
            Import,
            Weather,
            Total,
            Count
        };

        // Profiling timers
        static double gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Count)] = {};
        static int perfFrameCount = 0;
        static auto lastPerfTime = std::chrono::steady_clock::now();
        GLuint gpuQueries[static_cast<int>(GpuProfileSlot::Count) - 1] = {};
        GLuint totalTimestampQueries[2] = {};
        glGenQueries(static_cast<GLsizei>(GpuProfileSlot::Count) - 1, gpuQueries);
        glGenQueries(2, totalTimestampQueries);
        glQueryCounter(totalTimestampQueries[0], GL_TIMESTAMP);

        glUseProgram(m_shaderProgram);

        const float aspectRatio = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
        const glm::vec3 cameraTarget(viewControls.panOffset.x, viewControls.panOffset.y, viewControls.orbitTargetZ);
        const glm::vec3 cameraPos(cameraTarget.x, cameraTarget.y, cameraTarget.z + viewControls.zoomDistance);
        // Standard view matrix (with translation)
        const glm::mat4 view = glm::lookAt(
            cameraPos,
            cameraTarget,
            glm::vec3(0.0f, 1.0f, 0.0f));

        // Rotation-only view matrix for skydome (zero out translation in last column)
        glm::mat4 viewRotOnly = view;
        viewRotOnly[0][3] = 0.0f;
        viewRotOnly[1][3] = 0.0f;
        viewRotOnly[2][3] = 0.0f;
        viewRotOnly[3][3] = 1.0f;
        const glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, kCameraFarPlane);

        glm::mat4 world(1.0f);
        // Orbit around the active camera target instead of always rotating around world origin.
        world = glm::translate(world, cameraTarget);
        world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
        world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        world = glm::translate(world, -cameraTarget);
        if (m_environmentSettings.enableSkydome && m_skydomeProgram != 0 && m_skydomeVao != 0 && m_skydomeIndexCount > 0)
        {
            // Compute world rotation matrix (no translation, only world rotation)
            glm::mat4 worldRotMat(1.0f);
            worldRotMat = glm::rotate(worldRotMat, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            worldRotMat = glm::rotate(worldRotMat, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::mat3 worldRot3x3 = glm::mat3(worldRotMat);
            const int skyWorldRotLoc = glGetUniformLocation(m_skydomeProgram, "uWorldRot");
            glUniformMatrix3fv(skyWorldRotLoc, 1, GL_FALSE, glm::value_ptr(worldRot3x3));
            glUseProgram(m_skydomeProgram);

            glm::mat4 skydomeModel(1.0f);
            skydomeModel = glm::scale(skydomeModel, glm::vec3(m_environmentSettings.skydomeRadius));
            // No rotation or translation for skydome: only scale
            glm::mat4 skydomeWorld(1.0f);
            skydomeWorld = glm::translate(skydomeWorld, cameraPos);
            skydomeWorld = glm::rotate(skydomeWorld, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
            skydomeWorld = glm::rotate(skydomeWorld, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::mat4 skyMvp = projection * viewRotOnly * skydomeWorld * skydomeModel;


            const int skyMvpLoc = glGetUniformLocation(m_skydomeProgram, "uMvp");
            const int skyHorizonLoc = glGetUniformLocation(m_skydomeProgram, "uHorizonColor");
            const int skyZenithLoc = glGetUniformLocation(m_skydomeProgram, "uZenithColor");
            const int skySunDirLoc = glGetUniformLocation(m_skydomeProgram, "uSunDir");
            const int skySunDiscSizeLoc = glGetUniformLocation(m_skydomeProgram, "uSunDiscSize");
            const int skySunIntensityLoc = glGetUniformLocation(m_skydomeProgram, "uSunIntensity");
            const int skySunColorLoc = glGetUniformLocation(m_skydomeProgram, "uSunColor");
            const int skySunHaloSizeLoc = glGetUniformLocation(m_skydomeProgram, "uSunHaloSize");
            const int skySunHaloStrengthLoc = glGetUniformLocation(m_skydomeProgram, "uSunHaloStrength");
            const int skyCloudAmountLoc = glGetUniformLocation(m_skydomeProgram, "uCloudAmount");
            const int skyCloudScaleLoc = glGetUniformLocation(m_skydomeProgram, "uCloudScale");
            const int skyCloudSpeedLoc = glGetUniformLocation(m_skydomeProgram, "uCloudSpeed");
            const int skyCloudColorLoc = glGetUniformLocation(m_skydomeProgram, "uCloudColor");
            const int skyCloudShadowStrengthLoc = glGetUniformLocation(m_skydomeProgram, "uCloudShadowStrength");
            const int skyWindDirectionLoc = glGetUniformLocation(m_skydomeProgram, "uWindDirection");
            const int skyWindSpeedLoc = glGetUniformLocation(m_skydomeProgram, "uWindSpeed");
            const int skyTimeLoc = glGetUniformLocation(m_skydomeProgram, "uTime");

            // Patch: If sun is disabled, zero out sun uniforms for skydome

            float sunDiscSize = m_environmentSettings.enableSun ? m_environmentSettings.sunDiscSize : 0.0f;
            float sunIntensity = m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f;
            glm::vec3 sunColor = m_environmentSettings.enableSun ? m_environmentSettings.sunColor : glm::vec3(0.0f);
            float sunHaloSize = m_environmentSettings.enableSun ? m_environmentSettings.sunHaloSize : 0.0f;
            float sunHaloStrength = m_environmentSettings.enableSun ? m_environmentSettings.sunHaloStrength : 0.0f;

            // --- Environmental bloom calculation (same as skydome shader) ---
            glm::vec3 sunDir = glm::normalize(m_environmentSettings.terrainLightDirection);
            glm::vec3 upDir = glm::vec3(0.0f, 1.0f, 0.0f);
            float sunDot = glm::clamp(glm::dot(upDir, sunDir), 0.0f, 1.0f);
            float envBloom = powf(sunDot, 8.0f) * glm::clamp(sunIntensity, 0.0f, 2.0f);
            // --- Terrain rendering (always before water) ---
            // ...existing code...
            // Set environmental bloom uniform for terrain
            const int terrainEnvBloomLoc = glGetUniformLocation(m_terrainProgram, "uEnvBloom");
            glUniform1f(terrainEnvBloomLoc, envBloom);

            glUniformMatrix4fv(skyMvpLoc, 1, GL_FALSE, glm::value_ptr(skyMvp));
            glUniform3f(skyHorizonLoc, m_environmentSettings.skyHorizonColor.r, m_environmentSettings.skyHorizonColor.g, m_environmentSettings.skyHorizonColor.b);
            glUniform3f(skyZenithLoc, m_environmentSettings.skyZenithColor.r, m_environmentSettings.skyZenithColor.g, m_environmentSettings.skyZenithColor.b);
            glUniform3f(skySunDirLoc, m_environmentSettings.terrainLightDirection.x, m_environmentSettings.terrainLightDirection.y, m_environmentSettings.terrainLightDirection.z);
            glUniform1f(skySunDiscSizeLoc, sunDiscSize);
            glUniform1f(skySunIntensityLoc, sunIntensity);
            glUniform3f(skySunColorLoc, sunColor.r, sunColor.g, sunColor.b);
            glUniform1f(skySunHaloSizeLoc, sunHaloSize);
            glUniform1f(skySunHaloStrengthLoc, sunHaloStrength);
            glUniform1f(skyCloudAmountLoc, m_environmentSettings.skyCloudAmount);
            glUniform1f(skyCloudScaleLoc, m_environmentSettings.skyCloudScale);
            glUniform1f(skyCloudSpeedLoc, m_environmentSettings.skyCloudSpeed);
            glUniform3f(skyCloudColorLoc, m_environmentSettings.skyCloudColor.r, m_environmentSettings.skyCloudColor.g, m_environmentSettings.skyCloudColor.b);
            glUniform1f(skyCloudShadowStrengthLoc, m_environmentSettings.skyCloudShadowStrength);
            glUniform2f(skyWindDirectionLoc, m_environmentSettings.windDirection.x, m_environmentSettings.windDirection.y);
            glUniform1f(skyWindSpeedLoc, m_environmentSettings.windSpeed);
            glUniform1f(skyTimeLoc, elapsedSeconds);

            // Set star density uniform here (after program is bound)
            const int skyStarDensityLoc = glGetUniformLocation(m_skydomeProgram, "uStarDensity");
            glUniform1f(skyStarDensityLoc, m_environmentSettings.starDensity);

            if (m_hasSkydomeTexture)
            // Patch: Ignore skydome texture to ensure procedural stars are visible
            // (Do not bind skydome texture, always use procedural shader)

            glBeginQuery(GL_TIME_ELAPSED, gpuQueries[static_cast<int>(GpuProfileSlot::Skydome)]);
            glDepthMask(GL_FALSE);
            glBindVertexArray(m_skydomeVao);
            glDrawElements(GL_TRIANGLES, m_skydomeIndexCount, GL_UNSIGNED_INT, nullptr);
            glDepthMask(GL_TRUE);
            glEndQuery(GL_TIME_ELAPSED);
        }

        // --- Terrain rendering (always before water) ---
        if (m_environmentSettings.enableTerrain && m_terrainProgram != 0 && m_terrainVao != 0 && m_terrainIndexCount > 0)
        {
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
            const int terrainTexLoc = glGetUniformLocation(m_terrainProgram, "uTex");
            const int terrainUseTexLoc = glGetUniformLocation(m_terrainProgram, "uUseTexture");
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
            glUniform3f(terrainLightDirLoc, m_environmentSettings.terrainLightDirection.x, m_environmentSettings.terrainLightDirection.y, m_environmentSettings.terrainLightDirection.z);
            glUniform1i(terrainTexLoc, 1);
            glUniform1i(terrainUseTexLoc, m_hasTerrainTexture ? 1 : 0);
            glUniform3f(terrainCameraPosLoc, cameraPos.x, cameraPos.y, cameraPos.z);
            glUniform3f(terrainFogColorLoc, m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
            glUniform1f(terrainFogNearLoc, m_environmentSettings.fogNear);
            glUniform1f(terrainFogFarLoc, m_environmentSettings.fogFar);
            glUniform1f(terrainFogStrengthLoc, m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);
            // Set required uniforms for terrain shader
            const int terrainDiffuseColorLoc = glGetUniformLocation(m_terrainProgram, "uDiffuseColor");
            const int terrainOpacityLoc = glGetUniformLocation(m_terrainProgram, "uOpacity");
            const int terrainAlphaCutoffLoc = glGetUniformLocation(m_terrainProgram, "uAlphaCutoff");
            const int terrainUnlitShadingLoc = glGetUniformLocation(m_terrainProgram, "uUnlitShading");
            const int terrainShadowPassLoc = glGetUniformLocation(m_terrainProgram, "uShadowPass");
            const int terrainEmissiveLoc = glGetUniformLocation(m_terrainProgram, "uEmissive");
            const int terrainSunColorLoc = glGetUniformLocation(m_terrainProgram, "uSunColor");
            const int terrainSunGlowStrengthLoc = glGetUniformLocation(m_terrainProgram, "uSunGlowStrength");
            const int terrainWaterEnabledLoc = glGetUniformLocation(m_terrainProgram, "uWaterEnabled");
            const int terrainWaterLevelLoc = glGetUniformLocation(m_terrainProgram, "uWaterLevel");
            const int terrainWaterTintLoc = glGetUniformLocation(m_terrainProgram, "uWaterTint");
            glUniform4f(terrainDiffuseColorLoc, 1.0f, 1.0f, 1.0f, 1.0f); // White, fully opaque
            glUniform1f(terrainOpacityLoc, 1.0f);
            glUniform1f(terrainAlphaCutoffLoc, 0.01f);
            glUniform1i(terrainUnlitShadingLoc, 0);
            glUniform1i(terrainShadowPassLoc, 0);
            glUniform3f(terrainEmissiveLoc, 0.0f, 0.0f, 0.0f);
            glUniform3f(terrainSunColorLoc, m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
            glUniform1f(terrainSunGlowStrengthLoc, (m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f) * 1.0f);
            glUniform1i(terrainWaterEnabledLoc, m_environmentSettings.enableWater ? 1 : 0);
            glUniform1f(terrainWaterLevelLoc, m_environmentSettings.waterLevel);
            glUniform3f(terrainWaterTintLoc, m_environmentSettings.waterColor.r, m_environmentSettings.waterColor.g, m_environmentSettings.waterColor.b);

            // Bind terrain texture to texture unit 1 before drawing terrain
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_terrainTexture);

            glBeginQuery(GL_TIME_ELAPSED, gpuQueries[static_cast<int>(GpuProfileSlot::Terrain)]);
            glBindVertexArray(m_terrainVao);
            glDrawElements(GL_TRIANGLES, m_terrainIndexCount, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
            glEndQuery(GL_TIME_ELAPSED);
        }

        // --- Water rendering (after terrain) ---
        if (m_environmentSettings.enableWater && m_waterProgram != 0 && m_waterVao != 0 && m_waterIndexCount > 0)
        {
            glUseProgram(m_waterProgram);
            const int waterFoamIntensityLoc = glGetUniformLocation(m_waterProgram, "uFoamIntensity");
            glUniform1f(waterFoamIntensityLoc, m_environmentSettings.waterFoamIntensity);

            glm::mat4 waterModel(1.0f);
            waterModel = glm::translate(waterModel, glm::vec3(0.0f, m_environmentSettings.waterLevel, 0.0f));
            constexpr float kDefaultWaterSize = 220.0f;
            waterModel = glm::scale(waterModel, glm::vec3(kDefaultWaterSize / 220.0f, 1.0f, kDefaultWaterSize / 220.0f));
            const glm::mat4 waterWorldModel = world * waterModel;
            const glm::mat4 waterMvp = projection * view * waterWorldModel;

            const int waterMvpLoc = glGetUniformLocation(m_waterProgram, "uMvp");
            const int waterModelLoc = glGetUniformLocation(m_waterProgram, "uModel");
            const int waterColorLoc = glGetUniformLocation(m_waterProgram, "uWaterColor");
            const int waterOpacityLoc = glGetUniformLocation(m_waterProgram, "uOpacity");
            const int waterSunColorLoc = glGetUniformLocation(m_waterProgram, "uSunColor");
            const int waterSunIntensityLoc = glGetUniformLocation(m_waterProgram, "uSunIntensity");
            const int waterLightDirLoc = glGetUniformLocation(m_waterProgram, "uLightDir");
            const int waterCameraPosLoc = glGetUniformLocation(m_waterProgram, "uCameraPos");
            const int waterFogColorLoc = glGetUniformLocation(m_waterProgram, "uFogColor");
            const int waterFogNearLoc = glGetUniformLocation(m_waterProgram, "uFogNear");
            const int waterFogFarLoc = glGetUniformLocation(m_waterProgram, "uFogFar");
            const int waterFogStrengthLoc = glGetUniformLocation(m_waterProgram, "uFogStrength");
            const int waveAmplitudeLoc = glGetUniformLocation(m_waterProgram, "uWaveAmplitude");
            const int waveFrequencyLoc = glGetUniformLocation(m_waterProgram, "uWaveFrequency");
            const int waterTimeLoc = glGetUniformLocation(m_waterProgram, "uTime");

            glUniformMatrix4fv(waterMvpLoc, 1, GL_FALSE, glm::value_ptr(waterMvp));
            glUniformMatrix4fv(waterModelLoc, 1, GL_FALSE, glm::value_ptr(waterWorldModel));
            glUniform3f(waterColorLoc, m_environmentSettings.waterColor.r, m_environmentSettings.waterColor.g, m_environmentSettings.waterColor.b);
            glUniform1f(waterOpacityLoc, m_environmentSettings.waterOpacity);
            glUniform3f(waterSunColorLoc, m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
            glUniform1f(waterSunIntensityLoc, m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f);
            glUniform3f(waterLightDirLoc, m_environmentSettings.terrainLightDirection.x, m_environmentSettings.terrainLightDirection.y, m_environmentSettings.terrainLightDirection.z);
            glUniform3f(waterCameraPosLoc, cameraPos.x, cameraPos.y, cameraPos.z);
            glUniform3f(waterFogColorLoc, m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
            glUniform1f(waterFogNearLoc, m_environmentSettings.fogNear);
            glUniform1f(waterFogFarLoc, m_environmentSettings.fogFar);
            glUniform1f(waterFogStrengthLoc, m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);
            glUniform1f(waveAmplitudeLoc, m_environmentSettings.waveAmplitude);
            glUniform1f(waveFrequencyLoc, m_environmentSettings.waveFrequency);
            glUniform1f(waterTimeLoc, elapsedSeconds);

            glBeginQuery(GL_TIME_ELAPSED, gpuQueries[static_cast<int>(GpuProfileSlot::Water)]);
            glBindVertexArray(m_waterVao);
            glDrawElements(GL_TRIANGLES, m_waterIndexCount, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
            glEndQuery(GL_TIME_ELAPSED);

            // Reset OpenGL state if changed (e.g., blending, depth mask)
            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, m_cloudShadowTex);

        if (m_importVao != 0 && m_importTexture != 0 && m_importIndexCount > 0)
        {
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

            if (m_importAlphaBlend)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }

            glBeginQuery(GL_TIME_ELAPSED, gpuQueries[static_cast<int>(GpuProfileSlot::Import)]);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_importTexture);
            glBindVertexArray(m_importVao);
            glDrawElements(GL_TRIANGLES, m_importIndexCount, GL_UNSIGNED_INT, nullptr);
            glEndQuery(GL_TIME_ELAPSED);

            if (m_importAlphaBlend)
            {
                glDepthMask(GL_TRUE);
                glDisable(GL_BLEND);
            }
        }

        // Disable all cloud rendering

        // Set sun direction to be very far away (distant directional light)
        m_environmentSettings.terrainLightDirection = glm::normalize(glm::vec3(-1.0f, 0.5f, -2.0f));

        glBeginQuery(GL_TIME_ELAPSED, gpuQueries[static_cast<int>(GpuProfileSlot::Weather)]);
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
        glEndQuery(GL_TIME_ELAPSED);

        // Reset GL line width after weather rendering
        glLineWidth(1.0f);

        glBindVertexArray(0);

        // ── Grid overlay ─────────────────────────────────────────────────────────
        if (m_shaderProgram != 0 && m_gridVao != 0 && m_gridRegularCount > 0)
        {
            glUseProgram(m_shaderProgram);
            const glm::mat4 gridModel(1.0f);
            const glm::mat4 gridMvp = projection * view * world * gridModel;
            glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uMvp"),   1, GL_FALSE, glm::value_ptr(gridMvp));
            glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(gridModel));
            glUniform3f(glGetUniformLocation(m_shaderProgram, "uLightPos"),
                        m_environmentSettings.terrainLightDirection.x * 1000.0f,
                        m_environmentSettings.terrainLightDirection.y * 1000.0f,
                        m_environmentSettings.terrainLightDirection.z * 1000.0f);
            glUniform1f(glGetUniformLocation(m_shaderProgram, "uGradientStrength"), 0.0f);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);

            glBindVertexArray(m_gridVao);

            // Regular grid lines — subtle grey
            glUniform3f(glGetUniformLocation(m_shaderProgram, "uColor"), 0.45f, 0.45f, 0.45f);
            glUniform1f(glGetUniformLocation(m_shaderProgram, "uAlpha"), 0.5f);
            glDrawArrays(GL_LINES, 0, m_gridRegularCount);

            // X axis — red
            glUniform3f(glGetUniformLocation(m_shaderProgram, "uColor"), 0.85f, 0.2f, 0.2f);
            glUniform1f(glGetUniformLocation(m_shaderProgram, "uAlpha"), 0.9f);
            glDrawArrays(GL_LINES, m_gridAxisXStart, 2);

            // Z axis — blue
            glUniform3f(glGetUniformLocation(m_shaderProgram, "uColor"), 0.2f, 0.35f, 0.85f);
            glUniform1f(glGetUniformLocation(m_shaderProgram, "uAlpha"), 0.9f);
            glDrawArrays(GL_LINES, m_gridAxisYStart, 2);

            glBindVertexArray(0);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        // ── Underwater post-process pass ─────────────────────────────────────────
        // Detect whether the effective camera position in scene-local space is
        // below the flat water plane. The renderer orbits by rotating the world,
        // so comparing against the pre-rotation camera position produces the wrong
        // result once the scene is tilted.
        m_cameraUnderwater = false;
        m_usePostProcessed = false;
        if (m_underwaterProgram != 0 && m_postFbo != 0 && m_postColorTexture != 0 && m_fullscreenVao != 0)
        {
            const bool wantCinematic = m_environmentSettings.enableCinematic;
            bool enableUnderwater = false;
            float underwaterDepth = 0.0f;

            if (m_environmentSettings.enableWater)
            {
                const glm::mat4 inverseWorld = glm::inverse(world);
                const glm::vec3 sceneLocalCameraPos = glm::vec3(inverseWorld * glm::vec4(cameraPos, 1.0f));
                underwaterDepth = m_environmentSettings.waterLevel - sceneLocalCameraPos.y;
                if (underwaterDepth > 0.01f)
                {
                    enableUnderwater = true;
                    m_cameraUnderwater = true;
                }
            }

            if (enableUnderwater || wantCinematic)
            {
                m_usePostProcessed = true;

                glm::vec2 sunUv(0.5f, -0.2f);
                float sunVisible = 0.0f;
                if (enableUnderwater)
                {
                    const glm::vec3 lightDirLocal = glm::normalize(m_environmentSettings.terrainLightDirection);
                    const float skyRadius = std::max(m_environmentSettings.skydomeRadius, 1.0f);
                    const glm::vec3 sunLocal = lightDirLocal * (skyRadius * 0.92f);
                    const glm::vec4 sunClip = projection * view * world * glm::vec4(sunLocal, 1.0f);
                    if (sunClip.w > 0.0001f)
                    {
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
        glQueryCounter(totalTimestampQueries[1], GL_TIMESTAMP);

        GLuint64 queryResultNs[static_cast<int>(GpuProfileSlot::Count) - 1] = {};
        for (int queryIndex = 0; queryIndex < static_cast<int>(GpuProfileSlot::Count) - 1; ++queryIndex)
        {
            glGetQueryObjectui64v(gpuQueries[queryIndex], GL_QUERY_RESULT, &queryResultNs[queryIndex]);
            gpuProfileAccumMs[queryIndex] += static_cast<double>(queryResultNs[queryIndex]) / 1000000.0;
        }
        GLuint64 totalStartNs = 0;
        GLuint64 totalEndNs = 0;
        glGetQueryObjectui64v(totalTimestampQueries[0], GL_QUERY_RESULT, &totalStartNs);
        glGetQueryObjectui64v(totalTimestampQueries[1], GL_QUERY_RESULT, &totalEndNs);
        gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Total)] += static_cast<double>(totalEndNs - totalStartNs) / 1000000.0;
        glDeleteQueries(static_cast<GLsizei>(GpuProfileSlot::Count) - 1, gpuQueries);
        glDeleteQueries(2, totalTimestampQueries);

        // Profile reporting every 2 seconds
        perfFrameCount++;
        const auto currentTime = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(currentTime - lastPerfTime).count() >= 2.0) {
            const double avgFrames = std::max(perfFrameCount, 1);
            fprintf(stdout, "[Renderer GPU Profiling] Skydome: %.2f ms, Terrain: %.2f ms, Water: %.2f ms, Import: %.2f ms, Weather: %.2f ms, Total: %.2f ms (avg per frame in 2s)\n",
                    gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Skydome)] / avgFrames,
                    gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Terrain)] / avgFrames,
                    gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Water)] / avgFrames,
                    gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Import)] / avgFrames,
                    gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Weather)] / avgFrames,
                    gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Total)] / avgFrames);
            for (double &accumulatedMs : gpuProfileAccumMs)
            {
                accumulatedMs = 0.0;
            }
            perfFrameCount = 0;
            lastPerfTime = currentTime;
        }
    }

    void Renderer::createGridResources()
    {
        constexpr int halfGrid = 20;
        constexpr float spacing = 1.0f;
        constexpr float y = -0.75f;

        std::vector<float> vertices;
        vertices.reserve(halfGrid * 2 * 12 + 12);

        // Regular grid lines: skip the center axis lines, drawn separately with colors.
        for (int i = -halfGrid; i <= halfGrid; ++i)
        {
            if (i == 0)
            {
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

    void Renderer::createEnvironmentResources()
    {
        // Skydome sphere geometry.
        constexpr int lonSegments = 128;
        constexpr int latSegments = 64;
        std::vector<float> skyVertices;
        std::vector<unsigned int> skyIndices;
        skyVertices.reserve(static_cast<std::size_t>((lonSegments + 1) * (latSegments + 1)) * 3);
        skyIndices.reserve(static_cast<std::size_t>(lonSegments * latSegments) * 6);

        for (int y = 0; y <= latSegments; ++y)
        {
            const float v = static_cast<float>(y) / static_cast<float>(latSegments);
            const float theta = v * 3.1415926535f;
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            for (int x = 0; x <= lonSegments; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(lonSegments);
                const float phi = u * 6.283185307f;
                const float sinPhi = std::sin(phi);
                const float cosPhi = std::cos(phi);

                skyVertices.push_back(cosPhi * sinTheta);
                skyVertices.push_back(cosTheta);
                skyVertices.push_back(sinPhi * sinTheta);
            }
        }

        for (int y = 0; y < latSegments; ++y)
        {
            for (int x = 0; x < lonSegments; ++x)
            {
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

        // Generate precomputed star list (matches shader logic)
        m_starList = generateStarList(16, 12); // 16x12 grid, ~100 stars

        // Cloud geometry: single rectangular card centered at origin.
        std::vector<float> cloudVertices;
        std::vector<unsigned int> cloudIndices;
        cloudVertices.reserve(4 * 6);
        cloudIndices.reserve(6);

        const glm::vec3 points[4] = {
            glm::vec3(-0.72f, -0.48f, 0.0f),
            glm::vec3(0.72f, -0.48f, 0.0f),
            glm::vec3(0.72f, 0.48f, 0.0f),
            glm::vec3(-0.72f, 0.48f, 0.0f),
        };
        const glm::vec2 uvs[4] = {
            glm::vec2(-1.0f, -1.0f),
            glm::vec2(1.0f, -1.0f),
            glm::vec2(1.0f, 1.0f),
            glm::vec2(-1.0f, 1.0f),
        };
        for (int i = 0; i < 4; ++i)
        {
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
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), reinterpret_cast<void *>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), reinterpret_cast<void *>(5 * sizeof(float)));
        glEnableVertexAttribArray(2);
        m_cloudIndexCount = static_cast<int>(cloudIndices.size());

        // Subdivided terrain grid so vertex displacement produces actual height variation.
        {
            constexpr int kTerrainN = 160;
            constexpr float kTerrainSize = 220.0f;
            const float terrainStep = kTerrainSize * 2.0f / static_cast<float>(kTerrainN);
            std::vector<unsigned int> terrainIndices;
            terrainIndices.reserve(static_cast<std::size_t>(kTerrainN * kTerrainN) * 6);

            // Generate indices
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

            // Generate heightmap using DiamondSquareTerrain
            float patchScale = m_environmentSettings.terrainPatchScale;
            float roughness = m_environmentSettings.terrainRoughness;
            float seed = 42.0f; // Could be randomized or exposed as a setting
            DiamondSquareTerrain diamondTerrain(kTerrainN + 1, roughness, seed);

            // Each vertex: 3 pos, 3 normal, 2 uv = 8 floats
            std::vector<float> terrainVerticesFull;
            terrainVerticesFull.reserve((kTerrainN + 1) * (kTerrainN + 1) * 8);
            // Precompute positions for normal calculation
            std::vector<glm::vec3> positions((kTerrainN + 1) * (kTerrainN + 1));
            for (int row = 0; row <= kTerrainN; ++row) {
                for (int col = 0; col <= kTerrainN; ++col) {
                    float x = -kTerrainSize + static_cast<float>(col) * terrainStep;
                    float z = -kTerrainSize + static_cast<float>(row) * terrainStep;
                    // Sample height from diamond-square, scale by patchScale
                    float y = diamondTerrain.get(col, row) * patchScale * 20.0f; // 20.0f is an arbitrary amplitude scale
                    positions[row * (kTerrainN + 1) + col] = glm::vec3(x, y, z);
                }
            }
            for (int row = 0; row <= kTerrainN; ++row) {
                for (int col = 0; col <= kTerrainN; ++col) {
                    int idx = row * (kTerrainN + 1) + col;
                    glm::vec3 pos = positions[idx];
                    // --- Normal calculation (central differences) ---
                    glm::vec3 left = positions[idx - (col > 0 ? 1 : 0)];
                    glm::vec3 right = positions[idx + (col < kTerrainN ? 1 : 0)];
                    glm::vec3 down = positions[idx - (row > 0 ? (kTerrainN + 1) : 0)];
                    glm::vec3 up = positions[idx + (row < kTerrainN ? (kTerrainN + 1) : 0)];
                    glm::vec3 dx = right - left;
                    glm::vec3 dz = up - down;
                    glm::vec3 normal = glm::normalize(glm::cross(dz, dx));
                    // --- World-space UVs: anchor texture to world, not grid ---
                    float u = (pos.x + kTerrainSize) / (2.0f * kTerrainSize); // [0,1] across world X
                    float v = (pos.z + kTerrainSize) / (2.0f * kTerrainSize); // [0,1] across world Z
                    // --- Store vertex ---
                    terrainVerticesFull.push_back(pos.x);
                    terrainVerticesFull.push_back(pos.y);
                    terrainVerticesFull.push_back(pos.z);
                    terrainVerticesFull.push_back(normal.x);
                    terrainVerticesFull.push_back(normal.y);
                    terrainVerticesFull.push_back(normal.z);
                    terrainVerticesFull.push_back(u);
                    terrainVerticesFull.push_back(v);
                }
            }
            glGenVertexArrays(1, &m_terrainVao);
            glGenBuffers(1, &m_terrainVbo);
            glGenBuffers(1, &m_terrainEbo);
            glBindVertexArray(m_terrainVao);
            glBindBuffer(GL_ARRAY_BUFFER, m_terrainVbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(terrainVerticesFull.size() * sizeof(float)), terrainVerticesFull.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrainEbo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<long long>(terrainIndices.size() * sizeof(unsigned int)), terrainIndices.data(), GL_STATIC_DRAW);
            // Attribute 0: position (vec3)
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * static_cast<int>(sizeof(float)), reinterpret_cast<void *>(0));
            glEnableVertexAttribArray(0);
            // Attribute 1: normal (vec3)
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * static_cast<int>(sizeof(float)), reinterpret_cast<void *>(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            // Attribute 2: uv (vec2)
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * static_cast<int>(sizeof(float)), reinterpret_cast<void *>(6 * sizeof(float)));
            glEnableVertexAttribArray(2);
            m_terrainIndexCount = static_cast<int>(terrainIndices.size());
        }

        // Water plane – 128×128 subdivided grid for Gerstner wave geometry
        {
            constexpr int kWaterN = 128;
            constexpr float kTerrainSize = 220.0f; // Match terrain size
            constexpr float kStep = kTerrainSize * 2.0f / static_cast<float>(kWaterN);

            std::vector<float> wVerts;
            std::vector<unsigned int> wIdx;
            wVerts.reserve(static_cast<std::size_t>((kWaterN + 1) * (kWaterN + 1)) * 3);
            wIdx.reserve(static_cast<std::size_t>(kWaterN * kWaterN) * 6);

            for (int row = 0; row <= kWaterN; ++row)
            {
                for (int col = 0; col <= kWaterN; ++col)
                {
                    wVerts.push_back(-kTerrainSize + static_cast<float>(col) * kStep);
                    wVerts.push_back(0.0f);
                    wVerts.push_back(-kTerrainSize + static_cast<float>(row) * kStep);
                }
            }
            for (int row = 0; row < kWaterN; ++row)
            {
                for (int col = 0; col < kWaterN; ++col)
                {
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

    void Renderer::createFramebuffer()
    {
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

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
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

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            throw std::runtime_error("Post-process framebuffer is not complete.");
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Renderer::destroyFramebuffer()
    {
        if (m_depthStencilRbo != 0)
        {
            glDeleteRenderbuffers(1, &m_depthStencilRbo);
            m_depthStencilRbo = 0;
        }

        if (m_colorTexture != 0)
        {
            glDeleteTextures(1, &m_colorTexture);
            m_colorTexture = 0;
        }

        if (m_fbo != 0)
        {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }

        if (m_postColorTexture != 0)
        {
            glDeleteTextures(1, &m_postColorTexture);
            m_postColorTexture = 0;
        }

        if (m_postFbo != 0)
        {
            glDeleteFramebuffers(1, &m_postFbo);
            m_postFbo = 0;
        }
    }

    void Renderer::rebuildFramebufferIfNeeded(const int width, const int height)
    {
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

} // namespace sparks::render
