#include "sparks/render/Renderer.hpp"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace {

unsigned int compileShader(unsigned int shaderType, const char* source) {
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
        throw std::runtime_error("Shader compilation failed: " + infoLog);
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
        out vec4 FragColor;

        uniform vec3 uColor;

        void main() {
            FragColor = vec4(uColor, 1.0);
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
        throw std::runtime_error("Program link failed: " + infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    return program;
}

}  // namespace

namespace sparks::render {

Renderer::~Renderer() {
    destroyFramebuffer();

    if (m_shaderProgram != 0) {
        glDeleteProgram(m_shaderProgram);
    }

    if (m_ebo != 0) {
        glDeleteBuffers(1, &m_ebo);
    }

    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
    }

    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }

    if (m_gridVbo != 0) {
        glDeleteBuffers(1, &m_gridVbo);
    }

    if (m_gridVao != 0) {
        glDeleteVertexArrays(1, &m_gridVao);
    }
}

void Renderer::initialize() {
    m_shaderProgram = createProgram();
    createCubeResources();
    createGridResources();
    createFramebuffer();
}

void Renderer::setViewportSize(const int width, const int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (width == m_viewportWidth && height == m_viewportHeight) {
        return;
    }

    m_viewportWidth = width;
    m_viewportHeight = height;
    rebuildFramebufferIfNeeded(width, height);
}

void Renderer::render(
    std::span<const sparks::core::CubeProperties> objects,
    std::span<const bool> selectedObjects,
    const ViewControls& viewControls) {
    if (objects.empty() || selectedObjects.size() != objects.size()) {
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_viewportWidth, m_viewportHeight);
    glEnable(GL_DEPTH_TEST);

    glClearColor(objects[0].backgroundColor.r, objects[0].backgroundColor.g, objects[0].backgroundColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(m_shaderProgram);

    const float aspectRatio = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
    const glm::vec3 cameraTarget(viewControls.panOffset.x, viewControls.panOffset.y, 0.0f);
    const glm::vec3 cameraPos(cameraTarget.x, cameraTarget.y, viewControls.zoomDistance);
    const glm::mat4 view = glm::lookAt(
        cameraPos,
        cameraTarget,
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, 100.0f);

    glm::mat4 world(1.0f);
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));

    const int mvpLoc = glGetUniformLocation(m_shaderProgram, "uMvp");
    const int colorLoc = glGetUniformLocation(m_shaderProgram, "uColor");

    const glm::mat4 gridMvp = projection * view * world;
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(gridMvp));
    glUniform3f(colorLoc, 0.24f, 0.24f, 0.26f);
    glBindVertexArray(m_gridVao);
    glDrawArrays(GL_LINES, 0, m_gridVertexCount);

    glBindVertexArray(m_vao);

    auto drawCube = [&](const glm::mat4& model, const glm::vec3& color, const bool wireframe) {
        const glm::mat4 mvp = projection * view * world * model;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform3f(colorLoc, color.r, color.g, color.b);

        if (wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }

        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        if (wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    };

    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& props = objects[i];

        glm::mat4 model(1.0f);
        model = glm::translate(model, props.position);

        const glm::vec3 rotationRad = glm::radians(props.rotationEulerDegrees);
        model = glm::rotate(model, rotationRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotationRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotationRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, glm::vec3(props.scale));

        glm::vec3 drawColor = props.baseColor;
        if (selectedObjects[i]) {
            drawColor = glm::clamp(drawColor + glm::vec3(0.18f), glm::vec3(0.0f), glm::vec3(1.0f));
        }

        drawCube(model, drawColor, props.wireframe);
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::createCubeResources() {
    static constexpr std::array<float, 24> vertices = {
        -0.5f, -0.5f, -0.5f,
         0.5f, -0.5f, -0.5f,
         0.5f,  0.5f, -0.5f,
        -0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f,  0.5f,
         0.5f, -0.5f,  0.5f,
         0.5f,  0.5f,  0.5f,
        -0.5f,  0.5f,  0.5f,
    };

    static constexpr std::array<unsigned int, 36> indices = {
        0, 1, 2, 2, 3, 0,
        4, 5, 6, 6, 7, 4,
        4, 5, 1, 1, 0, 4,
        7, 6, 2, 2, 3, 7,
        4, 0, 3, 3, 7, 4,
        5, 1, 2, 2, 6, 5,
    };

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Renderer::createGridResources() {
    constexpr int halfGrid = 10;
    constexpr float spacing = 1.0f;
    constexpr float y = -0.75f;

    std::vector<float> vertices;
    vertices.reserve((halfGrid * 2 + 1) * 12);

    for (int i = -halfGrid; i <= halfGrid; ++i) {
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

    m_gridVertexCount = static_cast<int>(vertices.size() / 3);

    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);

    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<long long>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Renderer::createFramebuffer() {
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

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Framebuffer is not complete.");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroyFramebuffer() {
    if (m_depthStencilRbo != 0) {
        glDeleteRenderbuffers(1, &m_depthStencilRbo);
        m_depthStencilRbo = 0;
    }

    if (m_colorTexture != 0) {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }

    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
}

void Renderer::rebuildFramebufferIfNeeded(const int width, const int height) {
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
}

}  // namespace sparks::render
