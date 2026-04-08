#pragma once

#include <optional>
#include <string>
#include <vector>

#include "sparks/core/Rigging.hpp"
#include "sparks/render/Renderer.hpp"

namespace sparks::core {


struct FbxAnimationSettings {
    bool importAnimations = true;
    float animationSampleRate = 30.0f;
    bool importAllAnimations = true;
    int selectedAnimationIndex = 0;
    // Add more settings as needed
};

std::optional<render::ImportedModelData> loadFbxModel(
    const std::string& filePath,
    std::string& errorMessage,
    std::vector<rigging::RigBone>* importedRigBones,
    std::vector<rigging::VertexGroupInfo>* importedVertexGroups,
    const FbxAnimationSettings* animationSettings = nullptr);

}  // namespace sparks::core
