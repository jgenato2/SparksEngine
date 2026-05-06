#pragma once

#include "shaders/textured/TexturedProgram.hpp"
#include "shaders/skydome/SkydomeProgram.hpp"
#include "shaders/terrain/TerrainProgram.hpp"
#include "shaders/water/WaterProgram.hpp"
#include "shaders/cloud/CloudProgram.hpp"
#include "shaders/underwater/UnderwaterProgram.hpp"

namespace sparks::render {

class ShaderFacade {
public:
    static unsigned int CreateTexturedProgram();
    static unsigned int CreateSkydomeProgram();
    static unsigned int CreateTerrainProgram();
    static unsigned int CreateWaterProgram();
    static unsigned int CreateCloudProgram();
    static unsigned int CreateUnderwaterProgram();
};

} // namespace sparks::render
