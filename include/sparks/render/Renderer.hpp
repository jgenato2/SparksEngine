#pragma once

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace sparks::render {

struct ViewControls {
    float zoomDistance{4.0f};
    glm::vec2 panOffset{0.0f, 0.0f};
    glm::vec2 worldRotationDegrees{0.0f, 0.0f};
};

struct ImportedModelData {
    std::vector<float> vertices; // interleaved: position(3), normal(3), uv(2)
    std::vector<unsigned int> indices;
    std::vector<unsigned char> textureRgba;
    int textureWidth{1};
    int textureHeight{1};
    float opacity{1.0f};
    bool alphaBlend{false};
    glm::vec4 diffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 emissiveColor{0.0f, 0.0f, 0.0f};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 rotationEulerDegrees{0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f, 1.0f, 1.0f};
    glm::vec3 dimensions{0.0f, 0.0f, 0.0f};
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize();
    void setViewportSize(int width, int height);
    void setImportedModel(const ImportedModelData& model);
    void setImportedModelTransform(const glm::vec3& position, const glm::vec3& rotationEulerDegrees, const glm::vec3& scale);
    void render(const ViewControls& viewControls);

    unsigned int viewportTexture() const { return m_colorTexture; }

private:
    void createGridResources();
    void createFramebuffer();
    void destroyFramebuffer();
    void rebuildFramebufferIfNeeded(int width, int height);

    unsigned int m_shaderProgram{0};
    unsigned int m_gridVao{0};
    unsigned int m_gridVbo{0};
    int m_gridRegularCount{0};
    int m_gridAxisXStart{0};
    int m_gridAxisYStart{0};

    unsigned int m_texturedProgram{0};
    unsigned int m_importVao{0};
    unsigned int m_importVbo{0};
    unsigned int m_importEbo{0};
    unsigned int m_importTexture{0};
    int m_importIndexCount{0};
    float m_importOpacity{1.0f};
    bool m_importAlphaBlend{false};
    glm::vec4 m_importDiffuseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 m_importEmissive{0.0f, 0.0f, 0.0f};
    glm::vec3 m_importPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 m_importRotationEuler{0.0f, 0.0f, 0.0f};
    glm::vec3 m_importScale{1.0f, 1.0f, 1.0f};

    unsigned int m_fbo{0};
    unsigned int m_colorTexture{0};
    unsigned int m_depthStencilRbo{0};

    int m_viewportWidth{1280};
    int m_viewportHeight{720};
};

}  // namespace sparks::render
