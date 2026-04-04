#include "sparks/render/WeatherRenderer.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

namespace {

unsigned int compileShader(const unsigned int shaderType, const char* source) {
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
        throw std::runtime_error("Weather shader compilation failed: " + infoLog);
    }

    return shader;
}

unsigned int createProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec3 aPos;
        uniform mat4 uMvp;

        void main() {
            gl_Position = uMvp * vec4(aPos, 1.0);
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        uniform vec3 uRainColor;
        uniform float uRainAlpha;
        uniform float uTime;

        out vec4 FragColor;

        float hash12(vec2 p) {
            vec3 p3 = fract(vec3(p.xyx) * 0.1031);
            p3 += dot(p3, p3.yzx + 33.33);
            return fract((p3.x + p3.y) * p3.z);
        }

        void main() {
            if (uRainAlpha <= 0.001) {
                discard;
            }

            // Screen-space animated breakup so streaks are less uniform.
            vec2 frag = gl_FragCoord.xy;
            float band = sin((frag.y * 0.055) + uTime * 19.0) * 0.5 + 0.5;
            float grain = hash12(floor(frag * 0.20) + vec2(uTime * 24.0, uTime * 7.0));
            float breakup = mix(0.72, 1.12, clamp(band * 0.65 + grain * 0.35, 0.0, 1.0));
            float alpha = clamp(uRainAlpha * breakup, 0.0, 1.0);

            FragColor = vec4(uRainColor, alpha);
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
        throw std::runtime_error("Weather program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

unsigned int createParticleProgram() {
    static constexpr const char* kVertexShader = R"(
        #version 460 core
        layout (location = 0) in vec4 aPosAlpha;

        uniform mat4 uMvp;
        uniform float uPointSize;

        out float vAlpha;

        void main() {
            gl_Position = uMvp * vec4(aPosAlpha.xyz, 1.0);
            gl_PointSize = uPointSize;
            vAlpha = aPosAlpha.w;
        }
    )";

    static constexpr const char* kFragmentShader = R"(
        #version 460 core
        out vec4 FragColor;

        in float vAlpha;

        uniform vec3 uTint;
        uniform float uIntensity;
        uniform float uOpacityScale;
        uniform int uRenderMode;
        uniform float uStyleBoost;

        void main() {
            vec2 uv = gl_PointCoord * 2.0 - 1.0;
            float distSq = dot(uv, uv);
            if (distSq > 1.0) {
                discard;
            }

            float alpha = 0.0;
            if (uRenderMode == 1) {
                float progress = clamp(vAlpha, 0.0, 1.0);
                float radius = mix(0.18, 0.98, progress);
                float ringWidth = mix(0.22, 0.06, progress);
                float dist = sqrt(distSq);
                float outer = smoothstep(radius + ringWidth, radius, dist);
                float inner = smoothstep(radius, max(radius - ringWidth, 0.0), dist);
                float ring = outer * inner;
                alpha = ring * (1.0 - progress) * clamp(uIntensity, 0.0, 1.0) * max(uOpacityScale, 0.0) * uStyleBoost;
            } else {
                float softCircle = 1.0 - smoothstep(0.3, 1.0, sqrt(distSq));
                alpha = vAlpha * softCircle * clamp(uIntensity, 0.0, 1.0) * max(uOpacityScale, 0.0) * uStyleBoost;
            }
            if (alpha <= 0.002) {
                discard;
            }

            FragColor = vec4(uTint, alpha);
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
        throw std::runtime_error("Weather particle program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

}  // namespace

namespace sparks::render {

WeatherRenderer::~WeatherRenderer() {
    shutdown();
}

void WeatherRenderer::initialize() {
    if (m_rainProgram != 0 || m_particleProgram != 0) {
        return;
    }

    m_rainProgram = createProgram();
    m_particleProgram = createParticleProgram();
    glGenVertexArrays(1, &m_rainVao);
    glGenBuffers(1, &m_rainVbo);
    glGenVertexArrays(1, &m_splashVao);
    glGenBuffers(1, &m_splashVbo);
    glGenVertexArrays(1, &m_dropletVao);
    glGenBuffers(1, &m_dropletVbo);
    glGenVertexArrays(1, &m_rippleVao);
    glGenBuffers(1, &m_rippleVbo);

    glBindVertexArray(m_rainVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_rainVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(m_splashVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_splashVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(m_dropletVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_dropletVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(m_rippleVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_rippleVbo);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void WeatherRenderer::shutdown() {
    if (m_rainVbo != 0) {
        glDeleteBuffers(1, &m_rainVbo);
        m_rainVbo = 0;
    }

    if (m_rainVao != 0) {
        glDeleteVertexArrays(1, &m_rainVao);
        m_rainVao = 0;
    }

    if (m_splashVbo != 0) {
        glDeleteBuffers(1, &m_splashVbo);
        m_splashVbo = 0;
    }

    if (m_splashVao != 0) {
        glDeleteVertexArrays(1, &m_splashVao);
        m_splashVao = 0;
    }

    if (m_dropletVbo != 0) {
        glDeleteBuffers(1, &m_dropletVbo);
        m_dropletVbo = 0;
    }

    if (m_dropletVao != 0) {
        glDeleteVertexArrays(1, &m_dropletVao);
        m_dropletVao = 0;
    }

    if (m_rippleVbo != 0) {
        glDeleteBuffers(1, &m_rippleVbo);
        m_rippleVbo = 0;
    }

    if (m_rippleVao != 0) {
        glDeleteVertexArrays(1, &m_rippleVao);
        m_rippleVao = 0;
    }

    if (m_rainProgram != 0) {
        glDeleteProgram(m_rainProgram);
        m_rainProgram = 0;
    }

    if (m_particleProgram != 0) {
        glDeleteProgram(m_particleProgram);
        m_particleProgram = 0;
    }

    m_rainVertexCount = 0;
    m_splashCount = 0;
    m_dropletCount = 0;
    m_rippleCount = 0;
}

void WeatherRenderer::updateRainGeometry(const std::vector<glm::vec3>& lineVertices) {
    if (m_rainVbo == 0) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_rainVbo);
    glBufferData(GL_ARRAY_BUFFER,
        static_cast<long long>(lineVertices.size() * sizeof(glm::vec3)),
        lineVertices.data(),
        GL_DYNAMIC_DRAW);

    m_rainVertexCount = static_cast<int>(lineVertices.size());
}

void WeatherRenderer::updateSplashGeometry(const std::vector<glm::vec4>& splashPoints) {
    if (m_splashVbo == 0) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_splashVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(splashPoints.size() * sizeof(glm::vec4)),
        splashPoints.data(),
        GL_DYNAMIC_DRAW);

    m_splashCount = static_cast<int>(splashPoints.size());
}

void WeatherRenderer::updateDropletGeometry(const std::vector<glm::vec4>& dropletPoints) {
    if (m_dropletVbo == 0) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_dropletVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(dropletPoints.size() * sizeof(glm::vec4)),
        dropletPoints.data(),
        GL_DYNAMIC_DRAW);

    m_dropletCount = static_cast<int>(dropletPoints.size());
}

void WeatherRenderer::updateRippleGeometry(const std::vector<glm::vec4>& ripplePoints) {
    if (m_rippleVbo == 0) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_rippleVbo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<long long>(ripplePoints.size() * sizeof(glm::vec4)),
        ripplePoints.data(),
        GL_DYNAMIC_DRAW);

    m_rippleCount = static_cast<int>(ripplePoints.size());
}

void WeatherRenderer::renderRain(
    const glm::mat4& projection,
    const glm::mat4& view,
    const glm::mat4& world,
    const int rainConcept,
    const bool useCustomVisualProfile,
    const glm::vec3& rainTint,
    const float rainStyleBoost,
    const float intensity,
    const bool enabled,
    const float lineWidth,
    const float opacityScale) const {
    if (!enabled || intensity <= 0.001f || m_rainProgram == 0 || m_rainVao == 0 || m_rainVertexCount <= 0) {
        return;
    }

    const glm::mat4 mvp = projection * view * world;
    const int styleIndex = std::clamp(rainConcept, 0, 5);

    glm::vec3 rainColor(0.78f, 0.84f, 0.92f);
    float styleAlpha = 0.72f;
    if (styleIndex == 0) {
        rainColor = glm::vec3(0.68f, 0.78f, 0.90f);
        styleAlpha = 0.58f;
    } else if (styleIndex == 2) {
        rainColor = glm::vec3(0.88f, 0.93f, 0.99f);
        styleAlpha = 0.86f;
    } else if (styleIndex == 3) {
        rainColor = glm::vec3(0.84f, 0.91f, 0.98f);
        styleAlpha = 0.80f;
    } else if (styleIndex == 4) {
        rainColor = glm::vec3(0.93f, 0.97f, 1.00f);
        styleAlpha = 0.92f;
    } else if (styleIndex == 5) {
        rainColor = glm::vec3(0.66f, 0.90f, 1.00f);
        styleAlpha = 0.84f;
    }
    if (useCustomVisualProfile) {
        rainColor *= rainTint;
        styleAlpha = std::clamp(styleAlpha * rainStyleBoost, 0.22f, 1.0f);
    }

    const float finalAlpha = std::clamp(styleAlpha * intensity * opacityScale, 0.0f, 1.0f);
    if (finalAlpha <= 0.001f) {
        return;
    }
    const float coreAlpha = std::clamp(finalAlpha * 0.95f, 0.0f, 1.0f);
    const float veilAlpha = std::clamp(finalAlpha * 0.38f, 0.0f, 1.0f);

    glUseProgram(m_rainProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_rainProgram, "uMvp"),      1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3f(glGetUniformLocation(m_rainProgram, "uRainColor"),       rainColor.x, rainColor.y, rainColor.z);
    static const auto sRainClockStart = std::chrono::steady_clock::now();
    const auto rainNow = std::chrono::steady_clock::now();
    const float rainTimeSeconds = std::chrono::duration<float>(rainNow - sRainClockStart).count();
    glUniform1f(glGetUniformLocation(m_rainProgram, "uTime"),            rainTimeSeconds);

    const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_rainVao);

    // Pass 1: bright thin core streaks for visibility.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUniform1f(glGetUniformLocation(m_rainProgram, "uRainAlpha"), coreAlpha);
    glLineWidth(std::clamp(lineWidth * 2.0f, 1.0f, 6.0f));
    glDrawArrays(GL_LINES, 0, m_rainVertexCount);

    // Pass 2: broader soft veil to add volume and depth.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1f(glGetUniformLocation(m_rainProgram, "uRainAlpha"), veilAlpha);
    glLineWidth(std::clamp(lineWidth * 3.3f, 1.0f, 8.0f));
    glDrawArrays(GL_LINES, 0, m_rainVertexCount);

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    if (cullWasEnabled) {
        glEnable(GL_CULL_FACE);
    }
    if (depthWasEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void WeatherRenderer::renderSplashes(
    const glm::mat4& projection,
    const glm::mat4& view,
    const glm::mat4& world,
    const int rainConcept,
    const bool useCustomVisualProfile,
    const glm::vec3& customSplashTint,
    const float particleStyleBoost,
    const float intensity,
    const bool enabled,
    const float pointSize,
    const float opacityScale) const {
    if (!enabled || intensity <= 0.001f || m_particleProgram == 0 || m_splashVao == 0 || m_splashCount <= 0) {
        return;
    }

    const glm::mat4 mvp = projection * view * world;
    const int styleIndex = std::clamp(rainConcept, 0, 5);
    glm::vec3 splashTint(0.88f, 0.91f, 0.96f);
    float styleBoost = 1.0f;
    if (styleIndex == 0) {
        splashTint = glm::vec3(0.84f, 0.89f, 0.95f);
        styleBoost = 0.84f;
    } else if (styleIndex == 2) {
        splashTint = glm::vec3(0.94f, 0.96f, 0.99f);
        styleBoost = 1.20f;
    } else if (styleIndex == 3) {
        splashTint = glm::vec3(0.90f, 0.95f, 0.99f);
        styleBoost = 1.08f;
    } else if (styleIndex == 4) {
        splashTint = glm::vec3(0.96f, 0.98f, 1.00f);
        styleBoost = 1.30f;
    } else if (styleIndex == 5) {
        splashTint = glm::vec3(0.72f, 0.91f, 1.00f);
        styleBoost = 1.16f;
    }
    if (useCustomVisualProfile) {
        splashTint *= customSplashTint;
        styleBoost *= particleStyleBoost;
    }

    glUseProgram(m_particleProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_particleProgram, "uMvp"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(glGetUniformLocation(m_particleProgram, "uPointSize"), pointSize);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uIntensity"), intensity);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uOpacityScale"), opacityScale);
    glUniform1i(glGetUniformLocation(m_particleProgram, "uRenderMode"), 0);
    glUniform3f(glGetUniformLocation(m_particleProgram, "uTint"), splashTint.x, splashTint.y, splashTint.z);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uStyleBoost"), styleBoost);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_splashVao);
    glDrawArrays(GL_POINTS, 0, m_splashCount);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void WeatherRenderer::renderDroplets(
    const glm::mat4& projection,
    const glm::mat4& view,
    const glm::mat4& world,
    const int rainConcept,
    const bool useCustomVisualProfile,
    const glm::vec3& customDropletTint,
    const float particleStyleBoost,
    const float intensity,
    const bool enabled,
    const float pointSize,
    const float opacityScale) const {
    if (!enabled || intensity <= 0.001f || m_particleProgram == 0 || m_dropletVao == 0 || m_dropletCount <= 0) {
        return;
    }

    const glm::mat4 mvp = projection * view * world;
    const int styleIndex = std::clamp(rainConcept, 0, 5);
    glm::vec3 dropletTint(0.78f, 0.84f, 0.92f);
    float styleBoost = 1.0f;
    if (styleIndex == 0) {
        dropletTint = glm::vec3(0.72f, 0.80f, 0.90f);
        styleBoost = 0.80f;
    } else if (styleIndex == 2) {
        dropletTint = glm::vec3(0.86f, 0.92f, 0.98f);
        styleBoost = 1.15f;
    } else if (styleIndex == 3) {
        dropletTint = glm::vec3(0.80f, 0.88f, 0.97f);
        styleBoost = 1.08f;
    } else if (styleIndex == 4) {
        dropletTint = glm::vec3(0.90f, 0.95f, 1.00f);
        styleBoost = 1.25f;
    } else if (styleIndex == 5) {
        dropletTint = glm::vec3(0.64f, 0.88f, 0.99f);
        styleBoost = 1.20f;
    }
    if (useCustomVisualProfile) {
        dropletTint *= customDropletTint;
        styleBoost *= particleStyleBoost;
    }

    glUseProgram(m_particleProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_particleProgram, "uMvp"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(glGetUniformLocation(m_particleProgram, "uPointSize"), pointSize);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uIntensity"), intensity * 0.9f);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uOpacityScale"), opacityScale);
    glUniform1i(glGetUniformLocation(m_particleProgram, "uRenderMode"), 0);
    glUniform3f(glGetUniformLocation(m_particleProgram, "uTint"), dropletTint.x, dropletTint.y, dropletTint.z);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uStyleBoost"), styleBoost);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_dropletVao);
    glDrawArrays(GL_POINTS, 0, m_dropletCount);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

void WeatherRenderer::renderRipples(
    const glm::mat4& projection,
    const glm::mat4& view,
    const glm::mat4& world,
    const int rainConcept,
    const bool useCustomVisualProfile,
    const glm::vec3& customRippleTint,
    const float particleStyleBoost,
    const float intensity,
    const bool enabled,
    const float pointSize,
    const float opacityScale) const {
    if (!enabled || intensity <= 0.001f || m_particleProgram == 0 || m_rippleVao == 0 || m_rippleCount <= 0) {
        return;
    }

    const glm::mat4 mvp = projection * view * world;
    const int styleIndex = std::clamp(rainConcept, 0, 5);
    glm::vec3 rippleTint(0.72f, 0.80f, 0.89f);
    float styleBoost = 1.0f;
    if (styleIndex == 0) {
        rippleTint = glm::vec3(0.66f, 0.75f, 0.86f);
        styleBoost = 0.84f;
    } else if (styleIndex == 2) {
        rippleTint = glm::vec3(0.78f, 0.86f, 0.94f);
        styleBoost = 1.15f;
    } else if (styleIndex == 3) {
        rippleTint = glm::vec3(0.74f, 0.84f, 0.94f);
        styleBoost = 1.08f;
    } else if (styleIndex == 4) {
        rippleTint = glm::vec3(0.80f, 0.90f, 0.98f);
        styleBoost = 1.20f;
    } else if (styleIndex == 5) {
        rippleTint = glm::vec3(0.62f, 0.86f, 0.98f);
        styleBoost = 1.18f;
    }
    if (useCustomVisualProfile) {
        rippleTint *= customRippleTint;
        styleBoost *= particleStyleBoost;
    }

    glUseProgram(m_particleProgram);
    glUniformMatrix4fv(glGetUniformLocation(m_particleProgram, "uMvp"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(glGetUniformLocation(m_particleProgram, "uPointSize"), pointSize);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uIntensity"), intensity);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uOpacityScale"), opacityScale);
    glUniform1i(glGetUniformLocation(m_particleProgram, "uRenderMode"), 1);
    glUniform3f(glGetUniformLocation(m_particleProgram, "uTint"), rippleTint.x, rippleTint.y, rippleTint.z);
    glUniform1f(glGetUniformLocation(m_particleProgram, "uStyleBoost"), styleBoost);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_rippleVao);
    glDrawArrays(GL_POINTS, 0, m_rippleCount);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBindVertexArray(0);
}

}  // namespace sparks::render
