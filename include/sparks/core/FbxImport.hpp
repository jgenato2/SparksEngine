#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sparks/core/Rigging.hpp"
#include "sparks/render/Renderer.hpp"

namespace sparks::core {

std::optional<render::ImportedModelData> loadFbxModel(
    const std::string& filePath,
    std::string& errorMessage,
    std::vector<rigging::RigBone>* importedRigBones,
    std::vector<rigging::VertexGroupInfo>* importedVertexGroups);

}  // namespace sparks::core
