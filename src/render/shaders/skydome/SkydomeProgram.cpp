#include "sparks/render/shaders/skydome/SkydomeProgram.hpp"
#include "sparks/render/GLUtils.hpp"
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
        uniform float uCloudSpeed;
        uniform float uCloudShadowStrength;
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

        // 2-octave FBM — sufficient for sky clouds, half the cost of 4 octaves
        float fbm2Sky(vec2 p) {
            float v = 0.0;
            float a = 0.55;
            mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
            for (int i = 0; i < 2; ++i) {
                v += a * noise2Sky(p);
                p = rot * p * 2.02 + vec2(1.37, -0.91);
                a *= 0.5;
            }
            return v;
        }

        // Ultra-fast hash-based procedural starfield (no loop)
        float starFieldHash(vec3 dir, float viewTheta, float viewPhi, out float haloOut, out vec3 starColor) {
            // Early-out: stars only in upper hemisphere — skip all trig for lower half
            if (viewTheta > 1.5707963) {
                haloOut = 0.0;
                starColor = vec3(1.0);
                return 0.0;
            }
            if (viewPhi < 0.0) viewPhi += 6.2831853;
            float core = 0.0;
            float halo = 0.0;
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
            float starRadius = mix(0.000008, 0.000018, sizeRand); // clamp upper bound further
            float discEdge = starRadius * mix(1.03, 1.08, sizeRand); // even tinier halo

            // Twinkle: random phase and amplitude per star
            float t = uTime;
            float twinklePhase = hash21(tile + 5.7) * 6.2831853; // [0, 2pi]
            float twinkleAmp = mix(0.5, 1.0, shineRand); // random twinkle amplitude
            float twinkle = 0.7 + twinkleAmp * sin(t * 0.2 + twinklePhase); // much slower twinkle
            // Color variation per tile
            float c1 = hash21(tile + 1.3);
            float c2 = hash21(tile + 2.7);
            starColor = vec3(1.0, 0.95 + 0.1 * c1, 0.9 + 0.2 * c2);
            float coreDisc = smoothstep(starRadius, starRadius * 0.7, angularDist);
                float glareStrength = mix(0.5, 1.2, shineRand); // much lower glare for bloom
                float coreStrength = mix(1.0, 2.0, shineRand); // much lower core for bloom
                float haloStrength = mix(0.2, 0.5, shineRand); // much lower halo

            // --- 4-point star glare --- reuse passed-in spherical coords
            float glareTheta = viewTheta;
            float glarePhi = viewPhi < 0.0 ? viewPhi + 6.2831853 : viewPhi;
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
            starburst *= 4.0 * twinkle * glareStrength;

                float glare = pow(coreDisc, 16.0) * glareStrength + starburst;
                // Add a soft, wide halo for extra bloom
                float wideHalo = smoothstep(0.008, 0.003, angularDist) * 0.08 * twinkle;
            core = (coreDisc * coreStrength + glare) * twinkle;
                halo = (smoothstep(discEdge, starRadius, angularDist) * haloStrength + wideHalo) * twinkle;
            haloOut = halo;
            return core;
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

            // Use the configured sky gradient instead of forcing a saturated blue zenith.
            vec3 skyGradient = mix(uHorizonColor, uZenithColor, pow(h, 0.72));
            vec3 color = skyGradient;

            // Environmental bloom: brighten sky near sun based on sun intensity
            float envBloom = pow(sunDot, 8.0) * clamp(uSunIntensity, 0.0, 2.0);
            color += uSunColor * envBloom * 0.5;

            // Dust haze: stronger near horizon and slightly forward-scattered toward the sun.
            float dust = clamp(uDustAmount, 0.0, 1.5);
            float horizonDust = (1.0 - h) * dust;
            float sunDust = pow(sunDot, 2.8) * dust * 0.55;
            color = mix(color, uDustColor, clamp(horizonDust * 0.65, 0.0, 1.0));
            color += uDustColor * sunDust * 0.22;

            // --- Fluffy, pure white clouds ---
            float minY = 0.18;
            float blend = smoothstep(0.0, minY * 2.0, abs(skyDir.y));
            float safeY = mix(minY, abs(skyDir.y), blend);
            vec2 zenithUV = skyDir.xz / safeY;
            vec2 horizonUV = normalize(skyDir.xz) * (1.0 - abs(skyDir.y));
            vec2 cloudUV = mix(horizonUV, zenithUV, blend);
            // Clouds: skip expensive FBM when cloud amount is zero
            float cumAlpha = 0.0;
            if (uCloudAmount > 0.01) {
            float cloudSc = 0.038 * uCloudScale;
            vec2 uv0 = cloudUV * cloudSc;
            float timeShift = uTime * uCloudSpeed;
            float fbm1 = fbm2Sky(uv0 + vec2(timeShift, 0.0));
            float fbm2 = fbm2Sky(uv0 * 2.3 + vec2(timeShift * 0.5, 0.0));
            float fbm3 = fbm2Sky(uv0 * 4.1 + vec2(timeShift * 0.2, 0.0));
            float cloudShape = (fbm1 * 0.50 + fbm2 * 0.32 + fbm3 * 0.18); // rebalanced for 3 layers
            float cloudCoverage = 0.44 + 0.18 * (1.0 - clamp(uCloudAmount, 0.0, 1.5));
            // Softer, rounder edge for fluffy look
            cumAlpha = pow(smoothstep(cloudCoverage, cloudCoverage + 0.10, cloudShape), 1.08);
            } // end cloud FBM block
            float shadowStrength = clamp(uCloudShadowStrength, 0.0, 1.0);
            // Soft blue shadow, no gray
            vec3 shadowBase = mix(color, uZenithColor, 0.35);
            // Volumetric: bright white tops, soft blue bottoms
            float upness = clamp(dot(skyDir, vec3(0.0, 1.0, 0.0)), 0.0, 1.0);
            // Add multi-scattering tint for cotton candy effect
            float scatterTint = 0.18 * (1.0 - upness) * cumAlpha;
            vec3 pinkTint = vec3(1.0, 0.82, 0.92);
            vec3 blueTint = vec3(0.85, 0.92, 1.0);
            vec3 cloudTop = mix(vec3(1.0, 1.0, 1.0), pinkTint, scatterTint * 0.7);
            vec3 cloudBottom = mix(vec3(0.92, 0.95, 0.99), blueTint, scatterTint * 0.45);
            vec3 cloudColor = mix(cloudBottom, cloudTop, upness);
            // Add extra contrast for depth
            cloudColor = mix(cloudColor, vec3(1.0), pow(upness, 2.5) * 0.25);

            // --- Backlighting: clouds in front of sun are darker ---
            float viewToSun = dot(skyDir, sunDir); // 1 = looking at sun, -1 = looking away
            float backlight = smoothstep(0.2, 0.95, viewToSun); // 0 = away from sun, 1 = directly at sun
            float backShadow = cumAlpha * backlight * 0.85; // strong effect for thick clouds
            vec3 backShadowTint = mix(vec3(0.48, 0.48, 0.50), uZenithColor, 0.18);
            cloudColor = mix(cloudColor, backShadowTint, backShadow);

            // Blend cloud color with sky using alpha
            color = mix(color, cloudColor, cumAlpha * clamp(uCloudAmount, 0.0, 1.5));
            // Add strong shadow to cloud base only (not inside cloud)
            float shadowImpact = cumAlpha * shadowStrength * (1.0 - upness) * 1.5; // was 0.55, now 1.5 for much stronger effect
            shadowImpact = clamp(shadowImpact, 0.0, 1.0);
            color = mix(color, shadowBase, shadowImpact);

            // Sun as a soft disc with halo and diamond/cross glare (like star)
            float sizeN = clamp((uSunDiscSize - 0.2) / 7.8, 0.0, 1.0);
            float sunRadius = mix(0.012, 0.035, sizeN) * 0.10;
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
            // Pre-compute spherical coords once for both stars and (if needed) glare
            float viewTheta = acos(clamp(skyDir.y, -1.0, 1.0));
            float viewPhi   = atan(skyDir.z, skyDir.x);
            // Early-out: skip all star trig in lower hemisphere
            float star = 0.0;
            if (viewTheta <= 1.5707963)
                star = starFieldHash(skyDir, viewTheta, viewPhi, halo, starColor);
            // Add star and halo using max() so they are never dimmed by the background
            vec3 starGlow = max(star * starColor * 2.5, halo * starColor * 2.8);
            color = max(color, starGlow);

            FragColor = vec4(color, 1.0);
        }
    )";

    return GLUtils::createProgram(kVertexShader, kFragmentShader);
}

} // namespace sparks::render
