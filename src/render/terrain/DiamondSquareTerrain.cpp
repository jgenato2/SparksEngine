#pragma once
#include "sparks/render/terrain/DiamondSquareTerrain.hpp"
#include <algorithm>
#include <random>
#include <vector>

DiamondSquareTerrain::DiamondSquareTerrain(int size, float roughness, float seed)
    : m_size(size), m_roughness(roughness), m_heightmap(size * size, 0.0f) {
    generate(seed);
}

float DiamondSquareTerrain::get(int x, int y) const {
    x = std::clamp(x, 0, m_size - 1);
    y = std::clamp(y, 0, m_size - 1);
    return m_heightmap[y * m_size + x];
}

int DiamondSquareTerrain::size() const { return m_size; }
const std::vector<float>& DiamondSquareTerrain::data() const { return m_heightmap; }

DiamondSquareTerrain& DiamondSquareTerrain::operator=(const DiamondSquareTerrain& other) {
    if (this != &other) {
        m_size = other.m_size;
        m_roughness = other.m_roughness;
        m_heightmap = other.m_heightmap;
    }
    return *this;
}

void DiamondSquareTerrain::generate(float seed) {
    std::mt19937 rng(static_cast<unsigned int>(seed));
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    m_heightmap[0] = dist(rng);
    m_heightmap[m_size - 1] = dist(rng);
    m_heightmap[(m_size - 1) * m_size] = dist(rng);
    m_heightmap[m_size * m_size - 1] = dist(rng);
    int step = m_size - 1;
    float scale = m_roughness;
    while (step > 1) {
        for (int y = 0; y < m_size - 1; y += step) {
            for (int x = 0; x < m_size - 1; x += step) {
                int midx = x + step / 2;
                int midy = y + step / 2;
                float avg = (get(x, y) + get(x + step, y) + get(x, y + step) + get(x + step, y + step)) * 0.25f;
                m_heightmap[midy * m_size + midx] = avg + dist(rng) * scale;
            }
        }
        for (int y = 0; y < m_size; y += step / 2) {
            for (int x = (y + step / 2) % step; x < m_size; x += step) {
                float sum = 0.0f;
                int count = 0;
                if (x >= step / 2) { sum += get(x - step / 2, y); count++; }
                if (x + step / 2 < m_size) { sum += get(x + step / 2, y); count++; }
                if (y >= step / 2) { sum += get(x, y - step / 2); count++; }
                if (y + step / 2 < m_size) { sum += get(x, y + step / 2); count++; }
                float avg = sum / std::max(count, 1);
                m_heightmap[y * m_size + x] = avg + dist(rng) * scale;
            }
        }
        step /= 2;
        scale *= 0.5f;
    }
}
