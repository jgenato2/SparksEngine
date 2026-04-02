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

constexpr float kCameraFarPlane = 1000.0f;

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

    static constexpr const char* kFragmentShader = R"(
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

        void main() {
            vec4 base = texture(uTex, vUv);
            vec3 n = normalize(vWorldNormal);
            vec3 l = normalize(uLightPos - vWorldPos);
            float ndl = max(dot(n, l), 0.0);
            float lit = mix(0.35, 1.25, pow(ndl, 0.9));
            FragColor = vec4(base.rgb * lit, base.a);
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

    if (m_importTexture != 0) {
        glDeleteTextures(1, &m_importTexture);
    }

    if (m_importEbo != 0) {
        glDeleteBuffers(1, &m_importEbo);
    }

    if (m_importVbo != 0) {
        glDeleteBuffers(1, &m_importVbo);
    }

    if (m_importVao != 0) {
        glDeleteVertexArrays(1, &m_importVao);
    }

    if (m_texturedProgram != 0) {
        glDeleteProgram(m_texturedProgram);
    }
}

void Renderer::initialize() {
    m_shaderProgram = createProgram();
    m_texturedProgram = createTexturedProgram();
    createCubeResources();
    createGridResources();
    createFramebuffer();
}

void Renderer::setImportedModel(const ImportedModelData& model) {
    if (m_importTexture == 0) {
        glGenTextures(1, &m_importTexture);
    }
    if (m_importVao == 0) {
        glGenVertexArrays(1, &m_importVao);
    }
    if (m_importVbo == 0) {
        glGenBuffers(1, &m_importVbo);
    }
    if (m_importEbo == 0) {
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
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
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
    m_importPosition = model.position;
    m_importRotationEuler = model.rotationEulerDegrees;
    m_importScale = model.scale;
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
    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, kCameraFarPlane);

    glm::mat4 world(1.0f);
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));

    const int mvpLoc = glGetUniformLocation(m_shaderProgram, "uMvp");
    const int modelLoc = glGetUniformLocation(m_shaderProgram, "uModel");
    const int colorLoc = glGetUniformLocation(m_shaderProgram, "uColor");
    const int alphaLoc = glGetUniformLocation(m_shaderProgram, "uAlpha");
    const int gradientLoc = glGetUniformLocation(m_shaderProgram, "uGradientStrength");
    const int lightPosLoc = glGetUniformLocation(m_shaderProgram, "uLightPos");

    const glm::vec3 lightPos(5.5f, 6.5f, 4.0f);
    glUniform3f(lightPosLoc, lightPos.x, lightPos.y, lightPos.z);

    const glm::mat4 gridMvp = projection * view * world;
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(gridMvp));
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(world));
    glBindVertexArray(m_gridVao);
    glUniform1f(alphaLoc, 1.0f);
    glUniform1f(gradientLoc, 0.0f);
    glUniform3f(colorLoc, 0.24f, 0.24f, 0.26f);
    glDrawArrays(GL_LINES, 0, m_gridRegularCount);
    glUniform3f(colorLoc, 0.53f, 0.11f, 0.11f);
    glDrawArrays(GL_LINES, m_gridAxisXStart, 2);
    glUniform3f(colorLoc, 0.11f, 0.43f, 0.11f);
    glDrawArrays(GL_LINES, m_gridAxisYStart, 2);

    glBindVertexArray(m_vao);

    auto drawCube = [&](const glm::mat4& model, const glm::vec3& color, const bool wireframe, const float alpha, const float gradientStrength) {
        const glm::mat4 worldModel = world * model;
        const glm::mat4 mvp = projection * view * worldModel;
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(worldModel));
        glUniform3f(colorLoc, color.r, color.g, color.b);
        glUniform1f(alphaLoc, alpha);
        glUniform1f(gradientLoc, gradientStrength);

        if (wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }

        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
        if (wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    };

    // Planar projected shadows onto the grid plane.
    constexpr float kShadowPlaneY = -0.749f;
    const glm::vec4 shadowPlane(0.0f, 1.0f, 0.0f, -kShadowPlaneY);
    const glm::vec3 lightDir3 = glm::normalize(glm::vec3(-0.65f, 1.0f, -0.45f));
    const glm::vec4 lightDir(lightDir3, 0.0f);

    const float dot = shadowPlane.x * lightDir.x + shadowPlane.y * lightDir.y + shadowPlane.z * lightDir.z + shadowPlane.w * lightDir.w;
    glm::mat4 shadowProj(0.0f);
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const float identity = (row == col) ? dot : 0.0f;
            shadowProj[col][row] = identity - lightDir[row] * shadowPlane[col];
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& props = objects[i];

        glm::mat4 model(1.0f);
        model = glm::translate(model, props.position);

        const glm::vec3 rotationRad = glm::radians(props.rotationEulerDegrees);
        model = glm::rotate(model, rotationRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotationRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotationRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, props.scale);

        const glm::mat4 shadowModel = shadowProj * model;
        drawCube(shadowModel, glm::vec3(0.02f, 0.02f, 0.03f), false, 0.38f, 0.0f);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    for (std::size_t i = 0; i < objects.size(); ++i) {
        const auto& props = objects[i];

        glm::mat4 model(1.0f);
        model = glm::translate(model, props.position);

        const glm::vec3 rotationRad = glm::radians(props.rotationEulerDegrees);
        model = glm::rotate(model, rotationRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotationRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotationRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, props.scale);

        glm::vec3 drawColor = props.baseColor;
        if (selectedObjects[i]) {
            drawColor = glm::clamp(drawColor + glm::vec3(0.18f), glm::vec3(0.0f), glm::vec3(1.0f));
        }

        drawCube(model, drawColor, props.wireframe, 1.0f, 1.0f);
    }

    if (m_importVao != 0 && m_importTexture != 0 && m_importIndexCount > 0) {
        glUseProgram(m_texturedProgram);

        const int mvpLocT = glGetUniformLocation(m_texturedProgram, "uMvp");
        const int modelLocT = glGetUniformLocation(m_texturedProgram, "uModel");
        const int lightPosLocT = glGetUniformLocation(m_texturedProgram, "uLightPos");
        const int texLocT = glGetUniformLocation(m_texturedProgram, "uTex");

        glm::mat4 model(1.0f);
        model = glm::translate(model, m_importPosition);
        const glm::vec3 rotRad = glm::radians(m_importRotationEuler);
        model = glm::rotate(model, rotRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rotRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rotRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::scale(model, m_importScale);

        const glm::mat4 worldModel = world * model;
        const glm::mat4 mvp = projection * view * worldModel;

        glUniformMatrix4fv(mvpLocT, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniformMatrix4fv(modelLocT, 1, GL_FALSE, glm::value_ptr(worldModel));
        glUniform3f(lightPosLocT, 5.5f, 6.5f, 4.0f);
        glUniform1i(texLocT, 0);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_importTexture);
        glBindVertexArray(m_importVao);
        glDrawElements(GL_TRIANGLES, m_importIndexCount, GL_UNSIGNED_INT, nullptr);

        glUseProgram(m_shaderProgram);
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
    constexpr int halfGrid = 20;
    constexpr float spacing = 1.0f;
    constexpr float y = -0.75f;

    std::vector<float> vertices;
    vertices.reserve(halfGrid * 2 * 12 + 12);

    // Regular grid lines: skip the center axis lines, drawn separately with colors.
    for (int i = -halfGrid; i <= halfGrid; ++i) {
        if (i == 0) {
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
