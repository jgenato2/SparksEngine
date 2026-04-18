#pragma once
#include <glm/glm.hpp>

namespace sparks::render {
class CloudShadowMap {
public:
    CloudShadowMap();
    ~CloudShadowMap();
    void initialize(int size);
    void loadShader();
    void render(float time, const glm::vec3& sunDir);
    unsigned int getTexture() const;
private:
    unsigned int m_fbo = 0;
    unsigned int m_tex = 0;
    unsigned int m_program = 0;
    int m_size = 0;
    glm::mat4 m_sunViewProj;
    int m_sunViewProjLoc = -1;
    int m_timeLoc = -1;
    int m_baseLoc = -1;
    int m_topLoc = -1;
    int m_densityLoc = -1;
};
}
