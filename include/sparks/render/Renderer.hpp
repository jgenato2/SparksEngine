#pragma once

#include <span>

#include <glm/vec2.hpp>

#include "sparks/core/CubeProperties.hpp"

namespace sparks::render {

struct ViewControls {
    float zoomDistance{4.0f};
    glm::vec2 panOffset{0.0f, 0.0f};
    glm::vec2 worldRotationDegrees{0.0f, 0.0f};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize();
    void setViewportSize(int width, int height);
    void render(
        std::span<const sparks::core::CubeProperties> objects,
        std::span<const bool> selectedObjects,
        const ViewControls& viewControls);

    unsigned int viewportTexture() const { return m_colorTexture; }

private:
    void createCubeResources();
    void createGridResources();
    void createFramebuffer();
    void destroyFramebuffer();
    void rebuildFramebufferIfNeeded(int width, int height);

    unsigned int m_vao{0};
    unsigned int m_vbo{0};
    unsigned int m_ebo{0};
    unsigned int m_shaderProgram{0};
    unsigned int m_gridVao{0};
    unsigned int m_gridVbo{0};
    int m_gridVertexCount{0};

    unsigned int m_fbo{0};
    unsigned int m_colorTexture{0};
    unsigned int m_depthStencilRbo{0};

    int m_viewportWidth{1280};
    int m_viewportHeight{720};
};

}  // namespace sparks::render
