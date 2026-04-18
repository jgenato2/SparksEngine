#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <imgui.h>

#include "sparks/core/Rigging.hpp"
#include "sparks/render/Renderer.hpp"

namespace sparks::core::ui {

enum class TransformMode {
    Move,
    Rotate,
    Scale,
};

enum class TransformAxis {
    None,
    X,
    Y,
    Z,
    View,
};

void drawStatusBar(TransformMode transformMode, bool panModeEnabled, const std::string& importStatus, float statusBarHeight);

void drawSceneTab(
    render::Renderer& renderer,
    ImGuiIO& io,
    render::ViewControls& viewControls,
    render::EnvironmentSettings& environmentSettings,
    std::vector<int>& selectedCloudIndices,
    bool panModeEnabled,
    TransformMode& transformMode,
    TransformAxis& activeTransformAxis,
    bool& transformDragging,
    ImVec2& transformDragStartMouse,
    ImVec2& transformDragPrevMouse,
    float& transformRotateAccumDeg,
    glm::vec2& transformDragStartWorldRotation,
    glm::vec3& transformDragStartSelectedCenter,
    bool& selectionDragging,
    ImVec2& selectionStart,
    ImVec2& selectionEnd,
    std::optional<render::ImportedModelData>& importedModel,
    std::optional<render::ImportedModelData>& transformDragStartImportedModel,
    std::vector<render::CloudObjectSettings>& transformDragStartClouds,
    bool& importedModelSelected);

void drawRiggingTab(
    std::vector<rigging::RigBone>& rigBones,
    const std::vector<rigging::RigBone>& rigImportedSourceBones,
    std::vector<rigging::VertexGroupInfo>& rigVertexGroups,
    int& selectedVertexGroup,
    int& selectedRigBone,
    int& rigAvatarDefinition,
    int& rigAnimationType,
    bool& rigOptimizeGameObjects,
    bool& rigHasUnsavedChanges,
    int& rigRootBone,
    std::array<int, 15>& humanoidBoneMap,
    bool importedModelSelected,
    const std::optional<render::ImportedModelData>& importedModel);

void drawRightPane(
    float contentHeight,
    ImGuiIO& io,
    render::Renderer& renderer,
    render::EnvironmentSettings& environmentSettings,
    std::vector<int>& selectedCloudIndices,
    std::optional<render::ImportedModelData>& importedModel,
    bool& importedModelSelected);


// Global FPS variable for status bar
extern float gStatusBarFps;

}  // namespace sparks::core::ui
