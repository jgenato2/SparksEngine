#include <stdexcept>
#include <algorithm>

#include <glad/gl.h>

#include "sparks/render/Renderer.hpp"

namespace sparks::render {

void Renderer::createFramebuffer()
{
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_viewportWidth, m_viewportHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    glGenRenderbuffers(1, &m_depthStencilRbo);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_viewportWidth, m_viewportHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, m_depthStencilRbo);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Framebuffer is not complete.");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Post-process FBO (underwater / cinematic ping-pong target) ────────────
    glGenFramebuffers(1, &m_postFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_postFbo);

    glGenTextures(1, &m_postColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_postColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_viewportWidth, m_viewportHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_postColorTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Post-process framebuffer is not complete.");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // ── Half-resolution sky FBO ───────────────────────────────────────────────
    // Skydome fragment shader is expensive (FBM clouds + stars).
    // Render at 50% in each dimension → ~25% of pixels, then blit up.
    const int hw = std::max(1, m_viewportWidth  / 2);
    const int hh = std::max(1, m_viewportHeight / 2);
    glGenFramebuffers(1, &m_skyHalfFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_skyHalfFbo);
    glGenTextures(1, &m_skyHalfColorTex);
    glBindTexture(GL_TEXTURE_2D, m_skyHalfColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hw, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_skyHalfColorTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroyFramebuffer()
{
    if (m_depthStencilRbo != 0)
    {
        glDeleteRenderbuffers(1, &m_depthStencilRbo);
        m_depthStencilRbo = 0;
    }
    if (m_colorTexture != 0)
    {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }
    if (m_fbo != 0)
    {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_postColorTexture != 0)
    {
        glDeleteTextures(1, &m_postColorTexture);
        m_postColorTexture = 0;
    }
    if (m_skyHalfColorTex != 0)
    {
        glDeleteTextures(1, &m_skyHalfColorTex);
        m_skyHalfColorTex = 0;
    }
    if (m_skyHalfFbo != 0)
    {
        glDeleteFramebuffers(1, &m_skyHalfFbo);
        m_skyHalfFbo = 0;
    }
    if (m_postFbo != 0)
    {
        glDeleteFramebuffers(1, &m_postFbo);
        m_postFbo = 0;
    }
}

void Renderer::rebuildFramebufferIfNeeded(const int width, const int height)
{
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilRbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);

    glBindTexture(GL_TEXTURE_2D, m_postColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    const int hw = std::max(1, width  / 2);
    const int hh = std::max(1, height / 2);
    glBindTexture(GL_TEXTURE_2D, m_skyHalfColorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, hw, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
}

} // namespace sparks::render
