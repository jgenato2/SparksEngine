#include "sparks/render/shaders/underwater/UnderwaterProgram.hpp"
#include "sparks/render/GLUtils.hpp"

namespace sparks::render {

// Fullscreen post-process pass: underwater distortion, undersea gradient tint,
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
        uniform float     uDeepSeaDepth; // metres at which full deep-sea effect is reached
        uniform vec3      uWaterTint;   // configurable base water colour
        uniform vec3      uDeepColor;   // darkness tint colour at full depth
        uniform int       uUnderwaterEnabled;
        uniform int       uCinematicEnabled;

        float hash21(vec2 p) {
            p = fract(p * vec2(127.1, 311.7));
            p += dot(p, p + 184.75);
            return fract(p.x * p.y);
        }

        void main() {
            float t = uTime;
            float rawDepth = max(uDepth, 0.0);
            float depth = max(rawDepth - 0.30, 0.0);
            // nd: normalised depth — 0 at surface, 1 at full deep-sea effect
            float nd = clamp(depth / max(uDeepSeaDepth, 0.5), 0.0, 1.0);

            vec3 scene = texture(uSceneTex, vUv).rgb;
            vec3 outColor = scene;

            if (uUnderwaterEnabled == 1) {

            // ── Wave distortion of the scene (refraction effect) ──────────────
            // Peaks at nd=0.22, stays at max beyond that.
            float distStr = clamp(nd * 0.045, 0.0, 0.0100);
            vec2 distort;
            distort.x = sin(vUv.y * 4.5 + t * 0.55) * sin(vUv.x * 3.0 + t * 0.32);
            distort.y = cos(vUv.x * 4.0 + t * 0.48) * cos(vUv.y * 3.2 + t * 0.40);
            distort *= distStr;

            // ── Chromatic aberration (colour channels sample at slight offsets) ─
            float aberr = clamp(nd * 0.003, 0.0, 0.0030);
            vec2 uv = vUv + distort;
            float sceneR = texture(uSceneTex, uv + vec2( aberr,  0.0)).r;
            float sceneG = texture(uSceneTex, uv).g;
            float sceneB = texture(uSceneTex, uv - vec2( aberr,  0.0)).b;
            scene = vec3(sceneR, sceneG, sceneB);

            // ── Undersea gradient tint (no cloud-like noise) ──────────────────
            // Vertical gradient: brighter near the top of the frame, darker at
            // the lower part to emulate seabed murk.
            vec3 deepColor = uDeepColor;
            vec3 nearBlue = mix(uWaterTint, vec3(0.06, 0.44, 0.68), 0.50);
            vec3 depthTint = mix(nearBlue, deepColor, clamp(nd, 0.0, 1.0));
            float verticalT = smoothstep(0.05, 0.98, vUv.y);
            vec3 gradientTint = mix(depthTint * 1.10, depthTint * 0.30, verticalT);
            float tintAmt = clamp(0.28 + nd * 0.58, 0.0, 0.88);
            vec3 tinted = mix(scene, gradientTint, tintAmt);

            // ── Vignette ─────────────────────────────────────────────────────
            vec2 vigUv = vUv * 2.0 - 1.0;
            float vign = 1.0 - clamp(dot(vigUv * 0.58, vigUv * 0.58), 0.0, 1.0);
            vign = mix(0.22, 1.0, vign * vign);
            tinted *= vign;

            // ── Depth fog – exponential darkening; near-black at nd=1 ────────
            // exp(-5.0) ≈ 0.007, so the scene is essentially black at full depth.
            float fog = exp(-nd * 5.0);
            tinted = mix(deepColor * 0.02, tinted, fog);

            // ── Near-surface brightness flicker when barely submerged ─────────
            float surfaceGlow = smoothstep(0.15, 0.0, nd) * 0.06
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
