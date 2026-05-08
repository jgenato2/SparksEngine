#include "sparks/render/SkydomeChunkCuller.hpp"
#include <limits>
#include <vector>

namespace sparks::render {

SkydomeChunkCuller::~SkydomeChunkCuller() {
    destroy();
}

void SkydomeChunkCuller::initialize(GLuint sharedVbo,
                                     const std::vector<glm::vec3>& positions) {
    destroy();

    // Vertex layout: (kLonSegs+1) verts per latitude ring, (kLatSegs+1) rings total.
    const int vertsPerRow = kLonSegs + 1; // 129

    m_chunks.resize(static_cast<std::size_t>(kChunkLon * kChunkLat));

    for (int cl = 0; cl < kChunkLat; ++cl) {
        for (int cm = 0; cm < kChunkLon; ++cm) {
            Chunk& chunk = m_chunks[static_cast<std::size_t>(cl * kChunkLon + cm)];

            const int latRowStart = cl * kQuadsPerChunkLat;
            const int lonColStart = cm * kQuadsPerChunkLon;

            // Compute tight AABB from all vertices in this patch (unit-sphere space).
            float xMin =  std::numeric_limits<float>::max();
            float yMin =  std::numeric_limits<float>::max();
            float zMin =  std::numeric_limits<float>::max();
            float xMax = -std::numeric_limits<float>::max();
            float yMax = -std::numeric_limits<float>::max();
            float zMax = -std::numeric_limits<float>::max();

            for (int r = latRowStart; r <= latRowStart + kQuadsPerChunkLat; ++r) {
                for (int c = lonColStart; c <= lonColStart + kQuadsPerChunkLon; ++c) {
                    const glm::vec3& v = positions[static_cast<std::size_t>(r * vertsPerRow + c)];
                    if (v.x < xMin) xMin = v.x;  if (v.x > xMax) xMax = v.x;
                    if (v.y < yMin) yMin = v.y;  if (v.y > yMax) yMax = v.y;
                    if (v.z < zMin) zMin = v.z;  if (v.z > zMax) zMax = v.z;
                }
            }
            // Small guard for degenerate single-point patches (e.g. pole).
            if (xMin == xMax) { xMin -= 0.001f; xMax += 0.001f; }
            if (yMin == yMax) { yMin -= 0.001f; yMax += 0.001f; }
            if (zMin == zMax) { zMin -= 0.001f; zMax += 0.001f; }

            chunk.aabbMin = glm::vec3(xMin, yMin, zMin);
            chunk.aabbMax = glm::vec3(xMax, yMax, zMax);

            // Build index list for this chunk's quads.
            std::vector<unsigned int> indices;
            indices.reserve(static_cast<std::size_t>(kQuadsPerChunkLat * kQuadsPerChunkLon * 6));

            for (int r = latRowStart; r < latRowStart + kQuadsPerChunkLat; ++r) {
                for (int c = lonColStart; c < lonColStart + kQuadsPerChunkLon; ++c) {
                    const auto rowA  = static_cast<unsigned int>(r * vertsPerRow);
                    const auto rowB  = static_cast<unsigned int>((r + 1) * vertsPerRow);
                    const auto colA  = static_cast<unsigned int>(c);
                    const auto colB  = static_cast<unsigned int>(c + 1);
                    // Match winding from Renderer skydome index generation:
                    // rowA+c, rowB+c, rowA+c+1,  rowA+c+1, rowB+c, rowB+c+1
                    indices.push_back(rowA + colA);
                    indices.push_back(rowB + colA);
                    indices.push_back(rowA + colB);
                    indices.push_back(rowA + colB);
                    indices.push_back(rowB + colA);
                    indices.push_back(rowB + colB);
                }
            }

            chunk.indexCount = static_cast<int>(indices.size());

            glGenVertexArrays(1, &chunk.vao);
            glGenBuffers(1, &chunk.ebo);

            glBindVertexArray(chunk.vao);

            // Shared VBO — stride 3 floats: unit-sphere pos only.
            glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                  3 * static_cast<int>(sizeof(float)),
                                  reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(0);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                         indices.data(), GL_STATIC_DRAW);

            glBindVertexArray(0);
            glGenQueries(1, &chunk.occlusionQuery);
        }
    }
}

void SkydomeChunkCuller::destroy() {
    for (auto& chunk : m_chunks) {
        if (chunk.occlusionQuery != 0) { glDeleteQueries(1, &chunk.occlusionQuery);    chunk.occlusionQuery = 0; }
        if (chunk.ebo != 0)            { glDeleteBuffers(1, &chunk.ebo);               chunk.ebo = 0; }
        if (chunk.vao != 0)            { glDeleteVertexArrays(1, &chunk.vao);          chunk.vao = 0; }
    }
    m_chunks.clear();
}

int SkydomeChunkCuller::drawVisible(const Frustum& frustum, GLenum primitiveMode, bool enableOcclusionQuery) const {
    static constexpr int kProbeInterval = 30;
    int drawn = 0;
    for (const auto& chunk : m_chunks) {
        if (!aabbInFrustum(frustum, chunk.aabbMin, chunk.aabbMax))
            continue;

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
