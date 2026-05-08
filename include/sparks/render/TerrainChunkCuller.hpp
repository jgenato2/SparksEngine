#pragma once
#include "sparks/render/WaterChunkCuller.hpp" // reuse Frustum / aabbInFrustum
#include <vector>
#include <glad/gl.h>
#include <glm/glm.hpp>

namespace sparks::render {

// ── TerrainChunkCuller ────────────────────────────────────────────────────────
// Splits a kGridN×kGridN terrain quad-grid into kChunkN×kChunkN tiles.
// Per-chunk AABBs incorporate actual vertex heights so nothing visible is
// culled even on steep hills.
class TerrainChunkCuller {
public:
    static constexpr int kGridN         = 160; // quads per side
    static constexpr int kChunkN        = 16;  // tiles per side
    static constexpr int kQuadsPerChunk = kGridN / kChunkN; // 10

    TerrainChunkCuller() = default;
    ~TerrainChunkCuller();

    TerrainChunkCuller(const TerrainChunkCuller&) = delete;
    TerrainChunkCuller& operator=(const TerrainChunkCuller&) = delete;

    // Build per-chunk VAOs/EBOs.
    //   sharedVbo  – the VBO that holds all terrain vertex data (stride 8 floats: pos+normal+uv)
    //   positions  – flat CPU copy of all (kGridN+1)^2 vertex positions used to compute Y AABB
    void initialize(GLuint sharedVbo, const std::vector<glm::vec3>& positions);

    void destroy();

    // Draw only chunks whose AABB intersects the frustum.
    // The frustum must be extracted from the MVP used to render the terrain.
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
