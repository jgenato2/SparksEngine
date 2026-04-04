#include "sparks/core/Application.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "sparks/core/ApplicationImport.hpp"
#include "sparks/core/ApplicationUi.hpp"
#include "sparks/core/WeatherSystem.hpp"
#include "sparks/render/Renderer.hpp"

namespace {

constexpr float kMinCameraZoom = 1.5f;
constexpr float kMaxCameraZoom = 250.0f;

using sparks::core::rigging::RigBone;
using sparks::core::rigging::VertexGroupInfo;
using sparks::core::import_util::importFbxFromDialog;

} // namespace

namespace sparks::core {

Application::Application() {
    initializeWindow();
    initializeImGui();
}

Application::~Application() {
    shutdownImGui();

    if (m_window != nullptr) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();
}

int Application::run() {
    using TransformMode = ui::TransformMode;
    using TransformAxis = ui::TransformAxis;

    sparks::render::Renderer renderer;
    sparks::render::ViewControls viewControls;
    bool panModeEnabled = false;
    TransformMode transformMode = TransformMode::Move;
    TransformAxis activeTransformAxis = TransformAxis::None;
    bool transformDragging = false;
    ImVec2 transformDragStartMouse(0.0f, 0.0f);
    ImVec2 transformDragPrevMouse(0.0f, 0.0f);
    float transformRotateAccumDeg = 0.0f;
    glm::vec2 transformDragStartWorldRotation(0.0f, 0.0f);
    glm::vec3 transformDragStartSelectedCenter(0.0f);
    bool selectionDragging = false;
    ImVec2 selectionStart(0.0f, 0.0f);
    ImVec2 selectionEnd(0.0f, 0.0f);
    std::string importStatus;
    std::optional<sparks::render::ImportedModelData> importedModel;
    std::optional<sparks::render::ImportedModelData> transformDragStartImportedModel;
    std::vector<sparks::render::CloudObjectSettings> transformDragStartClouds;
    bool importedModelSelected = false;
    std::vector<RigBone> rigBones;
    std::vector<RigBone> rigImportedSourceBones;
    std::vector<VertexGroupInfo> rigVertexGroups;
    int selectedVertexGroup = -1;
    int selectedRigBone = 0;
    int rigAvatarDefinition = 0;
    int rigAnimationType = 0;
    bool rigOptimizeGameObjects = false;
    bool rigHasUnsavedChanges = false;
    int rigRootBone = 0;
    std::array<int, 15> humanoidBoneMap{};
    humanoidBoneMap.fill(-1);
    WeatherSystem weatherSystem;
    sparks::render::EnvironmentSettings environmentSettings;
    int weatherPresetIndex = 1;
    int environmentPresetIndex = 0;
    std::vector<int> selectedCloudIndices;
    int scatterCloudCount = 8;
    int scatterSeed = 1337;
    double previousFrameTime = glfwGetTime();

    auto applyWeatherPreset = [&](const int presetIndex) {
        WeatherSettings preset = weatherSystem.settings();
        if (presetIndex == 0) {
            // Drizzle preset
            preset.enabled = true;
            preset.rainIntensity = 0.22f;
            preset.rainSpeed = 6.5f;
            preset.rainAreaRadius = 16.0f;
            preset.maxDrops = 1200;
            preset.rainLengthMin = 0.25f;
            preset.rainLengthMax = 0.85f;
            preset.windX = 0.45f;
            preset.windZ = 0.18f;
            preset.turbulence = 0.35f;
            preset.splashAmount = 0.55f;
            preset.splashForce = 0.75f;
            preset.dropletAmount = 0.60f;
            preset.mistDrift = 0.30f;
            preset.windSwayStrength = 0.40f;
            preset.windSwayFrequency = 1.00f;
            preset.randomDropletBursts = 0.35f;
            preset.rippleAmount = 0.45f;
            preset.rippleSize = 10.0f;
            preset.rainLineWidth = 0.9f;
            preset.splashPointSize = 4.8f;
            preset.dropletPointSize = 3.0f;
            preset.rainOpacityScale = 0.70f;
            preset.splashOpacityScale = 0.75f;
            preset.dropletOpacityScale = 0.70f;
            preset.rippleOpacityScale = 0.65f;
        } else if (presetIndex == 1) {
            // Regular rain preset
            preset.enabled = true;
            preset.rainIntensity = 0.55f;
            preset.rainSpeed = 9.2f;
            preset.rainAreaRadius = 20.0f;
            preset.maxDrops = 2600;
            preset.rainLengthMin = 0.45f;
            preset.rainLengthMax = 1.50f;
            preset.windX = 1.10f;
            preset.windZ = 0.42f;
            preset.turbulence = 0.85f;
            preset.splashAmount = 1.0f;
            preset.splashForce = 1.0f;
            preset.dropletAmount = 1.0f;
            preset.mistDrift = 0.55f;
            preset.windSwayStrength = 0.75f;
            preset.windSwayFrequency = 1.35f;
            preset.randomDropletBursts = 1.0f;
            preset.rippleAmount = 1.0f;
            preset.rippleSize = 16.0f;
            preset.rainLineWidth = 1.15f;
            preset.splashPointSize = 6.8f;
            preset.dropletPointSize = 3.8f;
            preset.rainOpacityScale = 1.0f;
            preset.splashOpacityScale = 1.0f;
            preset.dropletOpacityScale = 1.0f;
            preset.rippleOpacityScale = 1.0f;
        } else {
            // Storm preset
            preset.enabled = true;
            preset.rainIntensity = 0.92f;
            preset.rainSpeed = 14.5f;
            preset.rainAreaRadius = 30.0f;
            preset.maxDrops = 5600;
            preset.rainLengthMin = 0.80f;
            preset.rainLengthMax = 2.40f;
            preset.windX = 3.60f;
            preset.windZ = 1.60f;
            preset.turbulence = 2.10f;
            preset.splashAmount = 2.30f;
            preset.splashForce = 1.65f;
            preset.dropletAmount = 2.00f;
            preset.mistDrift = 1.35f;
            preset.windSwayStrength = 1.80f;
            preset.windSwayFrequency = 2.40f;
            preset.randomDropletBursts = 2.20f;
            preset.rippleAmount = 2.30f;
            preset.rippleSize = 26.0f;
            preset.rainLineWidth = 1.95f;
            preset.splashPointSize = 10.5f;
            preset.dropletPointSize = 5.8f;
            preset.rainOpacityScale = 1.35f;
            preset.splashOpacityScale = 1.45f;
            preset.dropletOpacityScale = 1.20f;
            preset.rippleOpacityScale = 1.25f;
        }
        weatherSystem.setSettings(preset);
        weatherPresetIndex = presetIndex;
    };

    auto applyEnvironmentPreset = [&](const int presetIndex) {
        sparks::render::EnvironmentSettings preset = environmentSettings;
        if (presetIndex == 0) {
            // Clear Day
            preset.enableSkydome = true;
            preset.skydomeRadius = 240.0f;
            preset.skydomePitchDegrees = 90.0f;
            preset.skydomeYawDegrees = 0.0f;
            preset.skyHorizonColor = glm::vec3(0.58f, 0.72f, 0.90f);
            preset.skyZenithColor = glm::vec3(0.14f, 0.30f, 0.52f);
            preset.skyCloudColor = glm::vec3(0.92f, 0.95f, 0.98f);
            preset.skyCloudAmount = 0.11f;
            preset.skyCloudScale = 1.25f;

            preset.enableTerrain = true;
            preset.terrainSize = 260.0f;
            preset.terrainHeight = -0.76f;
            preset.terrainColorA = glm::vec3(0.18f, 0.24f, 0.16f);
            preset.terrainColorB = glm::vec3(0.28f, 0.35f, 0.24f);
            preset.terrainPatchScale = 0.12f;
            preset.terrainRoughness = 0.85f;
            preset.terrainLightDirection = glm::vec3(0.45f, 1.0f, 0.32f);

            preset.enableCloudObjects = true;
            preset.cloudObjects.clear();
        } else if (presetIndex == 1) {
            // Overcast
            preset.enableSkydome = true;
            preset.skydomeRadius = 260.0f;
            preset.skydomePitchDegrees = 90.0f;
            preset.skydomeYawDegrees = 0.0f;
            preset.skyHorizonColor = glm::vec3(0.43f, 0.49f, 0.57f);
            preset.skyZenithColor = glm::vec3(0.22f, 0.26f, 0.34f);
            preset.skyCloudColor = glm::vec3(0.72f, 0.76f, 0.82f);
            preset.skyCloudAmount = 0.45f;
            preset.skyCloudScale = 1.55f;

            preset.enableTerrain = true;
            preset.terrainSize = 280.0f;
            preset.terrainHeight = -0.76f;
            preset.terrainColorA = glm::vec3(0.15f, 0.18f, 0.16f);
            preset.terrainColorB = glm::vec3(0.21f, 0.24f, 0.22f);
            preset.terrainPatchScale = 0.16f;
            preset.terrainRoughness = 1.20f;
            preset.terrainLightDirection = glm::vec3(0.30f, 1.0f, 0.20f);

            preset.enableCloudObjects = true;
            preset.cloudObjects.clear();
        } else {
            // Stormy Dusk
            preset.enableSkydome = true;
            preset.skydomeRadius = 300.0f;
            preset.skydomePitchDegrees = 90.0f;
            preset.skydomeYawDegrees = 0.0f;
            preset.skyHorizonColor = glm::vec3(0.42f, 0.33f, 0.30f);
            preset.skyZenithColor = glm::vec3(0.10f, 0.11f, 0.18f);
            preset.skyCloudColor = glm::vec3(0.60f, 0.56f, 0.58f);
            preset.skyCloudAmount = 0.60f;
            preset.skyCloudScale = 1.90f;

            preset.enableTerrain = true;
            preset.terrainSize = 320.0f;
            preset.terrainHeight = -0.77f;
            preset.terrainColorA = glm::vec3(0.12f, 0.12f, 0.11f);
            preset.terrainColorB = glm::vec3(0.22f, 0.20f, 0.18f);
            preset.terrainPatchScale = 0.22f;
            preset.terrainRoughness = 1.50f;
            preset.terrainLightDirection = glm::vec3(0.22f, 1.0f, 0.14f);

            preset.enableCloudObjects = true;
            preset.cloudObjects.clear();
        }

        environmentSettings = preset;
        renderer.setEnvironmentSettings(environmentSettings);
        environmentPresetIndex = presetIndex;
        selectedCloudIndices.clear();
    };

    renderer.initialize();
    renderer.setEnvironmentSettings(environmentSettings);

    while (!glfwWindowShouldClose(m_window)) {
        const double currentTime = glfwGetTime();
        const float deltaSeconds = static_cast<float>(std::clamp(currentTime - previousFrameTime, 0.0, 0.1));
        previousFrameTime = currentTime;

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_P, false) && io.KeyCtrl) {
            panModeEnabled = !panModeEnabled;
        }
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                transformMode = TransformMode::Move;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                transformMode = TransformMode::Rotate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                transformMode = TransformMode::Scale;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                bool centered = false;
                if (importedModelSelected && importedModel.has_value()) {
                    viewControls.panTargetOffset.x = importedModel->position.x;
                    viewControls.panTargetOffset.y = importedModel->position.y;
                    viewControls.orbitTargetZTarget = importedModel->position.z;
                    centered = true;
                } else if (!selectedCloudIndices.empty() && !environmentSettings.cloudObjects.empty()) {
                    glm::vec3 center(0.0f);
                    int validCount = 0;
                    for (const int idx : selectedCloudIndices) {
                        if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                            center += environmentSettings.cloudObjects[static_cast<std::size_t>(idx)].position;
                            ++validCount;
                        }
                    }
                    if (validCount > 0) {
                        center /= static_cast<float>(validCount);
                        viewControls.panTargetOffset.x = center.x;
                        viewControls.panTargetOffset.y = center.y;
                        viewControls.orbitTargetZTarget = center.z;
                        centered = true;
                    }
                }
                if (centered) {
                    importStatus = "Camera focusing on selection (Z).";
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_1, false)) {
                applyWeatherPreset(0);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_2, false)) {
                applyWeatherPreset(1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_3, false)) {
                applyWeatherPreset(2);
            }
        }

        static constexpr ImGuiWindowFlags kWorkspaceFlags =
            ImGuiWindowFlags_MenuBar |
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings;

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

        ImGui::Begin("Workspace", nullptr, kWorkspaceFlags);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::BeginMenu("Import")) {
                    if (ImGui::MenuItem("FBX...")) {
                        importFbxFromDialog(
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
                            selectedVertexGroup,
                            importStatus);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Camera")) {
                if (ImGui::SliderFloat("Zoom", &viewControls.zoomTargetDistance, kMinCameraZoom, kMaxCameraZoom, "%.2f")) {
                    // Menu slider acts as an explicit camera command, so keep current zoom in sync.
                    viewControls.zoomDistance = viewControls.zoomTargetDistance;
                }
                if (ImGui::DragFloat2("Pan", &viewControls.panOffset.x, 0.01f, -10.0f, 10.0f, "%.2f")) {
                    viewControls.panTargetOffset = viewControls.panOffset;
                }
                ImGui::DragFloat2("World Rotation", &viewControls.worldRotationDegrees.x, 0.5f, -180.0f, 180.0f, "%.1f deg");
                ImGui::Checkbox("Pan Mode (Ctrl+P)", &panModeEnabled);

                if (ImGui::Button("Reset Camera")) {
                    viewControls = sparks::render::ViewControls{};
                    panModeEnabled = false;
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Transform")) {
                if (ImGui::MenuItem("Move (W)", nullptr, transformMode == TransformMode::Move)) {
                    transformMode = TransformMode::Move;
                }
                if (ImGui::MenuItem("Rotate (E)", nullptr, transformMode == TransformMode::Rotate)) {
                    transformMode = TransformMode::Rotate;
                }
                if (ImGui::MenuItem("Scale (R)", nullptr, transformMode == TransformMode::Scale)) {
                    transformMode = TransformMode::Scale;
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Weather")) {
                WeatherSettings weatherSettings = weatherSystem.settings();
                bool changed = false;

                int selectedPreset = weatherPresetIndex;
                if (ImGui::RadioButton("Drizzle", selectedPreset == 0)) {
                    selectedPreset = 0;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Rain", selectedPreset == 1)) {
                    selectedPreset = 1;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Storm", selectedPreset == 2)) {
                    selectedPreset = 2;
                }
                if (selectedPreset != weatherPresetIndex) {
                    applyWeatherPreset(selectedPreset);
                    weatherSettings = weatherSystem.settings();
                }

                ImGui::Separator();
                changed |= ImGui::Checkbox("Enable Rain", &weatherSettings.enabled);
                changed |= ImGui::SliderFloat("Rain Intensity", &weatherSettings.rainIntensity, 0.0f, 1.0f, "%.2f");
                changed |= ImGui::SliderFloat("Rain Speed", &weatherSettings.rainSpeed, 1.0f, 20.0f, "%.1f");
                changed |= ImGui::SliderFloat("Rain Area", &weatherSettings.rainAreaRadius, 4.0f, 50.0f, "%.1f");
                changed |= ImGui::SliderInt("Rain Drops", &weatherSettings.maxDrops, 64, 6000);
                ImGui::Separator();
                changed |= ImGui::SliderFloat("Wind X", &weatherSettings.windX, -8.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Wind Z", &weatherSettings.windZ, -8.0f, 8.0f, "%.2f");
                changed |= ImGui::SliderFloat("Turbulence", &weatherSettings.turbulence, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Rain Length Min", &weatherSettings.rainLengthMin, 0.1f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Rain Length Max", &weatherSettings.rainLengthMax, 0.1f, 6.0f, "%.2f");
                ImGui::Separator();
                changed |= ImGui::SliderFloat("Splash Amount", &weatherSettings.splashAmount, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Splash Force", &weatherSettings.splashForce, 0.2f, 3.5f, "%.2f");
                changed |= ImGui::SliderFloat("Droplet Amount", &weatherSettings.dropletAmount, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Mist Drift", &weatherSettings.mistDrift, 0.0f, 3.0f, "%.2f");
                changed |= ImGui::SliderFloat("Wind Sway Strength", &weatherSettings.windSwayStrength, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Wind Sway Frequency", &weatherSettings.windSwayFrequency, 0.1f, 6.0f, "%.2f");
                changed |= ImGui::SliderFloat("Random Droplet Bursts", &weatherSettings.randomDropletBursts, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Ripple Amount", &weatherSettings.rippleAmount, 0.0f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Ripple Size", &weatherSettings.rippleSize, 2.0f, 48.0f, "%.1f");
                ImGui::Separator();
                changed |= ImGui::SliderFloat("Rain Line Width", &weatherSettings.rainLineWidth, 0.5f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Splash Size", &weatherSettings.splashPointSize, 1.0f, 20.0f, "%.1f");
                changed |= ImGui::SliderFloat("Droplet Size", &weatherSettings.dropletPointSize, 1.0f, 14.0f, "%.1f");
                changed |= ImGui::SliderFloat("Rain Opacity", &weatherSettings.rainOpacityScale, 0.0f, 2.5f, "%.2f");
                changed |= ImGui::SliderFloat("Splash Opacity", &weatherSettings.splashOpacityScale, 0.0f, 2.5f, "%.2f");
                changed |= ImGui::SliderFloat("Droplet Opacity", &weatherSettings.dropletOpacityScale, 0.0f, 2.5f, "%.2f");
                changed |= ImGui::SliderFloat("Ripple Opacity", &weatherSettings.rippleOpacityScale, 0.0f, 2.5f, "%.2f");
                if (changed) {
                    weatherSystem.setSettings(weatherSettings);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Environment")) {
                bool envChanged = false;

                int selectedEnvironmentPreset = environmentPresetIndex;
                if (ImGui::RadioButton("Clear Day", selectedEnvironmentPreset == 0)) {
                    selectedEnvironmentPreset = 0;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Overcast", selectedEnvironmentPreset == 1)) {
                    selectedEnvironmentPreset = 1;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Stormy Dusk", selectedEnvironmentPreset == 2)) {
                    selectedEnvironmentPreset = 2;
                }
                if (selectedEnvironmentPreset != environmentPresetIndex) {
                    applyEnvironmentPreset(selectedEnvironmentPreset);
                }

                ImGui::Separator();
                envChanged |= ImGui::Checkbox("Enable Skydome", &environmentSettings.enableSkydome);
                envChanged |= ImGui::SliderFloat("Skydome Radius", &environmentSettings.skydomeRadius, 20.0f, 4000.0f, "%.1f");
                envChanged |= ImGui::SliderFloat("Skydome Pitch", &environmentSettings.skydomePitchDegrees, -180.0f, 180.0f, "%.1f deg");
                envChanged |= ImGui::SliderFloat("Skydome Yaw", &environmentSettings.skydomeYawDegrees, -180.0f, 180.0f, "%.1f deg");
                envChanged |= ImGui::ColorEdit3("Sky Horizon", &environmentSettings.skyHorizonColor.x);
                envChanged |= ImGui::ColorEdit3("Sky Zenith", &environmentSettings.skyZenithColor.x);
                envChanged |= ImGui::ColorEdit3("Cloud Color", &environmentSettings.skyCloudColor.x);
                envChanged |= ImGui::SliderFloat("Cloud Amount", &environmentSettings.skyCloudAmount, 0.0f, 1.0f, "%.2f");
                envChanged |= ImGui::SliderFloat("Cloud Scale", &environmentSettings.skyCloudScale, 0.1f, 6.0f, "%.2f");

                ImGui::Separator();
                envChanged |= ImGui::Checkbox("Enable Terrain", &environmentSettings.enableTerrain);
                envChanged |= ImGui::SliderFloat("Terrain Size", &environmentSettings.terrainSize, 20.0f, 4000.0f, "%.1f");
                envChanged |= ImGui::SliderFloat("Terrain Height", &environmentSettings.terrainHeight, -20.0f, 20.0f, "%.2f");
                envChanged |= ImGui::ColorEdit3("Terrain Color A", &environmentSettings.terrainColorA.x);
                envChanged |= ImGui::ColorEdit3("Terrain Color B", &environmentSettings.terrainColorB.x);
                envChanged |= ImGui::SliderFloat("Terrain Patch Scale", &environmentSettings.terrainPatchScale, 0.01f, 4.0f, "%.2f");
                envChanged |= ImGui::SliderFloat("Terrain Roughness", &environmentSettings.terrainRoughness, 0.0f, 3.0f, "%.2f");
                envChanged |= ImGui::DragFloat3("Terrain Light Dir", &environmentSettings.terrainLightDirection.x, 0.02f, -5.0f, 5.0f, "%.2f");

                ImGui::Separator();
                if (ImGui::BeginMenu("Cloud")) {
                    auto scatterClouds = [&](const bool clearExisting) {
                        if (clearExisting) {
                            environmentSettings.cloudObjects.clear();
                        }

                        const int clampedSeed = std::max(scatterSeed, 0);
                        std::mt19937 rng(static_cast<std::mt19937::result_type>(clampedSeed));
                        std::uniform_real_distribution<float> xzDist(-40.0f, 40.0f);
                        std::uniform_real_distribution<float> yDist(8.0f, 28.0f);
                        std::uniform_real_distribution<float> yawDist(-180.0f, 180.0f);
                        std::uniform_real_distribution<float> sxDist(2.0f, 7.0f);
                        std::uniform_real_distribution<float> syDist(0.9f, 2.6f);
                        std::uniform_real_distribution<float> szDist(1.6f, 5.5f);
                        std::uniform_real_distribution<float> cVarDist(-0.08f, 0.04f);
                        std::uniform_real_distribution<float> opDist(0.55f, 0.90f);
                        std::uniform_real_distribution<float> softDist(0.70f, 0.95f);
                        std::uniform_real_distribution<float> detailDist(0.7f, 1.5f);
                        std::uniform_int_distribution<int> typeDist(0, 3);
                        std::uniform_int_distribution<unsigned int> seedDist(1u, 0xffffffffu);

                        for (int i = 0; i < scatterCloudCount; ++i) {
                            sparks::render::CloudObjectSettings cloud;
                            cloud.position = glm::vec3(xzDist(rng), yDist(rng), xzDist(rng));
                            cloud.rotationEulerDegrees = glm::vec3(0.0f, yawDist(rng), 0.0f);
                            cloud.scale = glm::vec3(sxDist(rng), syDist(rng), szDist(rng));
                            const float tint = cVarDist(rng);
                            cloud.color = glm::clamp(glm::vec3(0.95f + tint, 0.97f + tint, 1.0f + tint), glm::vec3(0.75f), glm::vec3(1.0f));
                            cloud.opacity = opDist(rng);
                            cloud.softness = softDist(rng);
                            cloud.detail = detailDist(rng);
                            cloud.planeFade = 0.90f;
                            cloud.planeCount = 1;
                            cloud.cubeSpread = 1.0f;
                            cloud.motionSpeed = 1.0f;
                            cloud.cloudType = typeDist(rng);
                            cloud.planeSelectionSeed = seedDist(rng);
                            environmentSettings.cloudObjects.push_back(cloud);
                        }

                        selectedCloudIndices.clear();
                        if (!environmentSettings.cloudObjects.empty()) {
                            selectedCloudIndices.push_back(static_cast<int>(environmentSettings.cloudObjects.size()) - 1);
                        }
                        environmentSettings.enableCloudObjects = true;
                        envChanged = true;
                    };

                    if (ImGui::MenuItem("Add Cloud At Origin")) {
                        sparks::render::CloudObjectSettings cloud;
                        cloud.position = glm::vec3(viewControls.panOffset.x, viewControls.panOffset.y + 1.6f, -2.5f);
                        cloud.rotationEulerDegrees = glm::vec3(0.0f, 0.0f, 0.0f);
                        cloud.scale = glm::vec3(5.8f, 2.8f, 4.4f);
                        cloud.color = glm::vec3(0.88f, 0.90f, 0.94f);
                        cloud.opacity = 0.95f;
                        cloud.softness = 0.62f;
                        cloud.detail = 1.0f;
                        cloud.planeFade = 0.90f;
                        cloud.planeCount = 1;
                        cloud.cubeSpread = 1.0f;
                        cloud.motionSpeed = 1.0f;
                        cloud.cloudType = 0;
                        {
                            static std::mt19937 seedRng(std::random_device{}());
                            static std::uniform_int_distribution<unsigned int> seedDist(1u, 0xffffffffu);
                            cloud.planeSelectionSeed = seedDist(seedRng);
                        }
                        environmentSettings.cloudObjects.push_back(cloud);
                        environmentSettings.enableCloudObjects = true;
                        selectedCloudIndices = {static_cast<int>(environmentSettings.cloudObjects.size()) - 1};
                        envChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::MenuItem("Duplicate Selected", nullptr, false, !selectedCloudIndices.empty())) {
                        std::vector<sparks::render::CloudObjectSettings> copies;
                        for (const int idx : selectedCloudIndices) {
                            if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                                auto dup = environmentSettings.cloudObjects[idx];
                                dup.position.x += 2.0f;
                                dup.position.z += 2.0f;
                                copies.push_back(dup);
                            }
                        }
                        selectedCloudIndices.clear();
                        for (auto& c : copies) {
                            environmentSettings.cloudObjects.push_back(c);
                            selectedCloudIndices.push_back(static_cast<int>(environmentSettings.cloudObjects.size()) - 1);
                        }
                        environmentSettings.enableCloudObjects = true;
                        envChanged = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::MenuItem("Remove Selected", nullptr, false, !selectedCloudIndices.empty())) {
                        auto sortedIndices = selectedCloudIndices;
                        std::sort(sortedIndices.rbegin(), sortedIndices.rend());
                        for (const int idx : sortedIndices) {
                            if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                                environmentSettings.cloudObjects.erase(environmentSettings.cloudObjects.begin() + idx);
                            }
                        }
                        selectedCloudIndices.clear();
                        envChanged = true;
                    }

                    ImGui::SliderInt("Scatter Count", &scatterCloudCount, 1, 64);
                    ImGui::InputInt("Scatter Seed", &scatterSeed);
                    ImGui::SameLine();
                    if (ImGui::Button("Randomize Seed")) {
                        static std::random_device randomDevice;
                        scatterSeed = static_cast<int>(randomDevice() & 0x7fffffffU);
                    }
                    scatterSeed = std::max(scatterSeed, 0);
                    if (ImGui::MenuItem("Scatter Clouds Around Origin")) {
                        scatterClouds(false);
                    }
                    ImGui::SameLine();
                    if (ImGui::MenuItem("Regenerate Scatter")) {
                        scatterClouds(true);
                    }

                    if (ImGui::MenuItem("Clear All Clouds", nullptr, false, !environmentSettings.cloudObjects.empty())) {
                        environmentSettings.cloudObjects.clear();
                        selectedCloudIndices.clear();
                        envChanged = true;
                    }

                    ImGui::Separator();
                    envChanged |= ImGui::Checkbox("Enable Cloud Objects", &environmentSettings.enableCloudObjects);
                    ImGui::EndMenu();
                }

                if (envChanged) {
                    renderer.setEnvironmentSettings(environmentSettings);
                    environmentPresetIndex = -1;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        const glm::vec3 weatherCenter(viewControls.panOffset.x, viewControls.panOffset.y, 0.0f);
        weatherSystem.update(deltaSeconds, weatherCenter);
        renderer.setWeatherRain(
            weatherSystem.rainLineVertices(),
            weatherSystem.splashPoints(),
            weatherSystem.dropletPoints(),
            weatherSystem.ripplePoints(),
            weatherSystem.settings().rainIntensity,
            weatherSystem.settings().enabled,
            weatherSystem.settings().rainLineWidth,
            weatherSystem.settings().splashPointSize,
            weatherSystem.settings().dropletPointSize,
            weatherSystem.settings().rippleSize,
            weatherSystem.settings().rainOpacityScale,
            weatherSystem.settings().splashOpacityScale,
            weatherSystem.settings().dropletOpacityScale,
            weatherSystem.settings().rippleOpacityScale);

        const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
        const float statusBarHeight = 30.0f;
        const float contentHeight = (workspaceSize.y > statusBarHeight + 6.0f)
            ? (workspaceSize.y - statusBarHeight - 6.0f)
            : workspaceSize.y;
        static float leftPaneWidth = -1.0f;
        if (leftPaneWidth < 0.0f) {
            leftPaneWidth = workspaceSize.x * 0.72f;
        }
        leftPaneWidth = std::clamp(leftPaneWidth, 200.0f, workspaceSize.x - 220.0f);

        ImGui::BeginChild("ViewportPane", ImVec2(leftPaneWidth, contentHeight), true);
        {
            if (ImGui::BeginTabBar("SceneTabs")) {
                ui::drawSceneTab(
                    renderer,
                    io,
                    viewControls,
                    environmentSettings,
                    selectedCloudIndices,
                    panModeEnabled,
                    transformMode,
                    activeTransformAxis,
                    transformDragging,
                    transformDragStartMouse,
                    transformDragPrevMouse,
                    transformRotateAccumDeg,
                    transformDragStartWorldRotation,
                    transformDragStartSelectedCenter,
                    selectionDragging,
                    selectionStart,
                    selectionEnd,
                    importedModel,
                    transformDragStartImportedModel,
                    transformDragStartClouds,
                    importedModelSelected);

                ui::drawRiggingTab(
                    rigBones,
                    rigImportedSourceBones,
                    rigVertexGroups,
                    selectedVertexGroup,
                    selectedRigBone,
                    rigAvatarDefinition,
                    rigAnimationType,
                    rigOptimizeGameObjects,
                    rigHasUnsavedChanges,
                    rigRootBone,
                    humanoidBoneMap,
                    importedModelSelected,
                    importedModel);

                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.47f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.36f, 0.47f, 0.70f, 1.0f));
        ImGui::Button("##PanelSplitter", ImVec2(4.0f, contentHeight));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (ImGui::IsItemActive()) {
            leftPaneWidth = std::clamp(leftPaneWidth + io.MouseDelta.x, 200.0f, workspaceSize.x - 220.0f);
        }

        ImGui::SameLine();

        ui::drawRightPane(contentHeight, io, renderer, environmentSettings, selectedCloudIndices, importedModel, importedModelSelected);

        ui::drawStatusBar(transformMode, panModeEnabled, importStatus, statusBarHeight);

        ImGui::End();

        ImGui::PopStyleVar(2);

        ImGui::Render();

        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(m_window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.05f, 0.06f, 0.07f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(m_window);
    }

    return 0;
}

void Application::initializeWindow() {
    if (glfwInit() == GLFW_FALSE) {
        throw std::runtime_error("Failed to initialize GLFW.");
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    m_window = glfwCreateWindow(1600, 900, "SparksEngine - FBX Editor", nullptr, nullptr);
    if (m_window == nullptr) {
        throw std::runtime_error("Failed to create GLFW window.");
    }

    glfwMakeContextCurrent(m_window);
    glfwMaximizeWindow(m_window);
    glfwSwapInterval(1);

    const int loaded = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
    if (loaded == 0) {
        throw std::runtime_error("Failed to initialize GLAD.");
    }
}

void Application::initializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplGlfw_InitForOpenGL(m_window, true)) {
        throw std::runtime_error("Failed to initialize ImGui GLFW backend.");
    }

    if (!ImGui_ImplOpenGL3_Init("#version 460")) {
        throw std::runtime_error("Failed to initialize ImGui OpenGL backend.");
    }
}

void Application::shutdownImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

}  // namespace sparks::core