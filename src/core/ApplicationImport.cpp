#include "sparks/core/ApplicationImport.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include <glm/common.hpp>
#include <glm/vec3.hpp>
#include <tinyfiledialogs.h>

#include "sparks/core/FbxImport.hpp"

namespace sparks::core::import_util {

std::string formatVec3(const glm::vec3& value) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f)", value.x, value.y, value.z);
    return std::string(buffer);
}

glm::vec3 importedModelWorldDimensions(const render::ImportedModelData& model) {
    const glm::vec3 scaled = model.dimensions * model.scale;
    return glm::vec3(std::abs(scaled.x), std::abs(scaled.y), std::abs(scaled.z));
}

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
    int& selectedVertexGroup) {
    (void)viewControls;
    importedModel = importedModelData;
    importedModelSelected = true;
    if (!importedRigBones.empty()) {
        rigImportedSourceBones = importedRigBones;
        rigBones = importedRigBones;
        selectedRigBone = 0;
        rigRootBone = 0;
        humanoidBoneMap.fill(-1);
        rigHasUnsavedChanges = false;
    }

    rigVertexGroups = importedVertexGroups;
    selectedVertexGroup = rigVertexGroups.empty() ? -1 : 0;
    renderer.setImportedModel(importedModelData);
}

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
    std::string& importStatus) {
    const char* patterns[] = {"*.fbx", "*.FBX"};
    const char* selectedPath = tinyfd_openFileDialog(
        "Import FBX",
        "",
        2,
        patterns,
        "FBX Files",
        0);

    if (selectedPath == nullptr) {
        return;
    }

    std::string error;
    std::vector<rigging::RigBone> importedRigBones;
    std::vector<rigging::VertexGroupInfo> importedVertexGroups;
    const auto imported = loadFbxModel(selectedPath, error, &importedRigBones, &importedVertexGroups);
    if (!imported.has_value()) {
        importStatus = std::string("FBX import failed: ") + error;
        return;
    }

    applyImportedRigState(
        importedRigBones,
        importedVertexGroups,
        *imported,
        renderer,
        viewControls,
        importedModel,
        importedModelSelected,
        rigImportedSourceBones,
        rigBones,
        selectedRigBone,
        rigRootBone,
        humanoidBoneMap,
        rigHasUnsavedChanges,
        rigVertexGroups,
        selectedVertexGroup);

    importStatus = std::string("Imported: ")
        + std::filesystem::path(selectedPath).filename().string()
        + " | Pos " + formatVec3(imported->position)
        + " | Rot " + formatVec3(imported->rotationEulerDegrees)
        + " | Scale " + formatVec3(imported->scale)
        + " | Dim " + formatVec3(importedModelWorldDimensions(*imported))
        + " | Bones " + std::to_string(importedRigBones.size());
}

}  // namespace sparks::core::import_util
