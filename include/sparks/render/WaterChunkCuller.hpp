#pragma once
#include <array>
#include <vector>
#include <glad/gl.h>
#include <glm/glm.hpp>

namespace sparks::render {

// ── Frustum (6 planes) ────────────────────────────────────────────────────────
// Each plane is stored as (normal, d) where dot(normal, p) + d >= 0 means p is
// on the inside. Extracted from a combined clip-space matrix.
struct FrustumPlane {
    glm::vec3 normal{};
    float     d{0.0f};
};

struct Frustum {
    std::array<FrustumPlane, 6> planes{};
};

// Extract a frustum from the combined projection*view*(model) matrix.
Frustum extractFrustum(const glm::mat4& mvp);

// Returns true when the AABB [minPt, maxPt] overlaps the frustum.
// Conservative: may return true for boxes just outside (no false negatives).
bool aabbInFrustum(const Frustum& frustum,
                   const glm::vec3& minPt,
                   const glm::vec3& maxPt);

// ── WaterChunkCuller ─────────────────────────────────────────────────────────
// Splits the flat 128×128-quad water grid into kChunkN×kChunkN tiles.
// Each tile owns its own VAO + EBO (sharing the caller-supplied VBO).
// On each draw call only tiles that intersect the frustum are rendered.
class WaterChunkCuller {
public:
    static constexpr int kGridN          = 128; // quads per side of the full grid
    static constexpr int kChunkN         = 16;  // tiles per side
    static constexpr int kQuadsPerChunk  = kGridN / kChunkN; // 8

    WaterChunkCuller() = default;
    ~WaterChunkCuller();

    WaterChunkCuller(const WaterChunkCuller&) = delete;
    WaterChunkCuller& operator=(const WaterChunkCuller&) = delete;

    // Build per-chunk VAOs/EBOs.
    //   sharedVbo       – the VBO that holds all water vertex data (stride 3 floats, pos only)
    //   halfExtent      – half-size of the mesh in local XZ (e.g. 220)
    //   maxWaveAmplitude – conservative Y headroom for the AABB (e.g. 0.5)
    void initialize(GLuint sharedVbo, float halfExtent, float maxWaveAmplitude);

    void destroy();

    // Bind & draw only chunks whose AABB intersects the frustum.
    // The frustum must be extracted from the same MVP that will be used to render
    // the water (i.e. projection * view * waterWorldModel).
    // Returns the number of chunks drawn.
    // enableOcclusionQuery: set false for additional same-frame passes (e.g. water pass 2)
    // to avoid re-issuing queries on in-flight objects.
    int drawVisible(const Frustum& frustum, GLenum primitiveMode = GL_TRIANGLES, bool enableOcclusionQuery = true) const;

private:
    struct Chunk {
        GLuint    vao{0};
        GLuint    ebo{0};
        int       indexCount{0};
        glm::vec3 aabbMin{};
        glm::vec3 aabbMax{};
        // Occlusion culling state — mutable so drawVisible() stays const
        mutable GLuint occlusionQuery{0};
        mutable bool   visiblePrevFrame{true};
        mutable bool   queryPending{false};
        mutable int    framesHidden{0};
    };

    std::vector<Chunk> m_chunks;
};

} // namespace sparks::render
