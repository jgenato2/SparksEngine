#include "sparks/render/CloudShadowMap.hpp"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <string>

namespace sparks::render {
CloudShadowMap::CloudShadowMap() {}
CloudShadowMap::~CloudShadowMap() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_tex) glDeleteTextures(1, &m_tex);
    if (m_program) glDeleteProgram(m_program);
}
void CloudShadowMap::initialize(int size) {
    m_size = size;
    if (m_fbo) { glDeleteFramebuffers(1, &m_fbo); m_fbo = 0; }
    if (m_tex) { glDeleteTextures(1, &m_tex); m_tex = 0; }
    glGenFramebuffers(1, &m_fbo);
    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, size, size, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Cloud shadow framebuffer incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
void CloudShadowMap::loadShader() {
    std::ifstream file("src/render/CloudShadowMapShader.glsl");
    std::string shaderSrc((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const char* src = shaderSrc.c_str();
    m_program = glCreateProgram();
    unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
    const char* vsSrc = "#version 460 core\nout vec2 vUv;\nvoid main() { float x = float((gl_VertexID & 1) << 2) - 1.0; float y = float((gl_VertexID & 2) << 1) - 1.0; vUv = vec2(x * 0.5 + 0.5, y * 0.5 + 0.5); gl_Position = vec4(x, y, 0.0, 1.0); }";
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);
    unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &src, nullptr);
    glCompileShader(fs);
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    m_sunViewProjLoc = glGetUniformLocation(m_program, "uSunViewProj");
    m_timeLoc = glGetUniformLocation(m_program, "uTime");
    m_baseLoc = glGetUniformLocation(m_program, "uCloudBase");
    m_topLoc = glGetUniformLocation(m_program, "uCloudTop");
    m_densityLoc = glGetUniformLocation(m_program, "uCloudDensity");
}
void CloudShadowMap::render(float time, const glm::vec3& sunDir) {
    float orthoSize = 120.0f;
    glm::mat4 sunProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, -60.0f, 120.0f);
    glm::vec3 sunPos = -sunDir * 60.0f;
    glm::mat4 sunView = glm::lookAt(sunPos, glm::vec3(0,0,0), glm::vec3(0,1,0));
    m_sunViewProj = sunProj * sunView;
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_size, m_size);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_program);
    glUniformMatrix4fv(m_sunViewProjLoc, 1, GL_FALSE, glm::value_ptr(m_sunViewProj));
    glUniform1f(m_timeLoc, time);
    glUniform1f(m_baseLoc, 30.0f);
    glUniform1f(m_topLoc, 60.0f);
    glUniform1f(m_densityLoc, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
unsigned int CloudShadowMap::getTexture() const { return m_tex; }
} // namespace sparks::render
