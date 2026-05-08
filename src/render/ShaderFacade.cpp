#include "sparks/render/ShaderFacade.hpp"

namespace sparks::render {

unsigned int ShaderFacade::CreateTexturedProgram()  { return createTexturedProgram(); }
unsigned int ShaderFacade::CreateSkydomeProgram()   { return createSkydomeProgram(); }
unsigned int ShaderFacade::CreateTerrainProgram()   { return createTerrainProgram(); }
unsigned int ShaderFacade::CreateWaterProgram()     { return createWaterProgram(); }
unsigned int ShaderFacade::CreateCloudProgram()     { return createCloudProgram(); }
unsigned int ShaderFacade::CreateUnderwaterProgram(){ return createUnderwaterProgram(); }
unsigned int ShaderFacade::CreateObjectProgram()    { return createObjectProgram(); }

} // namespace sparks::render
