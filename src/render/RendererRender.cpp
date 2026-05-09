#include <ctime>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <chrono>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "sparks/render/Renderer.hpp"
#include "sparks/render/RenderContext.hpp"
#include "sparks/render/RendererAstronomy.hpp"

namespace sparks::render {

constexpr float kCameraFarPlane = 1000.0f;
constexpr float kWaterHalfExtentRender = 220.0f;

// ─────────────────────────────────────────────────────────────────────────────
// renderSkydome
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::renderSkydome(const RenderContext &ctx, unsigned int gpuQuery)
{
    if (!m_environmentSettings.enableSkydome || m_skydomeProgram == 0 ||
        m_skydomeVao == 0 || m_skydomeIndexCount == 0)
        return;

    const int skyWorldRotLoc = glGetUniformLocation(m_skydomeProgram, "uWorldRot");
    glUniformMatrix3fv(skyWorldRotLoc, 1, GL_FALSE, glm::value_ptr(ctx.worldRot3x3));
    glUseProgram(m_skydomeProgram);

    glm::mat4 skydomeModel(1.0f);
    skydomeModel = glm::scale(skydomeModel, glm::vec3(m_environmentSettings.skydomeRadius));
    const glm::mat4 skyMvp = ctx.projection * ctx.viewRotOnly * ctx.skyWorld * skydomeModel;

    const int skyMvpLoc               = glGetUniformLocation(m_skydomeProgram, "uMvp");
    const int skyHorizonLoc           = glGetUniformLocation(m_skydomeProgram, "uHorizonColor");
    const int skyZenithLoc            = glGetUniformLocation(m_skydomeProgram, "uZenithColor");
    const int skySunDirLoc            = glGetUniformLocation(m_skydomeProgram, "uSunDir");
    const int skySunDiscSizeLoc       = glGetUniformLocation(m_skydomeProgram, "uSunDiscSize");
    const int skySunIntensityLoc      = glGetUniformLocation(m_skydomeProgram, "uSunIntensity");
    const int skySunColorLoc          = glGetUniformLocation(m_skydomeProgram, "uSunColor");
    const int skySunHaloSizeLoc       = glGetUniformLocation(m_skydomeProgram, "uSunHaloSize");
    const int skySunHaloStrengthLoc   = glGetUniformLocation(m_skydomeProgram, "uSunHaloStrength");
    const int skyDustAmountLoc        = glGetUniformLocation(m_skydomeProgram, "uDustAmount");
    const int skyDustColorLoc         = glGetUniformLocation(m_skydomeProgram, "uDustColor");
    const int skyCloudAmountLoc       = glGetUniformLocation(m_skydomeProgram, "uCloudAmount");
    const int skyCloudScaleLoc        = glGetUniformLocation(m_skydomeProgram, "uCloudScale");
    const int skyCloudSpeedLoc        = glGetUniformLocation(m_skydomeProgram, "uCloudSpeed");
    const int skyCloudColorLoc        = glGetUniformLocation(m_skydomeProgram, "uCloudColor");
    const int skyCloudShadowStrLoc    = glGetUniformLocation(m_skydomeProgram, "uCloudShadowStrength");
    const int skyWindDirectionLoc     = glGetUniformLocation(m_skydomeProgram, "uWindDirection");
    const int skyWindSpeedLoc         = glGetUniformLocation(m_skydomeProgram, "uWindSpeed");
    const int skyTimeLoc              = glGetUniformLocation(m_skydomeProgram, "uTime");
    const int skyUnderseaTintLoc      = glGetUniformLocation(m_skydomeProgram, "uUnderseaTint");
    const int skyUnderseaDeepLoc      = glGetUniformLocation(m_skydomeProgram, "uUnderseaDeepColor");
    const int skyUnderseaEnableLoc    = glGetUniformLocation(m_skydomeProgram, "uUnderseaEnabled");
    const int skyCameraUnderwaterLoc  = glGetUniformLocation(m_skydomeProgram, "uCameraUnderwater");

    bool cameraUnderwater = false;
    if (m_environmentSettings.enableWater)
    {
        const bool insideWaterArea =
            std::abs(ctx.cameraPos.x) <= m_environmentSettings.waterHalfExtent &&
            std::abs(ctx.cameraPos.z) <= m_environmentSettings.waterHalfExtent;
        const float depth = m_environmentSettings.waterLevel - ctx.cameraPos.y;
        cameraUnderwater = insideWaterArea && depth > 0.30f;
    }

    const float sunDiscSize     = m_environmentSettings.enableSun ? m_environmentSettings.sunDiscSize     : 0.0f;
    const float sunIntensity    = m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity    : 0.0f;
    const glm::vec3 sunColor    = m_environmentSettings.enableSun ? m_environmentSettings.sunColor        : glm::vec3(0.0f);
    const float sunHaloSize     = m_environmentSettings.enableSun ? m_environmentSettings.sunHaloSize     : 0.0f;
    const float sunHaloStrength = m_environmentSettings.enableSun ? m_environmentSettings.sunHaloStrength : 0.0f;

    glUniformMatrix4fv(skyMvpLoc, 1, GL_FALSE, glm::value_ptr(skyMvp));
    glUniform3f(skyHorizonLoc, m_environmentSettings.skyHorizonColor.r, m_environmentSettings.skyHorizonColor.g, m_environmentSettings.skyHorizonColor.b);
    glUniform3f(skyZenithLoc,  m_environmentSettings.skyZenithColor.r,  m_environmentSettings.skyZenithColor.g,  m_environmentSettings.skyZenithColor.b);
    glUniform3f(skySunDirLoc,  m_environmentSettings.terrainLightDirection.x, m_environmentSettings.terrainLightDirection.y, m_environmentSettings.terrainLightDirection.z);
    glUniform1f(skySunDiscSizeLoc,       sunDiscSize);
    glUniform1f(skySunIntensityLoc,      sunIntensity);
    glUniform3f(skySunColorLoc,          sunColor.r, sunColor.g, sunColor.b);
    glUniform1f(skySunHaloSizeLoc,       sunHaloSize);
    glUniform1f(skySunHaloStrengthLoc,   sunHaloStrength);
    glUniform1f(skyDustAmountLoc,        m_environmentSettings.dustAmount);
    glUniform3f(skyDustColorLoc,         m_environmentSettings.dustColor.r, m_environmentSettings.dustColor.g, m_environmentSettings.dustColor.b);
    glUniform1f(skyCloudAmountLoc,       m_environmentSettings.skyCloudAmount);
    glUniform1f(skyCloudScaleLoc,        m_environmentSettings.skyCloudScale);
    glUniform1f(skyCloudSpeedLoc,        m_environmentSettings.skyCloudSpeed);
    glUniform3f(skyCloudColorLoc,        m_environmentSettings.skyCloudColor.r, m_environmentSettings.skyCloudColor.g, m_environmentSettings.skyCloudColor.b);
    glUniform1f(skyCloudShadowStrLoc,    m_environmentSettings.skyCloudShadowStrength);
    glUniform2f(skyWindDirectionLoc,     m_environmentSettings.windDirection.x, m_environmentSettings.windDirection.y);
    glUniform1f(skyWindSpeedLoc,         m_environmentSettings.windSpeed);
    glUniform1f(skyTimeLoc,              ctx.elapsedSeconds);
    glUniform3f(skyUnderseaTintLoc,      m_environmentSettings.waterColor.r, m_environmentSettings.waterColor.g, m_environmentSettings.waterColor.b);
    glUniform3f(skyUnderseaDeepLoc,      m_environmentSettings.waterDarknessColor.r, m_environmentSettings.waterDarknessColor.g, m_environmentSettings.waterDarknessColor.b);
    glUniform1i(skyUnderseaEnableLoc,    m_environmentSettings.enableWater ? 1 : 0);
    glUniform1i(skyCameraUnderwaterLoc,  cameraUnderwater ? 1 : 0);

    const int skyStarDensityLoc = glGetUniformLocation(m_skydomeProgram, "uStarDensity");
    glUniform1f(skyStarDensityLoc, m_environmentSettings.starDensity);

    if (m_hasSkydomeTexture)
        // Patch: Ignore skydome texture to ensure procedural stars are visible
        // (Do not bind skydome texture, always use procedural shader)

        glBeginQuery(GL_TIME_ELAPSED, gpuQuery);
    // --- Half-resolution sky pass ---
    const int hw = std::max(1, m_viewportWidth  / 2);
    const int hh = std::max(1, m_viewportHeight / 2);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_skyHalfFbo);
    glViewport(0, 0, hw, hh);
    glClear(GL_COLOR_BUFFER_BIT);
    glDepthMask(GL_FALSE);
    {
        const Frustum skyFrustum = extractFrustum(skyMvp);
        const int visibleChunks = m_skydomeChunkCuller.drawVisible(skyFrustum, GL_TRIANGLES, false);
        m_lastSkydomeVisibleVertices = visibleChunks
            * SkydomeChunkCuller::kQuadsPerChunkLat
            * SkydomeChunkCuller::kQuadsPerChunkLon
            * 4;
    }
    glDepthMask(GL_TRUE);
    // Blit half-res sky colour into the main FBO (upscale with GL_LINEAR).
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_skyHalfFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
    glBlitFramebuffer(0, 0, hw, hh,
                      0, 0, m_viewportWidth, m_viewportHeight,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
    // Restore full-res viewport and binding for subsequent passes.
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_viewportWidth, m_viewportHeight);
    glEndQuery(GL_TIME_ELAPSED);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderTerrain
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::renderTerrain(const RenderContext &ctx, unsigned int gpuQuery)
{
    if (!m_environmentSettings.enableTerrain || m_terrainProgram == 0 ||
        m_terrainVao == 0 || m_terrainIndexCount == 0)
        return;

    glUseProgram(m_terrainProgram);

    glm::mat4 terrainModel(1.0f);
    terrainModel = glm::translate(terrainModel, glm::vec3(0.0f, m_environmentSettings.terrainHeight, 0.0f));
    terrainModel = glm::scale(terrainModel, glm::vec3(m_environmentSettings.terrainSize / 220.0f, 1.0f, m_environmentSettings.terrainSize / 220.0f));
    const glm::mat4 terrainWorldModel = ctx.world * terrainModel;
    const glm::mat4 terrainMvp = ctx.projection * ctx.view * terrainWorldModel;

    glUniformMatrix4fv(glGetUniformLocation(m_terrainProgram, "uMvp"),   1, GL_FALSE, glm::value_ptr(terrainMvp));
    glUniformMatrix4fv(glGetUniformLocation(m_terrainProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(terrainWorldModel));
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uBaseA"),        m_environmentSettings.terrainColorA.r, m_environmentSettings.terrainColorA.g, m_environmentSettings.terrainColorA.b);
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uBaseB"),        m_environmentSettings.terrainColorB.r, m_environmentSettings.terrainColorB.g, m_environmentSettings.terrainColorB.b);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uPatchScale"),   m_environmentSettings.terrainPatchScale);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uRoughness"),    m_environmentSettings.terrainRoughness);
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uLightDir"),     ctx.rotatedLightDir.x, ctx.rotatedLightDir.y, ctx.rotatedLightDir.z);
    glUniform1i(glGetUniformLocation(m_terrainProgram, "uTex"),          1);
    glUniform1i(glGetUniformLocation(m_terrainProgram, "uUseTexture"),   m_hasTerrainTexture ? 1 : 0);
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uCameraPos"),    ctx.rotatedCameraPos.x, ctx.rotatedCameraPos.y, ctx.rotatedCameraPos.z);
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uFogColor"),     m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uFogNear"),      m_environmentSettings.fogNear);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uFogFar"),       m_environmentSettings.fogFar);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uFogStrength"),  m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);
    glUniform4f(glGetUniformLocation(m_terrainProgram, "uDiffuseColor"), 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uOpacity"),      1.0f);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uAlphaCutoff"),  0.01f);
    glUniform1i(glGetUniformLocation(m_terrainProgram, "uUnlitShading"), 0);
    glUniform1i(glGetUniformLocation(m_terrainProgram, "uShadowPass"),   0);
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uEmissive"),     0.0f, 0.0f, 0.0f);
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uSunColor"),     m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uSunGlowStrength"), m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f);
    glUniform1i(glGetUniformLocation(m_terrainProgram, "uWaterEnabled"), m_environmentSettings.enableWater ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uWaterLevel"),   m_environmentSettings.waterLevel);
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uWaterHalfExtent"), m_environmentSettings.waterHalfExtent);
    glUniform3f(glGetUniformLocation(m_terrainProgram, "uWaterTint"),    m_environmentSettings.waterColor.r, m_environmentSettings.waterColor.g, m_environmentSettings.waterColor.b);
    glUniformMatrix4fv(glGetUniformLocation(m_terrainProgram, "uInvWorld"), 1, GL_FALSE, glm::value_ptr(ctx.inverseWorld));
    glUniform1f(glGetUniformLocation(m_terrainProgram, "uEnvBloom"),     ctx.envBloom);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_terrainTexture);

    glBeginQuery(GL_TIME_ELAPSED, gpuQuery);
    {
        const Frustum terrainFrustum = extractFrustum(terrainMvp);
        const int visibleChunks = m_terrainChunkCuller.drawVisible(terrainFrustum, GL_TRIANGLES, false);
        m_lastTerrainVisibleVertices = visibleChunks
            * TerrainChunkCuller::kQuadsPerChunk
            * TerrainChunkCuller::kQuadsPerChunk
            * 4;
    }
    glBindVertexArray(0);
    glEndQuery(GL_TIME_ELAPSED);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderWater
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::renderWater(const RenderContext &ctx, unsigned int gpuQuery)
{
    if (!m_environmentSettings.enableWater || m_waterProgram == 0 ||
        m_waterVao == 0 || m_waterIndexCount == 0)
        return;

    glUseProgram(m_waterProgram);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    glUniform1f(glGetUniformLocation(m_waterProgram, "uFoamIntensity"), m_environmentSettings.waterFoamIntensity);

    glm::mat4 waterModel(1.0f);
    waterModel = glm::translate(waterModel, glm::vec3(0.0f, m_environmentSettings.waterLevel, 0.0f));
    const float waterScale = m_environmentSettings.waterHalfExtent / kWaterHalfExtentRender;
    waterModel = glm::scale(waterModel, glm::vec3(waterScale, 1.0f, waterScale));
    const glm::mat4 waterWorldModel = ctx.world * waterModel;
    const glm::mat4 waterMvp = ctx.projection * ctx.view * waterWorldModel;

    glUniformMatrix4fv(glGetUniformLocation(m_waterProgram, "uMvp"),   1, GL_FALSE, glm::value_ptr(waterMvp));
    glUniformMatrix4fv(glGetUniformLocation(m_waterProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(waterWorldModel));
    glUniform3f(glGetUniformLocation(m_waterProgram, "uWaterColor"),   m_environmentSettings.waterColor.r, m_environmentSettings.waterColor.g, m_environmentSettings.waterColor.b);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uOpacity"),      m_environmentSettings.waterOpacity);
    glUniform3f(glGetUniformLocation(m_waterProgram, "uSunColor"),     m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uSunIntensity"), m_environmentSettings.enableSun ? (m_environmentSettings.sunIntensity * m_environmentSettings.waterSunStrength) : 0.0f);
    glUniform3f(glGetUniformLocation(m_waterProgram, "uLightDir"),     ctx.rotatedLightDir.x, ctx.rotatedLightDir.y, ctx.rotatedLightDir.z);
    glUniform3f(glGetUniformLocation(m_waterProgram, "uCameraPos"),    ctx.rotatedCameraPos.x, ctx.rotatedCameraPos.y, ctx.rotatedCameraPos.z);
    glUniform3f(glGetUniformLocation(m_waterProgram, "uFogColor"),     m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uFogNear"),      m_environmentSettings.fogNear);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uFogFar"),       m_environmentSettings.fogFar);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uFogStrength"),  m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uWaveAmplitude"),m_environmentSettings.waveAmplitude);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uWaveFrequency"),m_environmentSettings.waveFrequency);
    glUniform1f(glGetUniformLocation(m_waterProgram, "uTime"),         ctx.elapsedSeconds);

    const glm::mat3 invWorldRot3x3 = glm::transpose(ctx.worldRot3x3);
    glUniformMatrix3fv(glGetUniformLocation(m_waterProgram, "uInvWorldRot"), 1, GL_FALSE, glm::value_ptr(invWorldRot3x3));

    const float reflStrength = m_environmentSettings.enableSun ? m_environmentSettings.waterReflectionStrength : 0.0f;
    glUniform1f(glGetUniformLocation(m_waterProgram, "uReflectionStrength"), reflStrength);

    const Frustum waterFrustum = extractFrustum(waterMvp);

    glBeginQuery(GL_TIME_ELAPSED, gpuQuery);
    // Pass 1: base water colour / alpha
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUniform1i(glGetUniformLocation(m_waterProgram, "uReflectionOnly"), 0);
    {
        const int visibleChunks = m_waterChunkCuller.drawVisible(waterFrustum, GL_TRIANGLES, false);
        m_lastWaterVisibleVertices = visibleChunks
            * WaterChunkCuller::kQuadsPerChunk
            * WaterChunkCuller::kQuadsPerChunk
            * 4;
    }
    // Pass 2: isolated additive reflection
    glBlendFunc(GL_ONE, GL_ONE);
    glUniform1i(glGetUniformLocation(m_waterProgram, "uReflectionOnly"), 1);
    m_waterChunkCuller.drawVisible(waterFrustum, GL_TRIANGLES, false);
    glBindVertexArray(0);
    glEndQuery(GL_TIME_ELAPSED);

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderImportedModel
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::renderImportedModel(const RenderContext &ctx, unsigned int gpuQuery)
{
    if (m_importVao == 0 || m_importTexture == 0 || m_importIndexCount == 0)
        return;

    glUseProgram(m_texturedProgram);

    glm::mat4 model(1.0f);
    model = glm::translate(model, m_importPosition);
    const glm::vec3 rotRad = glm::radians(m_importRotationEuler);
    model = glm::rotate(model, rotRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, rotRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rotRad.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, m_importScale);

    const glm::mat4 worldModel = ctx.world * model;
    const glm::mat4 mvp = ctx.projection * ctx.view * worldModel;

    glUniformMatrix4fv(glGetUniformLocation(m_texturedProgram, "uMvp"),   1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(glGetUniformLocation(m_texturedProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(worldModel));
    glUniform3f(glGetUniformLocation(m_texturedProgram, "uLightPos"),     5.5f, 6.5f, 4.0f);
    glUniform1i(glGetUniformLocation(m_texturedProgram, "uTex"),          0);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uOpacity"),      m_importOpacity);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uAlphaCutoff"),  m_importAlphaCutoff);
    glUniform1i(glGetUniformLocation(m_texturedProgram, "uUnlitShading"), m_importUnlitShading ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_texturedProgram, "uShadowPass"),   0);
    glUniform4f(glGetUniformLocation(m_texturedProgram, "uDiffuseColor"), m_importDiffuseColor.r, m_importDiffuseColor.g, m_importDiffuseColor.b, m_importDiffuseColor.a);
    glUniform3f(glGetUniformLocation(m_texturedProgram, "uEmissive"),     m_importEmissive.r, m_importEmissive.g, m_importEmissive.b);
    glUniform3f(glGetUniformLocation(m_texturedProgram, "uSunColor"),     m_environmentSettings.sunColor.r, m_environmentSettings.sunColor.g, m_environmentSettings.sunColor.b);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uSunGlowStrength"), (m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f) * m_environmentSettings.objectSunGlowStrength);
    glUniform1i(glGetUniformLocation(m_texturedProgram, "uWaterEnabled"), m_environmentSettings.enableWater ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uWaterLevel"),   m_environmentSettings.waterLevel);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uWaterHalfExtent"), m_environmentSettings.waterHalfExtent);
    glUniform3f(glGetUniformLocation(m_texturedProgram, "uWaterTint"),    0.10f, 0.42f, 0.52f);
    glUniformMatrix4fv(glGetUniformLocation(m_texturedProgram, "uInvWorld"), 1, GL_FALSE, glm::value_ptr(ctx.inverseWorld));
    glUniform3f(glGetUniformLocation(m_texturedProgram, "uCameraPos"),    ctx.cameraPos.x, ctx.cameraPos.y, ctx.cameraPos.z);
    glUniform3f(glGetUniformLocation(m_texturedProgram, "uFogColor"),     m_environmentSettings.fogColor.r, m_environmentSettings.fogColor.g, m_environmentSettings.fogColor.b);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uFogNear"),      m_environmentSettings.fogNear);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uFogFar"),       m_environmentSettings.fogFar);
    glUniform1f(glGetUniformLocation(m_texturedProgram, "uFogStrength"),  m_environmentSettings.enableFog ? m_environmentSettings.fogStrength : 0.0f);

    if (m_importAlphaBlend)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }

    glBeginQuery(GL_TIME_ELAPSED, gpuQuery);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_importTexture);
    glBindVertexArray(m_importVao);
    glDrawElements(GL_TRIANGLES, m_importIndexCount, GL_UNSIGNED_INT, nullptr);
    glEndQuery(GL_TIME_ELAPSED);

    if (m_importAlphaBlend)
    {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// renderWeather
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::renderWeather(const RenderContext &ctx, unsigned int gpuQuery)
{
    glBeginQuery(GL_TIME_ELAPSED, gpuQuery);
    m_weatherRenderer.renderRain(ctx.projection, ctx.view, ctx.world,
        m_rainConcept, m_useCustomVisualProfile, m_rainTint, m_rainStyleBoost,
        m_rainIntensity, m_weatherEnabled, m_rainLineWidth, m_rainOpacityScale);
    m_weatherRenderer.renderSplashes(ctx.projection, ctx.view, ctx.world,
        m_rainConcept, m_useCustomVisualProfile, m_splashTint, m_particleStyleBoost,
        m_rainIntensity, m_weatherEnabled, m_splashPointSize, m_splashOpacityScale);
    m_weatherRenderer.renderDroplets(ctx.projection, ctx.view, ctx.world,
        m_rainConcept, m_useCustomVisualProfile, m_dropletTint, m_particleStyleBoost,
        m_rainIntensity, m_weatherEnabled, m_dropletPointSize, m_dropletOpacityScale);
    m_weatherRenderer.renderRipples(ctx.projection, ctx.view, ctx.world,
        m_rainConcept, m_useCustomVisualProfile, m_rippleTint, m_particleStyleBoost,
        m_rainIntensity, m_weatherEnabled, m_ripplePointSize, m_rippleOpacityScale);
    glEndQuery(GL_TIME_ELAPSED);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderGrid
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::renderGrid(const RenderContext &ctx)
{
    if (m_shaderProgram == 0 || m_gridVao == 0 || m_gridRegularCount == 0)
        return;

    glUseProgram(m_shaderProgram);
    const glm::mat4 gridModel(1.0f);
    const glm::mat4 gridMvp = ctx.projection * ctx.view * ctx.world * gridModel;
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uMvp"),   1, GL_FALSE, glm::value_ptr(gridMvp));
    glUniformMatrix4fv(glGetUniformLocation(m_shaderProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(gridModel));
    glUniform3f(glGetUniformLocation(m_shaderProgram, "uLightPos"),
                m_environmentSettings.terrainLightDirection.x * 1000.0f,
                m_environmentSettings.terrainLightDirection.y * 1000.0f,
                m_environmentSettings.terrainLightDirection.z * 1000.0f);
    glUniform1f(glGetUniformLocation(m_shaderProgram, "uGradientStrength"), 0.0f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_gridVao);

    // Regular grid lines — subtle grey
    glUniform3f(glGetUniformLocation(m_shaderProgram, "uColor"), 0.45f, 0.45f, 0.45f);
    glUniform1f(glGetUniformLocation(m_shaderProgram, "uAlpha"), 0.5f);
    glDrawArrays(GL_LINES, 0, m_gridRegularCount);

    // X axis — red
    glUniform3f(glGetUniformLocation(m_shaderProgram, "uColor"), 0.85f, 0.2f, 0.2f);
    glUniform1f(glGetUniformLocation(m_shaderProgram, "uAlpha"), 0.9f);
    glDrawArrays(GL_LINES, m_gridAxisXStart, 2);

    // Z axis — blue
    glUniform3f(glGetUniformLocation(m_shaderProgram, "uColor"), 0.2f, 0.35f, 0.85f);
    glUniform1f(glGetUniformLocation(m_shaderProgram, "uAlpha"), 0.9f);
    glDrawArrays(GL_LINES, m_gridAxisYStart, 2);

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderPostProcess
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::renderPostProcess(const RenderContext &ctx)
{
    m_cameraUnderwater  = false;
    m_usePostProcessed  = false;

    float dt = 1.0f / 60.0f;
    if (m_lastPostProcessElapsed >= 0.0f)
    {
        dt = glm::clamp(ctx.elapsedSeconds - m_lastPostProcessElapsed, 0.0f, 0.2f);
    }
    m_lastPostProcessElapsed = ctx.elapsedSeconds;

    if (m_underwaterProgram == 0 || m_postFbo == 0 ||
        m_postColorTexture == 0 || m_fullscreenVao == 0)
        return;

    const bool wantCinematic = m_environmentSettings.enableCinematic;
    bool  enableUnderwater  = false;
    float underwaterDepth   = 0.0f;

    if (m_environmentSettings.enableWater)
    {
        const bool insideWaterArea =
            std::abs(ctx.cameraPos.x) <= m_environmentSettings.waterHalfExtent &&
            std::abs(ctx.cameraPos.z) <= m_environmentSettings.waterHalfExtent;
        underwaterDepth = m_environmentSettings.waterLevel - ctx.cameraPos.y;
        if (insideWaterArea && underwaterDepth > 0.30f)
        {
            enableUnderwater   = true;
            m_cameraUnderwater = true;
        }
    }

    if (enableUnderwater)
    {
        // Enter quickly so the effect tracks descent, but do not hard-reset on re-entry.
        const float riseRate = 8.0f;
        const float delta = underwaterDepth - m_underwaterVisualDepth;
        const float step = riseRate * dt;
        if (delta > step)
            m_underwaterVisualDepth += step;
        else
            m_underwaterVisualDepth = underwaterDepth;
    }
    else
    {
        // Decay slowly while above water so brief resurfaces keep depth continuity.
        m_underwaterVisualDepth = glm::max(0.0f, m_underwaterVisualDepth - 2.0f * dt);
    }

    if (!enableUnderwater && !wantCinematic)
        return;

    m_usePostProcessed = true;

    glm::vec2 sunUv(0.5f, -0.2f);
    float sunVisible = 0.0f;
    if (enableUnderwater)
    {
        const glm::vec3 lightDirLocal = glm::normalize(m_environmentSettings.terrainLightDirection);
        const float skyRadius = std::max(m_environmentSettings.skydomeRadius, 1.0f);
        const glm::vec3 sunLocal = lightDirLocal * (skyRadius * 0.92f);
        const glm::vec4 sunClip = ctx.projection * ctx.view * ctx.world * glm::vec4(sunLocal, 1.0f);
        if (sunClip.w > 0.0001f)
        {
            const glm::vec3 sunNdc = glm::vec3(sunClip) / sunClip.w;
            sunUv = glm::vec2(sunNdc.x * 0.5f + 0.5f, sunNdc.y * 0.5f + 0.5f);
            const float xVis = 1.0f - glm::clamp((std::abs(sunNdc.x) - 1.0f) * 2.2f, 0.0f, 1.0f);
            const float yVis = 1.0f - glm::clamp((std::abs(sunNdc.y) - 1.0f) * 2.2f, 0.0f, 1.0f);
            const float zVis = (sunNdc.z >= -1.0f && sunNdc.z <= 1.0f) ? 1.0f : 0.0f;
            sunVisible = xVis * yVis * zVis *
                         (m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_postFbo);
    glViewport(0, 0, m_viewportWidth, m_viewportHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    glUseProgram(m_underwaterProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glUniform1i(glGetUniformLocation(m_underwaterProgram, "uSceneTex"),        0);
    glUniform1f(glGetUniformLocation(m_underwaterProgram, "uTime"),            static_cast<float>(glfwGetTime()));
    glUniform1f(glGetUniformLocation(m_underwaterProgram, "uDepth"),           enableUnderwater ? m_underwaterVisualDepth : 0.0f);
    glUniform1f(glGetUniformLocation(m_underwaterProgram, "uDeepSeaDepth"),    m_environmentSettings.underwaterDeepDepth);
    glUniform3f(glGetUniformLocation(m_underwaterProgram, "uWaterTint"),       m_environmentSettings.waterColor.r, m_environmentSettings.waterColor.g, m_environmentSettings.waterColor.b);
    glUniform3f(glGetUniformLocation(m_underwaterProgram, "uDeepColor"),        m_environmentSettings.waterDarknessColor.r, m_environmentSettings.waterDarknessColor.g, m_environmentSettings.waterDarknessColor.b);
    glUniform2f(glGetUniformLocation(m_underwaterProgram, "uSunUv"),           sunUv.x, sunUv.y);
    glUniform1f(glGetUniformLocation(m_underwaterProgram, "uSunVisible"),      sunVisible);
    glUniform1i(glGetUniformLocation(m_underwaterProgram, "uUnderwaterEnabled"), enableUnderwater ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_underwaterProgram, "uCinematicEnabled"), wantCinematic ? 1 : 0);

    glBindVertexArray(m_fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}

// ─────────────────────────────────────────────────────────────────────────────
// render  (orchestration)
// ─────────────────────────────────────────────────────────────────────────────
void Renderer::render(const ViewControls &viewControls)
{
    // Update sun direction from real-world parameters
    std::time_t t = std::time(nullptr);
    std::tm tm_now;
#ifdef _WIN32
    localtime_s(&tm_now, &t);
#else
    tm_now = *std::localtime(&t);
#endif
    int dayOfYear = tm_now.tm_yday + 1;
    if (m_environmentSettings.latitude != 0.0 || m_environmentSettings.longitude != 0.0)
    {
        m_environmentSettings.terrainLightDirection = computeSunDirection(
            m_environmentSettings.latitude,
            m_environmentSettings.longitude,
            m_environmentSettings.utcTime,
            dayOfYear);
    }

    static const auto sRenderStart = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float elapsedSeconds = std::chrono::duration<float>(now - sRenderStart).count();

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_viewportWidth, m_viewportHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.28f, 0.40f, 0.58f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // ── GPU profiling queries ─────────────────────────────────────────────────
    static double gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Count)] = {};
    static int    perfFrameCount = 0;
    static auto   lastPerfTime   = std::chrono::steady_clock::now();

    GLuint gpuQueries[static_cast<int>(GpuProfileSlot::Count) - 1] = {};
    GLuint totalTimestampQueries[2] = {};
    glGenQueries(static_cast<GLsizei>(GpuProfileSlot::Count) - 1, gpuQueries);
    glGenQueries(2, totalTimestampQueries);
    glQueryCounter(totalTimestampQueries[0], GL_TIMESTAMP);

    // ── Build RenderContext ───────────────────────────────────────────────────
    const float aspectRatio = static_cast<float>(m_viewportWidth) / static_cast<float>(m_viewportHeight);
    const glm::vec3 cameraTarget(viewControls.panOffset.x, viewControls.panOffset.y, viewControls.orbitTargetZ);
    const glm::vec3 cameraPos(cameraTarget.x, cameraTarget.y, cameraTarget.z + viewControls.zoomDistance);

    RenderContext ctx;
    ctx.cameraPos    = cameraPos;
    ctx.cameraTarget = cameraTarget;
    ctx.projection   = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, kCameraFarPlane);
    ctx.view         = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));

    ctx.viewRotOnly         = ctx.view;
    ctx.viewRotOnly[0][3]   = 0.0f;
    ctx.viewRotOnly[1][3]   = 0.0f;
    ctx.viewRotOnly[2][3]   = 0.0f;
    ctx.viewRotOnly[3][3]   = 1.0f;

    ctx.world = glm::mat4(1.0f);
    ctx.world = glm::translate(ctx.world, cameraTarget);
    ctx.world = glm::rotate(ctx.world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    ctx.world = glm::rotate(ctx.world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    ctx.world = glm::translate(ctx.world, -cameraTarget);
    ctx.inverseWorld = glm::inverse(ctx.world);

    glm::mat4 worldRotMat(1.0f);
    worldRotMat = glm::rotate(worldRotMat, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    worldRotMat = glm::rotate(worldRotMat, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    ctx.worldRot3x3 = glm::mat3(worldRotMat);

    ctx.rotatedLightDir  = glm::normalize(ctx.worldRot3x3 * m_environmentSettings.terrainLightDirection);
    ctx.rotatedCameraPos = glm::vec3(ctx.world * glm::vec4(cameraPos, 1.0f));
    ctx.elapsedSeconds   = elapsedSeconds;

    // Skydome world: centred on camera, carries world rotation but no target shift
    ctx.skyWorld = glm::mat4(1.0f);
    ctx.skyWorld = glm::translate(ctx.skyWorld, cameraPos);
    ctx.skyWorld = glm::rotate(ctx.skyWorld, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    ctx.skyWorld = glm::rotate(ctx.skyWorld, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));

    // Environment bloom (used by terrain pass)
    {
        const glm::vec3 sunDir  = glm::normalize(m_environmentSettings.terrainLightDirection);
        const float sunDot      = glm::clamp(glm::dot(glm::vec3(0.0f, 1.0f, 0.0f), sunDir), 0.0f, 1.0f);
        const float sunIntensity = m_environmentSettings.enableSun ? m_environmentSettings.sunIntensity : 0.0f;
        ctx.envBloom = std::pow(sunDot, 8.0f) * glm::clamp(sunIntensity, 0.0f, 2.0f);
    }

    // ── Render passes ─────────────────────────────────────────────────────────
    glUseProgram(m_shaderProgram);
    renderSkydome(ctx, gpuQueries[static_cast<int>(GpuProfileSlot::Skydome)]);
    renderTerrain(ctx, gpuQueries[static_cast<int>(GpuProfileSlot::Terrain)]);
    renderWater  (ctx, gpuQueries[static_cast<int>(GpuProfileSlot::Water)]);

    // Bind cloud shadow map before import pass (texture unit 2)
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_cloudShadowTex);
    renderImportedModel(ctx, gpuQueries[static_cast<int>(GpuProfileSlot::Import)]);

    // Overwrite light direction (preserve existing behaviour)
    m_environmentSettings.terrainLightDirection = glm::normalize(glm::vec3(-1.0f, 0.5f, -2.0f));

    renderWeather(ctx, gpuQueries[static_cast<int>(GpuProfileSlot::Weather)]);
    glLineWidth(1.0f);
    glBindVertexArray(0);

    renderGrid(ctx);
    renderPostProcess(ctx);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glQueryCounter(totalTimestampQueries[1], GL_TIMESTAMP);

    // ── Collect GPU profiling results ─────────────────────────────────────────
    GLuint64 queryResultNs[static_cast<int>(GpuProfileSlot::Count) - 1] = {};
    for (int i = 0; i < static_cast<int>(GpuProfileSlot::Count) - 1; ++i)
    {
        glGetQueryObjectui64v(gpuQueries[i], GL_QUERY_RESULT, &queryResultNs[i]);
        gpuProfileAccumMs[i] += static_cast<double>(queryResultNs[i]) / 1000000.0;
    }
    GLuint64 totalStartNs = 0, totalEndNs = 0;
    glGetQueryObjectui64v(totalTimestampQueries[0], GL_QUERY_RESULT, &totalStartNs);
    glGetQueryObjectui64v(totalTimestampQueries[1], GL_QUERY_RESULT, &totalEndNs);
    gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Total)] +=
        static_cast<double>(totalEndNs - totalStartNs) / 1000000.0;
    glDeleteQueries(static_cast<GLsizei>(GpuProfileSlot::Count) - 1, gpuQueries);
    glDeleteQueries(2, totalTimestampQueries);

    perfFrameCount++;
    const auto currentTime = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(currentTime - lastPerfTime).count() >= 2.0)
    {
        const double avgFrames = std::max(perfFrameCount, 1);
        fprintf(stdout,
            "[Renderer GPU Profiling] Skydome: %.2f ms, Terrain: %.2f ms, Water: %.2f ms, "
            "Import: %.2f ms, Weather: %.2f ms, Total: %.2f ms (avg per frame in 2s)\n",
            gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Skydome)] / avgFrames,
            gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Terrain)] / avgFrames,
            gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Water)]   / avgFrames,
            gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Import)]  / avgFrames,
            gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Weather)] / avgFrames,
            gpuProfileAccumMs[static_cast<int>(GpuProfileSlot::Total)]   / avgFrames);
        for (double &v : gpuProfileAccumMs) v = 0.0;
        perfFrameCount = 0;
        lastPerfTime   = currentTime;
    }
}

} // namespace sparks::render
