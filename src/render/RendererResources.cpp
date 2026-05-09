#include <cmath>
#include <algorithm>
#include <vector>

#include <glad/gl.h>
#include <glm/common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "sparks/render/Renderer.hpp"
#include "sparks/render/RendererAstronomy.hpp"
#include "sparks/render/terrain/DiamondSquareTerrain.hpp"

namespace sparks::render {

constexpr float kWaterHalfExtentRes = 220.0f; // matches createWaterResources grid half-extent

void Renderer::createSkydomeResources()
{
    constexpr int lonSegments = 128;
    constexpr int latSegments = 64;
    std::vector<float> skyVertices;
    std::vector<unsigned int> skyIndices;
    skyVertices.reserve(static_cast<std::size_t>((lonSegments + 1) * (latSegments + 1)) * 3);
    skyIndices.reserve(static_cast<std::size_t>(lonSegments * latSegments) * 6);

    for (int y = 0; y <= latSegments; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(latSegments);
        const float theta = v * 3.1415926535f;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        for (int x = 0; x <= lonSegments; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(lonSegments);
            const float phi = u * 6.283185307f;
            skyVertices.push_back(std::cos(phi) * sinTheta);
            skyVertices.push_back(cosTheta);
            skyVertices.push_back(std::sin(phi) * sinTheta);
        }
    }

    for (int y = 0; y < latSegments; ++y)
    {
        for (int x = 0; x < lonSegments; ++x)
        {
            const int rowA = y * (lonSegments + 1);
            const int rowB = (y + 1) * (lonSegments + 1);
            skyIndices.push_back(static_cast<unsigned int>(rowA + x));
            skyIndices.push_back(static_cast<unsigned int>(rowB + x));
            skyIndices.push_back(static_cast<unsigned int>(rowA + x + 1));
            skyIndices.push_back(static_cast<unsigned int>(rowA + x + 1));
            skyIndices.push_back(static_cast<unsigned int>(rowB + x));
            skyIndices.push_back(static_cast<unsigned int>(rowB + x + 1));
        }
    }

    glGenVertexArrays(1, &m_skydomeVao);
    glGenBuffers(1, &m_skydomeVbo);
    glGenBuffers(1, &m_skydomeEbo);
    glBindVertexArray(m_skydomeVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_skydomeVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<long long>(skyVertices.size() * sizeof(float)),
                 skyVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_skydomeEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<long long>(skyIndices.size() * sizeof(unsigned int)),
                 skyIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);
    m_skydomeIndexCount = static_cast<int>(skyIndices.size());

    const std::size_t numVerts = skyVertices.size() / 3;
    std::vector<glm::vec3> skyPositions(numVerts);
    for (std::size_t i = 0; i < numVerts; ++i)
        skyPositions[i] = glm::vec3(skyVertices[i * 3], skyVertices[i * 3 + 1], skyVertices[i * 3 + 2]);
    m_skydomeChunkCuller.initialize(m_skydomeVbo, skyPositions);
}

void Renderer::createCloudResources()
{
    const glm::vec3 points[4] = {
        glm::vec3(-0.72f, -0.48f, 0.0f),
        glm::vec3( 0.72f, -0.48f, 0.0f),
        glm::vec3( 0.72f,  0.48f, 0.0f),
        glm::vec3(-0.72f,  0.48f, 0.0f),
    };
    const glm::vec2 uvs[4] = {
        glm::vec2(-1.0f, -1.0f),
        glm::vec2( 1.0f, -1.0f),
        glm::vec2( 1.0f,  1.0f),
        glm::vec2(-1.0f,  1.0f),
    };

    std::vector<float> cloudVertices;
    cloudVertices.reserve(4 * 6);
    for (int i = 0; i < 4; ++i)
    {
        cloudVertices.push_back(points[i].x);
        cloudVertices.push_back(points[i].y);
        cloudVertices.push_back(points[i].z);
        cloudVertices.push_back(uvs[i].x);
        cloudVertices.push_back(uvs[i].y);
        cloudVertices.push_back(0.0f);
    }
    const std::vector<unsigned int> cloudIndices = {0u, 1u, 2u, 2u, 3u, 0u};

    glGenVertexArrays(1, &m_cloudVao);
    glGenBuffers(1, &m_cloudVbo);
    glGenBuffers(1, &m_cloudSortedEbo);
    glBindVertexArray(m_cloudVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_cloudVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<long long>(cloudVertices.size() * sizeof(float)),
                 cloudVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cloudSortedEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<long long>(cloudIndices.size() * sizeof(unsigned int)),
                 cloudIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 6 * static_cast<int>(sizeof(float)),
                          reinterpret_cast<void *>(5 * sizeof(float)));
    glEnableVertexAttribArray(2);
    m_cloudIndexCount = static_cast<int>(cloudIndices.size());
}

void Renderer::createTerrainResources()
{
    constexpr int kTerrainN = 160;
    constexpr float kTerrainSize = 220.0f;
    const float terrainStep = kTerrainSize * 2.0f / static_cast<float>(kTerrainN);

    std::vector<unsigned int> terrainIndices;
    terrainIndices.reserve(static_cast<std::size_t>(kTerrainN * kTerrainN) * 6);
    for (int row = 0; row < kTerrainN; ++row)
    {
        for (int col = 0; col < kTerrainN; ++col)
        {
            const unsigned int base = static_cast<unsigned int>(row * (kTerrainN + 1) + col);
            terrainIndices.push_back(base);
            terrainIndices.push_back(base + 1u);
            terrainIndices.push_back(base + static_cast<unsigned int>(kTerrainN + 1));
            terrainIndices.push_back(base + 1u);
            terrainIndices.push_back(base + static_cast<unsigned int>(kTerrainN + 2));
            terrainIndices.push_back(base + static_cast<unsigned int>(kTerrainN + 1));
        }
    }

    DiamondSquareTerrain diamondTerrain(kTerrainN + 1, m_environmentSettings.terrainRoughness, 42.0f);

    std::vector<glm::vec3> positions((kTerrainN + 1) * (kTerrainN + 1));
    for (int row = 0; row <= kTerrainN; ++row)
    {
        for (int col = 0; col <= kTerrainN; ++col)
        {
            float x = -kTerrainSize + static_cast<float>(col) * terrainStep;
            float z = -kTerrainSize + static_cast<float>(row) * terrainStep;
            float y = diamondTerrain.get(col, row) * m_environmentSettings.terrainPatchScale * 20.0f;
            positions[row * (kTerrainN + 1) + col] = glm::vec3(x, y, z);
        }
    }

    // Smooth the generated heightfield to reduce jagged facets.
    {
        const int side = kTerrainN + 1;
        std::vector<float> smoothed(static_cast<std::size_t>(side * side));
        constexpr int kSmoothPasses = 2;
        for (int pass = 0; pass < kSmoothPasses; ++pass)
        {
            for (int row = 0; row < side; ++row)
            {
                for (int col = 0; col < side; ++col)
                {
                    float sum = 0.0f;
                    int count = 0;
                    for (int dr = -1; dr <= 1; ++dr)
                    {
                        const int rr = row + dr;
                        if (rr < 0 || rr >= side)
                            continue;
                        for (int dc = -1; dc <= 1; ++dc)
                        {
                            const int cc = col + dc;
                            if (cc < 0 || cc >= side)
                                continue;
                            sum += positions[rr * side + cc].y;
                            ++count;
                        }
                    }
                    const std::size_t idx = static_cast<std::size_t>(row * side + col);
                    const float avg = sum / static_cast<float>(std::max(count, 1));
                    // Preserve some original form while smoothing.
                    smoothed[idx] = glm::mix(positions[idx].y, avg, 0.55f);
                }
            }
            for (int i = 0; i < side * side; ++i)
            {
                positions[i].y = smoothed[static_cast<std::size_t>(i)];
            }
        }
    }

    // Each vertex: 3 pos + 3 normal + 2 uv = 8 floats
    std::vector<float> terrainVerticesFull;
    terrainVerticesFull.reserve((kTerrainN + 1) * (kTerrainN + 1) * 8);
    for (int row = 0; row <= kTerrainN; ++row)
    {
        for (int col = 0; col <= kTerrainN; ++col)
        {
            int idx = row * (kTerrainN + 1) + col;
            glm::vec3 pos = positions[idx];
            glm::vec3 left  = positions[idx - (col > 0 ? 1 : 0)];
            glm::vec3 right = positions[idx + (col < kTerrainN ? 1 : 0)];
            glm::vec3 down  = positions[idx - (row > 0 ? (kTerrainN + 1) : 0)];
            glm::vec3 up    = positions[idx + (row < kTerrainN ? (kTerrainN + 1) : 0)];
            glm::vec3 dx = right - left;
            glm::vec3 dz = up - down;
            glm::vec3 normal = glm::normalize(glm::cross(dz, dx));
            float u = (pos.x + kTerrainSize) / (2.0f * kTerrainSize);
            float v = (pos.z + kTerrainSize) / (2.0f * kTerrainSize);
            terrainVerticesFull.push_back(pos.x);
            terrainVerticesFull.push_back(pos.y);
            terrainVerticesFull.push_back(pos.z);
            terrainVerticesFull.push_back(normal.x);
            terrainVerticesFull.push_back(normal.y);
            terrainVerticesFull.push_back(normal.z);
            terrainVerticesFull.push_back(u);
            terrainVerticesFull.push_back(v);
        }
    }

    glGenVertexArrays(1, &m_terrainVao);
    glGenBuffers(1, &m_terrainVbo);
    glGenBuffers(1, &m_terrainEbo);
    glBindVertexArray(m_terrainVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_terrainVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<long long>(terrainVerticesFull.size() * sizeof(float)),
                 terrainVerticesFull.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_terrainEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<long long>(terrainIndices.size() * sizeof(unsigned int)),
                 terrainIndices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * static_cast<int>(sizeof(float)),
                          reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * static_cast<int>(sizeof(float)),
                          reinterpret_cast<void *>(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * static_cast<int>(sizeof(float)),
                          reinterpret_cast<void *>(6 * sizeof(float)));
    glEnableVertexAttribArray(2);
    m_terrainIndexCount = static_cast<int>(terrainIndices.size());

    m_terrainChunkCuller.initialize(m_terrainVbo, positions);
}

void Renderer::createWaterResources()
{
    constexpr int kWaterN = 128;
    constexpr float kHalfExtent = kWaterHalfExtentRes;
    constexpr float kStep = kHalfExtent * 2.0f / static_cast<float>(kWaterN);

    std::vector<float> wVerts;
    std::vector<unsigned int> wIdx;
    wVerts.reserve(static_cast<std::size_t>((kWaterN + 1) * (kWaterN + 1)) * 3);
    wIdx.reserve(static_cast<std::size_t>(kWaterN * kWaterN) * 6);

    for (int row = 0; row <= kWaterN; ++row)
    {
        for (int col = 0; col <= kWaterN; ++col)
        {
            wVerts.push_back(-kHalfExtent + static_cast<float>(col) * kStep);
            wVerts.push_back(0.0f);
            wVerts.push_back(-kHalfExtent + static_cast<float>(row) * kStep);
        }
    }
    for (int row = 0; row < kWaterN; ++row)
    {
        for (int col = 0; col < kWaterN; ++col)
        {
            const unsigned int base = static_cast<unsigned int>(row * (kWaterN + 1) + col);
            wIdx.push_back(base);
            wIdx.push_back(base + 1u);
            wIdx.push_back(base + static_cast<unsigned int>(kWaterN + 2));
            wIdx.push_back(base);
            wIdx.push_back(base + static_cast<unsigned int>(kWaterN + 2));
            wIdx.push_back(base + static_cast<unsigned int>(kWaterN + 1));
        }
    }

    glGenVertexArrays(1, &m_waterVao);
    glGenBuffers(1, &m_waterVbo);
    glGenBuffers(1, &m_waterEbo);
    glBindVertexArray(m_waterVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_waterVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(wVerts.size() * sizeof(float)),
                 wVerts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_waterEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(wIdx.size() * sizeof(unsigned int)),
                 wIdx.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * static_cast<int>(sizeof(float)), nullptr);
    glEnableVertexAttribArray(0);
    m_waterIndexCount = static_cast<int>(wIdx.size());

    // Max Gerstner displacement ≈ 0.5; use 1.0 as conservative AABB headroom.
    m_waterChunkCuller.initialize(m_waterVbo, kWaterHalfExtentRes, 1.0f);
}

void Renderer::createEnvironmentResources()
{
    createSkydomeResources();
    m_starList = generateStarList(16, 12);
    createCloudResources();
    createTerrainResources();
    createWaterResources();
    glBindVertexArray(0);
}

} // namespace sparks::render
