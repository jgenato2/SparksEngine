#include "sparks/render/shaders/textured/TexturedProgram.hpp"
#include "sparks/render/GLUtils.hpp"
#include <glad/gl.h>
#include <string>

namespace sparks::render {

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
        uniform float uWaterHalfExtent;
        uniform vec3 uWaterTint;
        uniform mat4 uInvWorld;
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

            vec3 scenePos = vec3(uInvWorld * vec4(vWorldPos, 1.0));
            bool insideWaterArea = abs(scenePos.x) <= uWaterHalfExtent && abs(scenePos.z) <= uWaterHalfExtent;

            if (uWaterEnabled == 1 && insideWaterArea) {
                float waterDepth = uWaterLevel - scenePos.y;
                if (waterDepth > 0.02) {
                    float submerged = smoothstep(0.02, 1.8, waterDepth);
                    float deepSubmerged = smoothstep(0.50, 4.5, waterDepth);
                    float waterline = 1.0 - smoothstep(0.02, 0.10, waterDepth);
                    vec3 underwaterColor = mix(color, uWaterTint * mix(0.82, 1.12, ndl), submerged * 0.50);
                    underwaterColor *= mix(vec3(1.0), vec3(0.72, 0.88, 0.96), deepSubmerged);
                    underwaterColor += vec3(0.07, 0.11, 0.09) * waterline * 0.22;
                    color = underwaterColor;
                }
            }

            float fogSpan = max(uFogFar - uFogNear, 0.001);
            float fogT = clamp((distance(vWorldPos, uCameraPos) - uFogNear) / fogSpan, 0.0, 1.0);
            float fogAmount = pow(fogT, 1.25) * clamp(uFogStrength, 0.0, 1.0);
            vec3 fogTarget = uFogColor;
            if (uWaterEnabled == 1 && insideWaterArea) {
                float heightAboveWater = max(scenePos.y - uWaterLevel, 0.0);
                float aboveWaterBlend = smoothstep(0.25, 8.0, heightAboveWater);
                float fogLuma = dot(uFogColor, vec3(0.299, 0.587, 0.114));
                vec3 desaturatedFog = mix(uFogColor, vec3(fogLuma), 0.40);
                vec3 landFog = mix(desaturatedFog, color, 0.22);
                fogTarget = mix(uFogColor, landFog, aboveWaterBlend);
            }
            color = mix(color, fogTarget, fogAmount);

            FragColor = vec4(color, alpha);
        }
    )";

    return GLUtils::createProgram(kVertexShader, kFragmentShader);
}

} // namespace sparks::render
