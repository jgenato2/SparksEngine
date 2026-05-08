#include <ctime>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include "sparks/render/terrain/DiamondSquareTerrain.hpp"
#include "sparks/render/Renderer.hpp"
#include "sparks/render/RendererAstronomy.hpp"
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "sparks/render/CloudShadowMap.hpp"

namespace sparks::render
{

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
constexpr float kWaterHalfExtent = 220.0f;

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

// â”€â”€ Underwater post-process program â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
// Renders a fullscreen triangle (gl_VertexID trick, no VBO) and applies:
//   wave-distorted UV sampling, chromatic aberration, animated FBM caustics,
//   depth-based colour tint/fog, and a vignette.
unsigned int createUnderwaterPostProcessProgram()
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

        // â”€â”€ Noise helpers (caustics) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
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
            float t = uTime;
            float rawDepth = max(uDepth, 0.0);
            float depth = max(rawDepth - 0.30, 0.0);

            vec3 scene = texture(uSceneTex, vUv).rgb;
            vec3 outColor = scene;

            if (uUnderwaterEnabled == 1) {

            // â”€â”€ Wave distortion of the scene (refraction effect) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
            // Strength scales softly from zero to a maximum of 0.014 as depth grows
            float distStr = clamp(depth * 0.045, 0.0, 0.0100);
            vec2 distort;
            distort.x = sin(vUv.y * 4.5 + t * 0.55) * sin(vUv.x * 3.0 + t * 0.32);
            distort.y = cos(vUv.x * 4.0 + t * 0.48) * cos(vUv.y * 3.2 + t * 0.40);
            distort *= distStr;

            // â”€â”€ Chromatic aberration (colour channels sample at slight offsets) â”€
            float aberr = clamp(depth * 0.0018, 0.0, 0.0030);
            vec2 uv = vUv + distort;
            float sceneR = texture(uSceneTex, uv + vec2( aberr,  0.0)).r;
            float sceneG = texture(uSceneTex, uv).g;
            float sceneB = texture(uSceneTex, uv - vec2( aberr,  0.0)).b;
            scene = vec3(sceneR, sceneG, sceneB);

            // â”€â”€ Caustics â€“ bright animated light patches from sun above waves â”€â”€
            // Fade caustics away as camera descends deeper
            float causticStr = clamp(1.0 - depth * 0.28, 0.0, 1.0);
            vec2 cuv = vUv * 2.80;
            float cA = fbm3Water(cuv + vec2( t * 0.18,  t * 0.12));
            float cB = fbm3Water(cuv + vec2(-t * 0.12,  t * 0.16) + vec2(3.4, 1.2));
            float cC = fbm3Water(cuv * 0.72 + vec2( t * 0.08, -t * 0.09) + vec2(7.1, 5.4));
            float caustic = smoothstep(0.74, 0.96, (cA + cB) * 0.50 + cC * 0.25)
                          * causticStr * 0.30;

            // â”€â”€ Colour tint â€“ deeper water is colder and darker â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
            float tintAmt = clamp(depth * 0.30, 0.0, 0.82);
            vec3 deepColor = vec3(0.01, 0.06, 0.26);
            // Near surface is nudged slightly toward cyan-blue, then transitions
            // to a deeper ocean blue as depth increases.
            vec3 nearBlue = mix(uWaterTint, vec3(0.06, 0.44, 0.68), 0.50);
            vec3 tint = mix(nearBlue, deepColor, clamp(depth * 0.12, 0.0, 1.0));
            vec3 tinted = mix(scene, tint, tintAmt);

            // Add caustic shimmer
            tinted += vec3(0.14, 0.34, 0.52) * caustic;

            // Directional underwater god rays from the visible sun in screen space.
            vec2 sunToScene = vec2(0.5, 0.10) - uSunUv;
            vec2 sunDir2 = sunToScene / max(length(sunToScene), 0.0001);
            float rayA = godRayMask(vUv, uSunUv, sunDir2, t);
            float rayB = godRayMask(vUv, uSunUv + vec2(0.035, -0.010), normalize(sunDir2 + vec2(0.12, 0.06)), t + 1.7);
            float godRays = (rayA * 0.72 + rayB * 0.46)
                          * clamp(1.0 - depth * 0.22, 0.0, 1.0)
                          * uSunVisible;
            tinted += vec3(0.14, 0.34, 0.58) * godRays;

            // â”€â”€ Vignette â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
            vec2 vigUv = vUv * 2.0 - 1.0;
            float vign = 1.0 - clamp(dot(vigUv * 0.58, vigUv * 0.58), 0.0, 1.0);
            vign = mix(0.22, 1.0, vign * vign);
            tinted *= vign;

            // â”€â”€ Depth fog â€“ exponential darkening/murk as camera goes deeper â”€
            float fog = exp(-depth * 0.22);
            tinted = mix(tint * 0.10, tinted, fog);

            // â”€â”€ Near-surface brightness flicker when barely submerged â”€â”€â”€â”€â”€â”€â”€â”€â”€
            float surfaceGlow = smoothstep(0.7, 0.0, depth) * 0.06
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
        m_shaderProgram = ShaderFacade::CreateObjectProgram();
        m_texturedProgram = ShaderFacade::CreateTexturedProgram();
        m_skydomeProgram = ShaderFacade::CreateSkydomeProgram();
        m_terrainProgram = ShaderFacade::CreateTerrainProgram();
        m_waterProgram = ShaderFacade::CreateWaterProgram();
        m_cloudProgram = ShaderFacade::CreateCloudProgram();
        m_underwaterProgram = ShaderFacade::CreateUnderwaterProgram();

        // Only disable cloud objects, allow procedural cloud formation
        m_environmentSettings.enableCloudObjects = false;
        if (!m_environmentSettings.cloudObjects.empty())
        {
            for (auto &cloud : m_environmentSettings.cloudObjects)
            {
                cloud.enabled = false;
            }
        }
        // Fullscreen triangle VAO â€“ no buffers, vertex positions generated from gl_VertexID
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
            settings.waterHalfExtent != m_environmentSettings.waterHalfExtent ||
            settings.waterColor != m_environmentSettings.waterColor ||
            settings.waterOpacity != m_environmentSettings.waterOpacity ||
            settings.waveAmplitude != m_environmentSettings.waveAmplitude ||
            settings.waveFrequency != m_environmentSettings.waveFrequency ||
            settings.waterFoamIntensity != m_environmentSettings.waterFoamIntensity;

        m_environmentSettings = settings;
        // Only disable cloud objects, allow procedural cloud formation
        m_environmentSettings.enableCloudObjects = false;
        if (!m_environmentSettings.cloudObjects.empty())
        {
            for (auto &cloud : m_environmentSettings.cloudObjects)
            {
                cloud.enabled = false;
            }
        }
        m_environmentSettings.skydomeRadius = glm::clamp(m_environmentSettings.skydomeRadius, 20.0f, 4000.0f);
        m_environmentSettings.skyCloudAmount = glm::clamp(m_environmentSettings.skyCloudAmount, 0.0f, 1.5f);
        m_environmentSettings.skyCloudScale = glm::clamp(m_environmentSettings.skyCloudScale, 0.1f, 8.0f);
        m_environmentSettings.sunDiscSize = glm::clamp(m_environmentSettings.sunDiscSize, 0.2f, 8.0f);
        m_environmentSettings.sunIntensity = glm::clamp(m_environmentSettings.sunIntensity, 0.0f, 4.0f);
        m_environmentSettings.waterSunStrength = glm::clamp(m_environmentSettings.waterSunStrength, 0.0f, 6.0f);
        m_environmentSettings.waterReflectionStrength = glm::clamp(m_environmentSettings.waterReflectionStrength, 0.0f, 3.0f);
        m_environmentSettings.sunHeatStrength = glm::clamp(m_environmentSettings.sunHeatStrength, 0.0f, 3.0f);
        m_environmentSettings.dustAmount = glm::clamp(m_environmentSettings.dustAmount, 0.0f, 1.5f);
        m_environmentSettings.sunRayStrength = glm::clamp(m_environmentSettings.sunRayStrength, 0.0f, 2.5f);
        m_environmentSettings.lensFlareStrength = glm::clamp(m_environmentSettings.lensFlareStrength, 0.0f, 2.5f);

        // Clamp fogNear and fogFar with safety
        m_environmentSettings.fogNear = glm::clamp(m_environmentSettings.fogNear, 1.0f, 500.0f);
        float fogFarMin = m_environmentSettings.fogNear + 1.0f;
        float fogFarMax = 900.0f;
        if (fogFarMin > fogFarMax)
        {
            m_environmentSettings.fogFar = fogFarMin;
        }
        else
        {
            m_environmentSettings.fogFar = glm::clamp(m_environmentSettings.fogFar, fogFarMin, fogFarMax);
        }
        m_environmentSettings.fogStrength = glm::clamp(m_environmentSettings.fogStrength, 0.0f, 1.0f);
        m_environmentSettings.waterHalfExtent = glm::clamp(m_environmentSettings.waterHalfExtent, 20.0f, 4000.0f);

        // Clamp terrain
        float terrainSizeMin = 20.0f, terrainSizeMax = 4000.0f;
        if (terrainSizeMin > terrainSizeMax)
        {
            m_environmentSettings.terrainSize = terrainSizeMin;
        }
        else
        {
            m_environmentSettings.terrainSize = glm::clamp(m_environmentSettings.terrainSize, terrainSizeMin, terrainSizeMax);
        }
        float terrainHeightMin = -20.0f, terrainHeightMax = 20.0f;
        if (terrainHeightMin > terrainHeightMax)
        {
            m_environmentSettings.terrainHeight = terrainHeightMin;
        }
        else
        {
            m_environmentSettings.terrainHeight = glm::clamp(m_environmentSettings.terrainHeight, terrainHeightMin, terrainHeightMax);
        }
        float terrainPatchScaleMin = 0.01f, terrainPatchScaleMax = 4.0f;
        if (terrainPatchScaleMin > terrainPatchScaleMax)
        {
            m_environmentSettings.terrainPatchScale = terrainPatchScaleMin;
        }
        else
        {
            m_environmentSettings.terrainPatchScale = glm::clamp(m_environmentSettings.terrainPatchScale, terrainPatchScaleMin, terrainPatchScaleMax);
        }
        float terrainRoughnessMin = 0.0f, terrainRoughnessMax = 3.0f;
        if (terrainRoughnessMin > terrainRoughnessMax)
        {
            m_environmentSettings.terrainRoughness = terrainRoughnessMin;
        }
        else
        {
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
        if (terrainChanged || waterChanged)
        {
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

} // namespace sparks::render
