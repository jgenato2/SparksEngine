#include "SkydomeProgram.hpp"
#include "../../GLUtils.hpp"
#include <glad/gl.h>
#include <string>

namespace sparks::render {

unsigned int createSkydomeProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;

        uniform mat4 uMvp;
        uniform mat3 uWorldRot;
        out vec3 vWorldDir;
        out vec3 vLocalPos;
        invariant gl_Position;
        void main() {
            vLocalPos = aPos;
            vWorldDir = uWorldRot * aPos;
            gl_Position = uMvp * vec4(aPos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;
        in vec3 vWorldDir;
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
        // Remove uStarDensity, use fixed star count

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

        // Ultra-fast hash-based procedural starfield (no loop)
        float starFieldHash(vec3 dir, out float haloOut, out vec3 starColor) {
            // Project direction to spherical coordinates
            float phi = atan(dir.z, dir.x);
            float theta = acos(clamp(dir.y, -1.0, 1.0));
            // Map to [0,1]
            vec2 uv = vec2(phi / (2.0 * 3.14159265) + 0.5, theta / 3.14159265);
            // Scale up for more randomness
            uv *= 512.0;
            float h = hash21(uv);
            // Color variation per star
            float colorSeed = hash21(uv + 17.0);
            starColor = mix(vec3(1.0, 0.95, 0.95), vec3(0.7, 0.85, 1.0), colorSeed); // white to blue
            // Patch: Render star as a soft disc (like sun), using angular distance from star center
            // Optimized: hash-based, no loop, disc-shaped stars
            // Tile-based: divide sky into grid, place one star per tile
            float core = 0.0;
            float halo = 0.0;
            // Convert direction to spherical coordinates
            float viewTheta = acos(clamp(dir.y, -1.0, 1.0)); // [0, pi]
            float viewPhi = atan(dir.z, dir.x); // [-pi, pi]
            if (viewPhi < 0.0) viewPhi += 6.2831853;
            // Grid size (tune for ~100 stars)
            const float TILE_U = 1.0 / 16.0; // 16 tiles horizontally
            const float TILE_V = 1.0 / 12.0; // 12 tiles vertically
            float u = viewPhi / 6.2831853;
            float v = viewTheta / 3.14159265;
            // Avoid seam: skip tiles near u=0 or u=1
            if (u < 0.04 || u > 0.96) {
                haloOut = 0.0;
                starColor = vec3(1.0);
                return 0.0;
            }
            float tileX = floor(u / TILE_U);
            float tileY = floor(v / TILE_V);
            vec2 tile = vec2(tileX, tileY);
            // Hash to get star position within tile, avoid edge
            vec2 starUV = (vec2(hash21(tile + 0.1), hash21(tile + 0.7)) * 0.6 + 0.2) * vec2(TILE_U, TILE_V);
            vec2 starPos = vec2(tileX * TILE_U, tileY * TILE_V) + starUV;
            // Convert starPos to direction
            float starPhi = starPos.x * 6.2831853;
            float starTheta = starPos.y * 3.14159265;
            vec3 starDir = vec3(cos(starPhi) * sin(starTheta), cos(starTheta), sin(starPhi) * sin(starTheta));
            float angularDist = acos(clamp(dot(dir, starDir), -1.0, 1.0));
            // Randomize star size and shine
            float sizeRand = hash21(tile + 8.3);
            float shineRand = hash21(tile + 9.7);
            float starRadius = mix(0.00004, 0.00010, sizeRand); // ultra tiny, far-away stars
            float discEdge = starRadius * mix(1.05, 1.18, sizeRand); // ultra tiny halo

            // Twinkle: random phase and amplitude per star
            float t = uTime;
            float twinklePhase = hash21(tile + 5.7) * 6.2831853; // [0, 2pi]
            float twinkleAmp = mix(0.5, 1.0, shineRand); // random twinkle amplitude
            float twinkle = 0.7 + twinkleAmp * sin(t * 0.7 + twinklePhase); // slower twinkle
            // Only render stars in upper dome
            if (viewTheta > 1.5707963) {
                haloOut = 0.0;
                starColor = vec3(1.0);
                return 0.0;
            }
            // Color variation per tile
            float c1 = hash21(tile + 1.3);
            float c2 = hash21(tile + 2.7);
            starColor = vec3(1.0, 0.95 + 0.1 * c1, 0.9 + 0.2 * c2);
            float coreDisc = smoothstep(starRadius, starRadius * 0.7, angularDist);
                float glareStrength = mix(2.0, 4.0, shineRand); // much lower glare for bloom
                float coreStrength = mix(3.0, 5.0, shineRand); // much lower core for bloom
                float haloStrength = mix(0.7, 1.5, shineRand); // much lower halo

            // --- 4-point star glare ---
            // Spherical coordinates for pixel and star
            float glareTheta = acos(clamp(dir.y, -1.0, 1.0));
            float glarePhi = atan(dir.z, dir.x);
            if (glarePhi < 0.0) glarePhi += 6.2831853;
            float starThetaG = acos(clamp(starDir.y, -1.0, 1.0));
            float starPhiG = atan(starDir.z, starDir.x);
            if (starPhiG < 0.0) starPhiG += 6.2831853;
            float dTheta = glareTheta - starThetaG;
            float dPhi = glarePhi - starPhiG;
            float angDist = sqrt(dTheta * dTheta + dPhi * dPhi);
            // True starburst: N arms using azimuthal angle
            float azimuth = atan(dTheta, dPhi); // angle around the star center
            float arms = 4.0; // 4 diamond arms (diagonals)
            float armSharpness = 18.0;
            float armWidth = 0.003 + starRadius * 0.7;
            // Diamond shape: emphasize diagonals using sine and cosine
            float diamondPattern = pow(abs(sin(azimuth * arms)), armSharpness);
            float starburst = pow(max(0.0, 1.0 - angDist / armWidth), 2.5) * diamondPattern;
            // Attenuate near the pole to avoid distortion
            float poleFade = smoothstep(0.0, 0.25, abs(starThetaG - 1.5707963));
            starburst *= poleFade;
            starburst *= 10.0 * twinkle * glareStrength;

                float glare = pow(coreDisc, 16.0) * glareStrength + starburst;
                // Add a soft, wide halo for extra bloom
                float wideHalo = smoothstep(0.008, 0.003, angularDist) * 0.18 * twinkle;
            core = (coreDisc * coreStrength + glare) * twinkle;
                halo = (smoothstep(discEdge, starRadius, angularDist) * haloStrength + wideHalo) * twinkle;
            haloOut = halo;
            return core * 4.0;
        }

        void main() {
            // Fallback: if both horizon and zenith color are near black, output magenta for debugging
            if (dot(uHorizonColor, uHorizonColor) < 0.01 && dot(uZenithColor, uZenithColor) < 0.01) {
                FragColor = vec4(1.0, 0.0, 1.0, 1.0);
                return;
            }

            float h = clamp(vLocalPos.y * 0.5 + 0.5, 0.0, 1.0);
            vec3 skyDir = normalize(vLocalPos);
            vec3 sunDir = normalize(uSunDir);
            float sunDot = max(dot(skyDir, sunDir), 0.0);

            // Sky gradient
            vec3 color = mix(uHorizonColor, uZenithColor, pow(h, 0.62));

            // Environmental bloom: brighten sky near sun based on sun intensity
            float envBloom = pow(sunDot, 8.0) * clamp(uSunIntensity, 0.0, 2.0); // sharper, more intense near sun
            color += uSunColor * envBloom * 0.5;

            // Clouds (simplified, no lighting for brevity)
            float yGuard = max(skyDir.y, 0.12);
            vec2 cloudPlane = skyDir.xz / yGuard;
            float cloudSc = 0.038 * uCloudScale;
            vec2 uv0 = cloudPlane * cloudSc;
            float cumBase = fbm4Sky(uv0);
            float cumAlpha = smoothstep(0.44, 0.58, cumBase);
            color = mix(color, uCloudColor, cumAlpha * clamp(uCloudAmount, 0.0, 1.5));

            // Sun as a soft disc with halo and diamond/cross glare (like star)
            float sizeN = clamp((uSunDiscSize - 0.2) / 7.8, 0.0, 1.0);
            float discOuter = mix(0.99978, 0.99908, sizeN);
            float discInner = mix(0.99995, 0.99935, sizeN);
            float sunRadius = mix(0.012, 0.035, sizeN); // sun angular radius (smaller base size)
            float sunDisc = smoothstep(sunRadius, sunRadius * 0.7, 1.0 - sunDot);
            float sunHalo = smoothstep(sunRadius * 1.2, sunRadius, 1.0 - sunDot);

            // Sun glare/starburst (diamond/cross)
            float glareTheta = acos(clamp(skyDir.y, -1.0, 1.0));
            float glarePhi = atan(skyDir.z, skyDir.x);
            if (glarePhi < 0.0) glarePhi += 6.2831853;
            float sunTheta = acos(clamp(sunDir.y, -1.0, 1.0));
            float sunPhi = atan(sunDir.z, sunDir.x);
            if (sunPhi < 0.0) sunPhi += 6.2831853;
            float dTheta = glareTheta - sunTheta;
            float dPhi = glarePhi - sunPhi;
            float angDist = sqrt(dTheta * dTheta + dPhi * dPhi);
            float azimuth = atan(dTheta, dPhi);
            float arms = 8.0;
            float armSharpness = 16.0;
            float armWidth = sunRadius * 1.2;
            float diamondPattern = pow(abs(sin(azimuth * arms)), armSharpness);
            float sunburst = pow(max(0.0, 1.0 - angDist / armWidth), 2.5) * diamondPattern;
            sunburst *= sunDisc * 0.7 * uSunIntensity;

            color += uSunColor * uSunIntensity * (
                sunDisc * 1.0 + // soft core
                sunHalo * 0.25   // soft halo
            ) + uSunColor * sunburst;

            // Add exactly 100 procedural stars
            float halo = 0.0;
                vec3 starColor = vec3(1.0);
                float star = starFieldHash(skyDir, halo, starColor);
                // Add star and halo using max() so they are never dimmed by the background
                vec3 starGlow = max(star * starColor * 2.5, halo * starColor * 2.8);
            color = max(color, starGlow);

            FragColor = vec4(color, 1.0);
        }
    )";

    return GLUtils::createProgram(kVertexShader, kFragmentShader);
}

} // namespace sparks::render
