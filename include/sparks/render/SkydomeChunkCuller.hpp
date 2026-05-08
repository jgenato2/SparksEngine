#pragma once
#include "sparks/render/WaterChunkCuller.hpp" // reuse Frustum / aabbInFrustum
#include <vector>
#include <glad/gl.h>
#include <glm/glm.hpp>

namespace sparks::render {

// ── SkydomeChunkCuller ────────────────────────────────────────────────────────
// Splits the 128-lon × 64-lat unit-sphere skydome into kChunkLon×kChunkLat tiles.
// AABBs are in unit-sphere local space (before the radius scale in skydomeModel).
// The frustum passed to drawVisible must be extracted from the same skyMvp.
class SkydomeChunkCuller {
public:
    static constexpr int kLonSegs         = 128;
    static constexpr int kLatSegs         = 64;
    static constexpr int kChunkLon        = 8;  // chunks around the equator
    static constexpr int kChunkLat        = 8;  // chunks pole-to-pole
    static constexpr int kQuadsPerChunkLon = kLonSegs / kChunkLon; // 16
    static constexpr int kQuadsPerChunkLat = kLatSegs / kChunkLat; // 8

    SkydomeChunkCuller() = default;
    ~SkydomeChunkCuller();

    SkydomeChunkCuller(const SkydomeChunkCuller&) = delete;
    SkydomeChunkCuller& operator=(const SkydomeChunkCuller&) = delete;

    // Build per-chunk VAOs/EBOs.
    //   sharedVbo – the VBO holding all skydome vertex data (stride 3 floats: unit-sphere pos)
    //   positions – flat CPU copy of all (kLonSegs+1)*(kLatSegs+1) unit-sphere vertices
    void initialize(GLuint sharedVbo, const std::vector<glm::vec3>& positions);

    void destroy();

    // Draw only chunks whose AABB intersects the frustum.
    // frustum must be extracted from skyMvp (projection * viewRotOnly * skydomeWorld * skydomeModel).
    // Returns the number of chunks drawn.
    int drawVisible(const Frustum& frustum, GLenum primitiveMode = GL_TRIANGLES, bool enableOcclusionQuery = true) const;

private:
    struct Chunk {
        GLuint    vao{0};
        GLuint    ebo{0};
        int       indexCount{0};
        glm::vec3 aabbMin{};
        glm::vec3 aabbMax{};
        mutable GLuint occlusionQuery{0};
        mutable bool   visiblePrevFrame{true};
        mutable bool   queryPending{false};
        mutable int    framesHidden{0};
    };

    std::vector<Chunk> m_chunks;
};

} // namespace sparks::render
