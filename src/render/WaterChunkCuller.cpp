#include "sparks/render/WaterChunkCuller.hpp"
#include <cmath>
#include <vector>

namespace sparks::render {

// ── Frustum extraction ────────────────────────────────────────────────────────
// Gribb/Hartmann method: extract the 6 clip planes from the combined MVP matrix.
// GLM stores matrices column-major, so m[col][row].
Frustum extractFrustum(const glm::mat4& m) {
    // Build the 4 rows of the matrix
    auto row = [&](int r) -> glm::vec4 {
        return glm::vec4(m[0][r], m[1][r], m[2][r], m[3][r]);
    };
    const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    auto makePlane = [](const glm::vec4& v) -> FrustumPlane {
        const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (len < 1e-9f) return FrustumPlane{};
        return FrustumPlane{glm::vec3(v.x, v.y, v.z) / len, v.w / len};
    };

    Frustum f;
    f.planes[0] = makePlane(r3 + r0); // left
    f.planes[1] = makePlane(r3 - r0); // right
    f.planes[2] = makePlane(r3 + r1); // bottom
    f.planes[3] = makePlane(r3 - r1); // top
    f.planes[4] = makePlane(r3 + r2); // near
    f.planes[5] = makePlane(r3 - r2); // far
    return f;
}

bool aabbInFrustum(const Frustum& frustum,
                   const glm::vec3& minPt,
                   const glm::vec3& maxPt) {
    for (const auto& plane : frustum.planes) {
        // Pick the corner of the AABB that is farthest in the plane's normal direction
        // (the "positive vertex"). If even this corner is outside, the whole box is out.
        const glm::vec3 pv(
            plane.normal.x >= 0.0f ? maxPt.x : minPt.x,
            plane.normal.y >= 0.0f ? maxPt.y : minPt.y,
            plane.normal.z >= 0.0f ? maxPt.z : minPt.z
        );
        if (glm::dot(plane.normal, pv) + plane.d < 0.0f)
            return false; // entirely outside this plane
    }
    return true;
}

// ── WaterChunkCuller ──────────────────────────────────────────────────────────

WaterChunkCuller::~WaterChunkCuller() {
    destroy();
}

void WaterChunkCuller::initialize(GLuint sharedVbo,
                                   float  halfExtent,
                                   float  maxWaveAmplitude) {
    destroy();

    const float totalSize  = halfExtent * 2.0f;
    const float chunkSize  = totalSize / static_cast<float>(kChunkN);
    const float step       = totalSize / static_cast<float>(kGridN);
    const int   vertsPerRow = kGridN + 1; // 129

    (void)step; // not used directly in index math

    m_chunks.resize(static_cast<std::size_t>(kChunkN * kChunkN));

    for (int cr = 0; cr < kChunkN; ++cr) {
        for (int cc = 0; cc < kChunkN; ++cc) {
            Chunk& chunk = m_chunks[static_cast<std::size_t>(cr * kChunkN + cc)];

            // Axis-aligned bounding box in local mesh space
            chunk.aabbMin = glm::vec3(
                -halfExtent + cc * chunkSize,
                -maxWaveAmplitude,
                -halfExtent + cr * chunkSize
            );
            chunk.aabbMax = glm::vec3(
                chunk.aabbMin.x + chunkSize,
                 maxWaveAmplitude,
                chunk.aabbMin.z + chunkSize
            );

            // Build the index list for this chunk's quads
            const int rowStart = cr * kQuadsPerChunk;
            const int colStart = cc * kQuadsPerChunk;

            std::vector<unsigned int> indices;
            indices.reserve(static_cast<std::size_t>(kQuadsPerChunk * kQuadsPerChunk * 6));

            for (int r = rowStart; r < rowStart + kQuadsPerChunk; ++r) {
                for (int c = colStart; c < colStart + kQuadsPerChunk; ++c) {
                    const auto base = static_cast<unsigned int>(r * vertsPerRow + c);
                    const auto right  = base + 1u;
                    const auto up     = base + static_cast<unsigned int>(vertsPerRow);
                    const auto upRight = up + 1u;
                    // Two triangles per quad (same winding as the original mesh)
                    indices.push_back(base);
                    indices.push_back(right);
                    indices.push_back(upRight);
                    indices.push_back(base);
                    indices.push_back(upRight);
                    indices.push_back(up);
                }
            }

            chunk.indexCount = static_cast<int>(indices.size());

            // Each chunk gets its own VAO (sharing the caller's VBO) + its own EBO
            glGenVertexArrays(1, &chunk.vao);
            glGenBuffers(1, &chunk.ebo);

            glBindVertexArray(chunk.vao);

            // Bind the shared vertex buffer and declare the attribute layout.
            // The water VBO contains tightly-packed vec3 positions (stride = 3 floats).
            glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                  3 * static_cast<int>(sizeof(float)),
                                  reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(0);

            // Upload this chunk's index data
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                         indices.data(), GL_STATIC_DRAW);

            glBindVertexArray(0);
            glGenQueries(1, &chunk.occlusionQuery);
        }
    }
}

void WaterChunkCuller::destroy() {
    for (auto& chunk : m_chunks) {
        if (chunk.occlusionQuery != 0) { glDeleteQueries(1, &chunk.occlusionQuery);    chunk.occlusionQuery = 0; }
        if (chunk.ebo != 0)            { glDeleteBuffers(1, &chunk.ebo);               chunk.ebo = 0; }
        if (chunk.vao != 0)            { glDeleteVertexArrays(1, &chunk.vao);          chunk.vao = 0; }
    }
    m_chunks.clear();
}

int WaterChunkCuller::drawVisible(const Frustum& frustum, GLenum primitiveMode, bool enableOcclusionQuery) const {
    static constexpr int kProbeInterval = 30;
    int drawn = 0;
    for (const auto& chunk : m_chunks) {
        if (!aabbInFrustum(frustum, chunk.aabbMin, chunk.aabbMax))
            continue;

        // Non-blocking poll of the previous frame's occlusion query result.
        if (chunk.queryPending) {
            GLuint available = 0;
            glGetQueryObjectuiv(chunk.occlusionQuery, GL_QUERY_RESULT_AVAILABLE, &available);
            if (available) {
                GLuint result = 0;
                glGetQueryObjectuiv(chunk.occlusionQuery, GL_QUERY_RESULT, &result);
                chunk.visiblePrevFrame = (result > 0u);
                chunk.queryPending     = false;
            }
        }

        // Skip geometry that was fully occluded last frame.
        // Re-probe every kProbeInterval frames so occluded chunks can re-appear
        // when the blocking object moves away.
        if (!chunk.visiblePrevFrame) {
            if (++chunk.framesHidden < kProbeInterval)
                continue;
            chunk.visiblePrevFrame = true;
            chunk.framesHidden     = 0;
        }

        if (enableOcclusionQuery)
            glBeginQuery(GL_ANY_SAMPLES_PASSED, chunk.occlusionQuery);

        glBindVertexArray(chunk.vao);
        glDrawElements(primitiveMode, chunk.indexCount, GL_UNSIGNED_INT, nullptr);

        if (enableOcclusionQuery) {
            glEndQuery(GL_ANY_SAMPLES_PASSED);
            chunk.queryPending = true;
        }
        ++drawn;
    }
    return drawn;
}

} // namespace sparks::render
