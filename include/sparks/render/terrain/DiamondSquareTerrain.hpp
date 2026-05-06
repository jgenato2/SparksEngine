#pragma once
#include <vector>

class DiamondSquareTerrain {
public:
    DiamondSquareTerrain(int size, float roughness, float seed);
    float get(int x, int y) const;
    int size() const;
    const std::vector<float>& data() const;
    DiamondSquareTerrain& operator=(const DiamondSquareTerrain& other);
    void generate(float seed);
private:
    int m_size;
    float m_roughness;
    std::vector<float> m_heightmap;
};
