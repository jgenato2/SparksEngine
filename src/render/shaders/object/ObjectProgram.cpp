#include "sparks/render/shaders/object/ObjectProgram.hpp"
#include "sparks/render/GLUtils.hpp"

namespace sparks::render {

unsigned int createObjectProgram()
{
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

    return GLUtils::createProgram(kVertexShader, kFragmentShader);
}

} // namespace sparks::render
