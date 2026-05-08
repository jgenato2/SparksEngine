#include "sparks/render/TerrainChunkCuller.hpp"
#include <limits>
#include <vector>

namespace sparks::render {

TerrainChunkCuller::~TerrainChunkCuller() {
    destroy();
}

void TerrainChunkCuller::initialize(GLuint sharedVbo,
                                     const std::vector<glm::vec3>& positions) {
    destroy();

    // Constants that mirror the terrain mesh generation in Renderer.cpp:
    //   kGridN = 160 quads, half-extent = 220.0f
    constexpr float kHalfExtent = 220.0f;
    constexpr float kTotalSize  = kHalfExtent * 2.0f;   // 440
    constexpr float kChunkSize  = kTotalSize / static_cast<float>(kChunkN); // 27.5
    const int vertsPerRow = kGridN + 1; // 161

    m_chunks.resize(static_cast<std::size_t>(kChunkN * kChunkN));

    for (int cr = 0; cr < kChunkN; ++cr) {
        for (int cc = 0; cc < kChunkN; ++cc) {
            Chunk& chunk = m_chunks[static_cast<std::size_t>(cr * kChunkN + cc)];

            // XZ extent of this chunk in local mesh space
            const float xMin = -kHalfExtent + cc * kChunkSize;
            const float zMin = -kHalfExtent + cr * kChunkSize;

            // Y extent: scan actual vertex positions in this chunk's vertex range
            float yMin =  std::numeric_limits<float>::max();
            float yMax = -std::numeric_limits<float>::max();

            const int rowStart = cr * kQuadsPerChunk;
            const int colStart = cc * kQuadsPerChunk;
            for (int r = rowStart; r <= rowStart + kQuadsPerChunk; ++r) {
                for (int c = colStart; c <= colStart + kQuadsPerChunk; ++c) {
                    const float y = positions[static_cast<std::size_t>(r * vertsPerRow + c)].y;
                    if (y < yMin) yMin = y;
                    if (y > yMax) yMax = y;
                }
            }
            // Small guard to ensure nonzero height for flat areas
            if (yMin == yMax) { yMin -= 0.1f; yMax += 0.1f; }

            chunk.aabbMin = glm::vec3(xMin,             yMin, zMin);
            chunk.aabbMax = glm::vec3(xMin + kChunkSize, yMax, zMin + kChunkSize);

            // Build the index list for this chunk's quads
            std::vector<unsigned int> indices;
            indices.reserve(static_cast<std::size_t>(kQuadsPerChunk * kQuadsPerChunk * 6));

            for (int r = rowStart; r < rowStart + kQuadsPerChunk; ++r) {
                for (int c = colStart; c < colStart + kQuadsPerChunk; ++c) {
                    const auto base   = static_cast<unsigned int>(r * vertsPerRow + c);
                    const auto right  = base + 1u;
                    const auto up     = base + static_cast<unsigned int>(vertsPerRow);
                    const auto upRight = up + 1u;
                    // Match winding from Renderer terrain index generation
                    indices.push_back(base);
                    indices.push_back(right);
                    indices.push_back(up);
                    indices.push_back(right);
                    indices.push_back(upRight);
                    indices.push_back(up);
                }
            }

            chunk.indexCount = static_cast<int>(indices.size());

            glGenVertexArrays(1, &chunk.vao);
            glGenBuffers(1, &chunk.ebo);

            glBindVertexArray(chunk.vao);

            // Shared VBO — stride 8 floats: pos(3) + normal(3) + uv(2)
            glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                                  8 * static_cast<int>(sizeof(float)),
                                  reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                                  8 * static_cast<int>(sizeof(float)),
                                  reinterpret_cast<void*>(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                                  8 * static_cast<int>(sizeof(float)),
                                  reinterpret_cast<void*>(6 * sizeof(float)));
            glEnableVertexAttribArray(2);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, chunk.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                         indices.data(), GL_STATIC_DRAW);

            glBindVertexArray(0);
            glGenQueries(1, &chunk.occlusionQuery);
        }
    }
}

void TerrainChunkCuller::destroy() {
    for (auto& chunk : m_chunks) {
        if (chunk.occlusionQuery != 0) { glDeleteQueries(1, &chunk.occlusionQuery);    chunk.occlusionQuery = 0; }
        if (chunk.ebo != 0)            { glDeleteBuffers(1, &chunk.ebo);               chunk.ebo = 0; }
        if (chunk.vao != 0)            { glDeleteVertexArrays(1, &chunk.vao);          chunk.vao = 0; }
    }
    m_chunks.clear();
}

int TerrainChunkCuller::drawVisible(const Frustum& frustum, GLenum primitiveMode, bool enableOcclusionQuery) const {
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
