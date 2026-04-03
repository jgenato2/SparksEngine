#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "sparks/core/Rigging.hpp"
#include "sparks/render/Renderer.hpp"

namespace sparks::core::import_util {

std::string formatVec3(const glm::vec3& value);

glm::vec3 importedModelWorldDimensions(const render::ImportedModelData& model);

float importedModelFocusZoom(const render::ImportedModelData& model);

void applyImportedRigState(
    const std::vector<rigging::RigBone>& importedRigBones,
    const std::vector<rigging::VertexGroupInfo>& importedVertexGroups,
    const render::ImportedModelData& importedModelData,
    render::Renderer& renderer,
    render::ViewControls& viewControls,
    std::optional<render::ImportedModelData>& importedModel,
    bool& importedModelSelected,
    std::vector<rigging::RigBone>& rigImportedSourceBones,
    std::vector<rigging::RigBone>& rigBones,
    int& selectedRigBone,
    int& rigRootBone,
    std::array<int, 15>& humanoidBoneMap,
    bool& rigHasUnsavedChanges,
    std::vector<rigging::VertexGroupInfo>& rigVertexGroups,
    int& selectedVertexGroup);

void importFbxFromDialog(
    render::Renderer& renderer,
    render::ViewControls& viewControls,
    std::optional<render::ImportedModelData>& importedModel,
    bool& importedModelSelected,
    std::vector<rigging::RigBone>& rigImportedSourceBones,
    std::vector<rigging::RigBone>& rigBones,
    int& selectedRigBone,
    int& rigRootBone,
    std::array<int, 15>& humanoidBoneMap,
    bool& rigHasUnsavedChanges,
    std::vector<rigging::VertexGroupInfo>& rigVertexGroups,
    int& selectedVertexGroup,
    std::string& importStatus);

}  // namespace sparks::core::import_util
