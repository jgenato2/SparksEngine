#include "sparks/render/shaders/water/WaterProgram.hpp"
#include "sparks/render/GLUtils.hpp"
#include <glad/gl.h>
#include <string>

namespace sparks::render {

unsigned int createWaterProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;
        layout (location = 2) in vec2 aUv;

        uniform mat4 uMvp;
        uniform mat4 uModel;
        uniform float uTime;
        uniform float uWaveAmplitude;
        uniform float uWaveFrequency;
        out vec3 vWorldPos;
        out vec3 vWorldNormal;
        out vec2 vUv;

        // Gerstner wave parameters
        const int NUM_WAVES = 4;
        const float PI = 3.14159265;
        const vec2 directions[4] = vec2[4](
            normalize(vec2(1.0, 0.3)),
            normalize(vec2(-0.8, 0.6)),
            normalize(vec2(0.5, -1.0)),
            normalize(vec2(-0.6, -0.7))
        );
        const float wavelengths[4] = float[4](32.0, 24.0, 18.0, 12.0);
        const float amplitudes[4] = float[4](1.0, 0.7, 0.5, 0.3);
        const float speeds[4] = float[4](2.0, 1.5, 1.2, 0.8);

        void main() {
            vec3 pos = aPos;
            vec3 tangent = vec3(1, 0, 0);
            vec3 binormal = vec3(0, 0, 1);
            vec3 normal = vec3(0, 1, 0);
            float totalDisp = 0.0;
            for (int i = 0; i < NUM_WAVES; ++i) {
                float k = 2.0 * PI / wavelengths[i];
                float A = amplitudes[i] * uWaveAmplitude;
                float w = sqrt(9.8 * k) * speeds[i] * uWaveFrequency;
                float phase = k * dot(directions[i], pos.xz) - w * uTime;
                float disp = A * sin(phase);
                pos.xz += directions[i] * (A * cos(phase));
                pos.y += disp;
                tangent += vec3(
                    -directions[i].x * directions[i].x * k * A * sin(phase),
                    directions[i].x * k * A * cos(phase),
                    -directions[i].x * directions[i].y * k * A * sin(phase)
                );
                binormal += vec3(
                    -directions[i].x * directions[i].y * k * A * sin(phase),
                    directions[i].y * k * A * cos(phase),
                    -directions[i].y * directions[i].y * k * A * sin(phase)
                );
            }
            normal = normalize(cross(binormal, tangent));
            vWorldPos = vec3(uModel * vec4(pos, 1.0));
            mat3 normalMat = mat3(transpose(inverse(uModel)));
            vWorldNormal = normalize(normalMat * normal);
            vUv = aUv;
            gl_Position = uMvp * vec4(pos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;
        in vec3 vWorldPos;
        in vec3 vWorldNormal;
        in vec2 vUv;

        uniform vec3 uSunColor;
        uniform float uSunIntensity;
        uniform float uOpacity;
        uniform float uAlphaCutoff;
        uniform vec3 uCameraPos;
        uniform vec3 uFogColor;
        uniform float uFogNear;
        uniform float uFogFar;
        uniform float uFogStrength;
        uniform vec3 uWaterColor;
        uniform float uFoamIntensity;

        void main() {
            vec3 n = normalize(vWorldNormal);
            vec3 viewDir = normalize(uCameraPos - vWorldPos);
            float fresnel = clamp(pow(1.0 - max(dot(n, viewDir), 0.0), 3.0) * 0.65 + 0.35, 0.0, 1.0);
            // Sun direction (from environment)
            vec3 sunDir = normalize(vec3(0.2, 1.0, 0.2));
            float sunDot = max(dot(n, sunDir), 0.0);
            // Sun highlight (sharper, more realistic)
            float sunSpec = pow(sunDot, 128.0) * fresnel;
            // Ambient and base color
            vec3 ambient = vec3(0.08, 0.13, 0.22);
            vec3 base = mix(uWaterColor, ambient, 0.4);
            base = mix(base, vec3(0.18, 0.32, 0.48), fresnel);
            base += uSunColor * uSunIntensity * sunSpec * 1.2;
            base = max(base, ambient * 0.7); // Prevent black

            // Foam: based on wave slope (normal Y) and fresnel
            float foam = (1.0 - n.y) * fresnel * uFoamIntensity;
            foam *= smoothstep(0.2, 0.8, foam); // Sharpen foam edge
            vec3 foamColor = mix(vec3(0.85, 0.95, 1.0), base, 0.5);
            base = mix(base, foamColor, clamp(foam, 0.0, 1.0));

            float alpha = uOpacity;
            // Fog
            float fogSpan = max(uFogFar - uFogNear, 0.001);
            float fogT = clamp((distance(vWorldPos, uCameraPos) - uFogNear) / fogSpan, 0.0, 1.0);
            float fogAmount = pow(fogT, 1.25) * clamp(uFogStrength, 0.0, 1.0);
            base = mix(base, uFogColor, fogAmount);
            FragColor = vec4(base, alpha);
        }
    )";

    return GLUtils::createProgram(kVertexShader, kFragmentShader);
}

} // namespace sparks::render
