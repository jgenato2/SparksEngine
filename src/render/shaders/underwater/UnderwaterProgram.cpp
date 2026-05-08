#include "sparks/render/shaders/underwater/UnderwaterProgram.hpp"
#include "sparks/render/GLUtils.hpp"

namespace sparks::render {

// Fullscreen post-process pass: underwater distortion, caustics, god rays,
// depth fog, and optional cinematic colour grade + letterbox.
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
            float t = uTime;
            float rawDepth = max(uDepth, 0.0);
            float depth = max(rawDepth - 0.30, 0.0);

            vec3 scene = texture(uSceneTex, vUv).rgb;
            vec3 outColor = scene;

            if (uUnderwaterEnabled == 1) {

            // ── Wave distortion of the scene (refraction effect) ──────────────
            // Strength scales softly from zero to a maximum of 0.014 as depth grows
            float distStr = clamp(depth * 0.045, 0.0, 0.0100);
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

            // ── Vignette ─────────────────────────────────────────────────────
            vec2 vigUv = vUv * 2.0 - 1.0;
            float vign = 1.0 - clamp(dot(vigUv * 0.58, vigUv * 0.58), 0.0, 1.0);
            vign = mix(0.22, 1.0, vign * vign);
            tinted *= vign;

            // ── Depth fog – exponential darkening/murk as camera goes deeper ─
            float fog = exp(-depth * 0.22);
            tinted = mix(tint * 0.10, tinted, fog);

            // ── Near-surface brightness flicker when barely submerged ─────────
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

} // namespace sparks::render
