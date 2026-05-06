#include "sparks/core/Application.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <glm/ext/matrix_transform.hpp>
#include <glm/trigonometric.hpp>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "sparks/core/ApplicationImport.hpp"
#include "sparks/core/ApplicationUi.hpp"
#include "sparks/core/WeatherSystem.hpp"
#include "sparks/render/Renderer.hpp"

namespace
{

    constexpr float kMinCameraZoom = 1.5f;
    constexpr float kMaxCameraZoom = 250.0f;

    using sparks::core::import_util::importFbxFromDialog;
    using sparks::core::rigging::RigBone;
    using sparks::core::rigging::VertexGroupInfo;

    glm::vec3 transformPoint(const glm::mat4 &matrix, const glm::vec3 &point)
    {
        const glm::vec4 transformed = matrix * glm::vec4(point, 1.0f);
        return glm::vec3(transformed.x, transformed.y, transformed.z);
    }

    void computeImportedModelBounds(
        const sparks::render::ImportedModelData &model,
        glm::vec3 &outMin,
        glm::vec3 &outMax)
    {
        const glm::vec3 halfExtent = glm::abs(model.dimensions * model.scale) * 0.5f;

        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, model.position);
        const glm::vec3 rotRad = glm::radians(model.rotationEulerDegrees);
        transform = glm::rotate(transform, rotRad.x, glm::vec3(1.0f, 0.0f, 0.0f));
        transform = glm::rotate(transform, rotRad.y, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, rotRad.z, glm::vec3(0.0f, 0.0f, 1.0f));

        outMin = glm::vec3(std::numeric_limits<float>::max());
        outMax = glm::vec3(std::numeric_limits<float>::lowest());

        for (int sx = -1; sx <= 1; sx += 2)
        {
            for (int sy = -1; sy <= 1; sy += 2)
            {
                for (int sz = -1; sz <= 1; sz += 2)
                {
                    const glm::vec3 corner(
                        halfExtent.x * static_cast<float>(sx),
                        halfExtent.y * static_cast<float>(sy),
                        halfExtent.z * static_cast<float>(sz));
                    const glm::vec3 worldCorner = transformPoint(transform, corner);
                    outMin = glm::min(outMin, worldCorner);
                    outMax = glm::max(outMax, worldCorner);
                }
            }
        }
    }

} // namespace

namespace sparks::core
{

    Application::Application()
    {
        initializeWindow();
        initializeImGui();
        // Set default skydome cloud settings
        m_environmentSettings.skyCloudAmount = 0.77f;
        m_environmentSettings.skyCloudScale = 4.84f;
        m_environmentSettings.skyCloudSpeed = 0.04f;
        m_environmentSettings.skyCloudShadowStrength = 0.00f;

        // Set default terrain settings
        m_environmentSettings.terrainSize = 1097.9f;
        m_environmentSettings.terrainHeight = -5.0f;
        m_environmentSettings.terrainColorA = glm::vec3(41.0f/255.0f, 51.0f/255.0f, 36.0f/255.0f);
        m_environmentSettings.terrainColorB = glm::vec3(61.0f/255.0f, 71.0f/255.0f, 51.0f/255.0f);
        m_environmentSettings.terrainPatchScale = 1.36f;
        m_environmentSettings.terrainRoughness = 1.30f;
        m_environmentSettings.fbxShadowSoftness = 1.00f;
    }

    Application::~Application()
    {
        shutdownImGui();

        if (m_window != nullptr)
        {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }

        glfwTerminate();
    }

    int Application::run()
    {
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
        sparks::render::EnvironmentSettings environmentSettings = m_environmentSettings;
        int weatherPresetIndex = 1;
        glm::vec3 fixedRainCenter(0.0f, 0.0f, 0.0f);
        std::vector<int> selectedCloudIndices;
        double previousFrameTime = glfwGetTime();

        auto applyWeatherPreset = [&](const int presetIndex)
        {
            constexpr float kTinyPresetRainLineWidth = 0.55f;
            WeatherSettings preset = weatherSystem.settings();
            preset.useCustomVisualProfile = false;
            preset.rainTint = glm::vec3(1.0f, 1.0f, 1.0f);
            preset.splashTint = glm::vec3(1.0f, 1.0f, 1.0f);
            preset.dropletTint = glm::vec3(1.0f, 1.0f, 1.0f);
            preset.rippleTint = glm::vec3(1.0f, 1.0f, 1.0f);
            preset.rainStyleBoost = 1.0f;
            preset.particleStyleBoost = 1.0f;
            if (presetIndex == 0)
            {
                // Vertical drizzle curtain
                preset.enabled = true;
                preset.rainConcept = 0;
                preset.rainIntensity = 0.24f;
                preset.rainSpeed = 6.0f;
                preset.rainAreaRadius = 18.0f;
                preset.maxDrops = 1500;
                preset.rainLengthMin = 0.20f;
                preset.rainLengthMax = 0.72f;
                preset.windX = 0.12f;
                preset.windZ = 0.05f;
                preset.turbulence = 0.20f;
                preset.splashAmount = 0.55f;
                preset.splashForce = 0.68f;
                preset.dropletAmount = 0.60f;
                preset.mistDrift = 0.20f;
                preset.windSwayStrength = 0.18f;
                preset.windSwayFrequency = 0.80f;
                preset.randomDropletBursts = 0.35f;
                preset.rippleAmount = 0.45f;
                preset.rippleSize = 11.0f;
                preset.rainLineWidth = 0.95f;
                preset.splashPointSize = 4.8f;
                preset.dropletPointSize = 2.9f;
                preset.rainOpacityScale = 0.82f;
                preset.splashOpacityScale = 0.75f;
                preset.dropletOpacityScale = 0.70f;
                preset.rippleOpacityScale = 0.65f;
            }
            else if (presetIndex == 1)
            {
                // Layered depth rain (balanced baseline)
                preset.enabled = true;
                preset.rainConcept = 1;
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
            }
            else if (presetIndex == 2)
            {
                // Directional storm sheets
                preset.enabled = true;
                preset.rainConcept = 2;
                preset.rainIntensity = 0.96f;
                preset.rainSpeed = 16.0f;
                preset.rainAreaRadius = 32.0f;
                preset.maxDrops = 5800;
                preset.rainLengthMin = 0.95f;
                preset.rainLengthMax = 2.80f;
                preset.windX = 5.40f;
                preset.windZ = 2.80f;
                preset.turbulence = 2.20f;
                preset.splashAmount = 2.35f;
                preset.splashForce = 1.75f;
                preset.dropletAmount = 2.10f;
                preset.mistDrift = 1.40f;
                preset.windSwayStrength = 2.10f;
                preset.windSwayFrequency = 2.65f;
                preset.randomDropletBursts = 2.40f;
                preset.rippleAmount = 2.40f;
                preset.rippleSize = 28.0f;
                preset.rainLineWidth = 2.05f;
                preset.splashPointSize = 11.0f;
                preset.dropletPointSize = 6.0f;
                preset.rainOpacityScale = 1.45f;
                preset.splashOpacityScale = 1.50f;
                preset.dropletOpacityScale = 1.25f;
                preset.rippleOpacityScale = 1.30f;
            }
            else if (presetIndex == 3)
            {
                // Wind-gust bursts
                preset.enabled = true;
                preset.rainConcept = 3;
                preset.rainIntensity = 0.78f;
                preset.rainSpeed = 12.8f;
                preset.rainAreaRadius = 26.0f;
                preset.maxDrops = 4400;
                preset.rainLengthMin = 0.75f;
                preset.rainLengthMax = 2.15f;
                preset.windX = 4.20f;
                preset.windZ = 1.90f;
                preset.turbulence = 3.20f;
                preset.splashAmount = 1.35f;
                preset.splashForce = 1.30f;
                preset.dropletAmount = 1.70f;
                preset.mistDrift = 1.10f;
                preset.windSwayStrength = 2.70f;
                preset.windSwayFrequency = 3.60f;
                preset.randomDropletBursts = 2.85f;
                preset.rippleAmount = 1.35f;
                preset.rippleSize = 20.0f;
                preset.rainLineWidth = 1.55f;
                preset.splashPointSize = 8.6f;
                preset.dropletPointSize = 5.2f;
                preset.rainOpacityScale = 1.22f;
                preset.splashOpacityScale = 1.15f;
                preset.dropletOpacityScale = 1.10f;
                preset.rippleOpacityScale = 1.10f;
            }
            else if (presetIndex == 4)
            {
                // Tropical monsoon
                preset.enabled = true;
                preset.rainConcept = 4;
                preset.rainIntensity = 1.0f;
                preset.rainSpeed = 15.0f;
                preset.rainAreaRadius = 34.0f;
                preset.maxDrops = 6000;
                preset.rainLengthMin = 1.00f;
                preset.rainLengthMax = 3.20f;
                preset.windX = 2.00f;
                preset.windZ = 1.10f;
                preset.turbulence = 1.55f;
                preset.splashAmount = 2.80f;
                preset.splashForce = 2.10f;
                preset.dropletAmount = 2.20f;
                preset.mistDrift = 1.60f;
                preset.windSwayStrength = 1.90f;
                preset.windSwayFrequency = 2.10f;
                preset.randomDropletBursts = 2.30f;
                preset.rippleAmount = 2.75f;
                preset.rippleSize = 31.0f;
                preset.rainLineWidth = 2.20f;
                preset.splashPointSize = 12.0f;
                preset.dropletPointSize = 6.4f;
                preset.rainOpacityScale = 1.52f;
                preset.splashOpacityScale = 1.62f;
                preset.dropletOpacityScale = 1.35f;
                preset.rippleOpacityScale = 1.40f;
            }
            else
            {
                // Neon city rain
                preset.enabled = true;
                preset.rainConcept = 5;
                preset.rainIntensity = 0.62f;
                preset.rainSpeed = 11.0f;
                preset.rainAreaRadius = 22.0f;
                preset.maxDrops = 3200;
                preset.rainLengthMin = 0.55f;
                preset.rainLengthMax = 1.95f;
                preset.windX = 1.90f;
                preset.windZ = 0.80f;
                preset.turbulence = 1.25f;
                preset.splashAmount = 1.15f;
                preset.splashForce = 1.20f;
                preset.dropletAmount = 1.40f;
                preset.mistDrift = 1.05f;
                preset.windSwayStrength = 1.10f;
                preset.windSwayFrequency = 1.70f;
                preset.randomDropletBursts = 1.40f;
                preset.rippleAmount = 1.20f;
                preset.rippleSize = 18.0f;
                preset.rainLineWidth = 1.35f;
                preset.splashPointSize = 7.6f;
                preset.dropletPointSize = 4.6f;
                preset.rainOpacityScale = 1.55f;
                preset.splashOpacityScale = 1.30f;
                preset.dropletOpacityScale = 1.25f;
                preset.rippleOpacityScale = 1.05f;
            }
            // Keep rain preset visuals consistently thin.
            preset.rainLineWidth = kTinyPresetRainLineWidth;
            weatherSystem.setSettings(preset);
            weatherPresetIndex = std::clamp(presetIndex, 0, 5);
        };

        renderer.initialize();
        renderer.setEnvironmentSettings(environmentSettings);
        applyWeatherPreset(weatherPresetIndex);

        // Animation playback state
        bool animPlaying = true;
        float animTime = 0.0f;
        float animDuration = 1.0f;
        int selectedAnimIndex = 0;

        // --- Frame timing/profiling state ---
        static double lastProfileTime = 0.0;
        static double maxFrameTime = 0.0;
        static double minFrameTime = 1000.0;
        static double avgFrameTime = 0.0;
        static int profileFrameCount = 0;

        // Section timing counters
        static double sectionAnimTime = 0.0, sectionWeatherTime = 0.0, sectionImGuiTime = 0.0;
        static double sectionRenderTime = 0.0, sectionGLTime = 0.0;
        static int sectionFrameCount = 0;

        while (!glfwWindowShouldClose(m_window))
        {
            const double frameStartTime = glfwGetTime();
            const double currentTime = frameStartTime;
            const float deltaSeconds = static_cast<float>(std::clamp(currentTime - previousFrameTime, 0.0, 0.1));
            previousFrameTime = currentTime;

            // --- Animation playback and bone transform update (runtime) ---
            double sectionStart = glfwGetTime();
            if (importedModel.has_value() && !importedModel->animations.empty())
            {
                // Use first animation for now (could be made selectable)
                selectedAnimIndex = std::clamp(selectedAnimIndex, 0, static_cast<int>(importedModel->animations.size()) - 1);
                auto &anim = importedModel->animations[selectedAnimIndex];
                animDuration = anim.duration / std::max(anim.ticksPerSecond, 0.001f);
                if (animPlaying)
                {
                    animTime += deltaSeconds;
                    if (animTime > animDuration)
                        animTime = 0.0f;
                }
                // Build a map from bone name to channel for fast lookup
                std::map<std::string, const sparks::render::ImportedModelData::AnimationChannel *> channelMap;
                for (const auto &ch : anim.channels)
                {
                    channelMap[ch.boneName] = &ch;
                }
                // Resize boneTransforms if needed
                if (importedModel->boneTransforms.size() != importedModel->boneParentIndices.size())
                    importedModel->boneTransforms.resize(importedModel->boneParentIndices.size(), glm::mat4(1.0f));
                // For each bone, compute interpolated transform
                for (size_t i = 0; i < importedModel->boneParentIndices.size(); ++i)
                {
                    std::string boneName;
                    if (i < importedModel->boneNames.size())
                        boneName = importedModel->boneNames[i];
                    glm::vec3 pos(0.0f);
                    glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
                    glm::vec3 scale(1.0f);
                    // Find channel for this bone
                    auto it = channelMap.find(boneName);
                    if (it != channelMap.end())
                    {
                        const auto &keyframes = it->second->keyframes;
                        // Find two keyframes to interpolate
                        if (!keyframes.empty())
                        {
                            const float t = animTime * anim.ticksPerSecond;
                            size_t k0 = 0, k1 = 0;
                            for (size_t k = 1; k < keyframes.size(); ++k)
                            {
                                if (keyframes[k].time > t)
                                {
                                    k1 = k;
                                    k0 = k - 1;
                                    break;
                                }
                            }
                            if (k1 == 0)
                            {
                                k0 = 0;
                                k1 = 0;
                            }
                            float t0 = keyframes[k0].time;
                            float t1 = keyframes[k1].time;
                            float alpha = (t1 > t0) ? (t - t0) / (t1 - t0) : 0.0f;
                            pos = glm::mix(keyframes[k0].position, keyframes[k1].position, alpha);
                            rot = glm::slerp(keyframes[k0].rotation, keyframes[k1].rotation, alpha);
                            scale = glm::mix(keyframes[k0].scale, keyframes[k1].scale, alpha);
                        }
                    }
                    glm::mat4 local = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot) * glm::scale(glm::mat4(1.0f), scale);
                    int parent = importedModel->boneParentIndices[i];
                    if (parent >= 0 && parent < static_cast<int>(i))
                        importedModel->boneTransforms[i] = importedModel->boneTransforms[parent] * local;
                    else
                        importedModel->boneTransforms[i] = local;
                }
            }
            sectionAnimTime += glfwGetTime() - sectionStart;

            glfwPollEvents();

                sectionStart = glfwGetTime();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // --- Display FPS in top-left corner ---
            static float fps = 0.0f;
            static float frameTimeAccum = 0.0f;
            static int frameCount = 0;
            frameTimeAccum += deltaSeconds;
            frameCount++;
            if (frameTimeAccum > 0.25f) {
                fps = frameCount / frameTimeAccum;
                frameTimeAccum = 0.0f;
                frameCount = 0;
                // Update global FPS for status bar
                sparks::core::ui::gStatusBarFps = fps;
            }
            ImGui::SetNextWindowPos(ImVec2(8, 8), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.35f);
            ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("FPS: %.1f", fps);
            ImGui::End();

            ImGuiIO &io = ImGui::GetIO();
            if (ImGui::IsKeyPressed(ImGuiKey_P, false) && io.KeyCtrl)
            {
                panModeEnabled = !panModeEnabled;
            }
            if (!io.WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
                {
                    bool hasSelection = false;
                    glm::vec3 boundMin(std::numeric_limits<float>::max());
                    glm::vec3 boundMax(std::numeric_limits<float>::lowest());

                    if (importedModelSelected && importedModel.has_value())
                    {
                        glm::vec3 modelMin(0.0f);
                        glm::vec3 modelMax(0.0f);
                        computeImportedModelBounds(*importedModel, modelMin, modelMax);
                        boundMin = glm::min(boundMin, modelMin);
                        boundMax = glm::max(boundMax, modelMax);
                        hasSelection = true;
                    }

                    for (const int idx : selectedCloudIndices)
                    {
                        if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size()))
                        {
                            const auto &cloud = environmentSettings.cloudObjects[static_cast<std::size_t>(idx)];
                            const glm::vec3 halfExtent = glm::abs(cloud.scale) * 0.5f;
                            boundMin = glm::min(boundMin, cloud.position - halfExtent);
                            boundMax = glm::max(boundMax, cloud.position + halfExtent);
                            hasSelection = true;
                        }
                    }

                    if (hasSelection)
                    {
                        const glm::vec3 center = (boundMin + boundMax) * 0.5f;
                        const glm::vec3 halfExtent = glm::max((boundMax - boundMin) * 0.5f, glm::vec3(0.001f));
                        const bool alreadyFocused =
                            std::abs(viewControls.panTargetOffset.x - center.x) < 0.001f &&
                            std::abs(viewControls.panTargetOffset.y - center.y) < 0.001f &&
                            std::abs(viewControls.orbitTargetZTarget - center.z) < 0.001f;

                        viewControls.panTargetOffset.x = center.x;
                        viewControls.panTargetOffset.y = center.y;
                        viewControls.orbitTargetZTarget = center.z;

                        if (!alreadyFocused)
                        {
                            importStatus = "Camera focusing on selection (Z).";
                        }
                        else
                        {
                            int fbWidth = 1;
                            int fbHeight = 1;
                            glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
                            const float aspect = static_cast<float>(std::max(fbWidth, 1)) / static_cast<float>(std::max(fbHeight, 1));

                            const float verticalFov = glm::radians(50.0f);
                            const float horizontalFov = 2.0f * std::atan(std::tan(verticalFov * 0.5f) * aspect);
                            const float limitingHalfFov = std::max(0.05f, std::min(verticalFov, horizontalFov) * 0.5f);
                            const float selectionRadius = glm::length(halfExtent);
                            const float fitDistance = std::clamp(
                                (selectionRadius / std::tan(limitingHalfFov)) * 1.18f,
                                kMinCameraZoom,
                                kMaxCameraZoom);

                            viewControls.zoomTargetDistance = fitDistance;
                            importStatus = "Camera framing selection (Z).";
                        }
                    }
                }
                if (ImGui::IsKeyPressed(ImGuiKey_1, false))
                {
                    applyWeatherPreset(0);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_2, false))
                {
                    applyWeatherPreset(1);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_3, false))
                {
                    applyWeatherPreset(2);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_4, false))
                {
                    applyWeatherPreset(3);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_5, false))
                {
                    applyWeatherPreset(4);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_6, false))
                {
                    applyWeatherPreset(5);
                }
            }

            static constexpr ImGuiWindowFlags kWorkspaceFlags =
                ImGuiWindowFlags_MenuBar |
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings;

            const ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));

            ImGui::Begin("Workspace", nullptr, kWorkspaceFlags);

            if (ImGui::BeginMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (ImGui::BeginMenu("Import"))
                    {
                        if (ImGui::MenuItem("FBX..."))
                        {
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

                if (ImGui::BeginMenu("Camera"))
                {
                    if (ImGui::SliderFloat("Zoom", &viewControls.zoomTargetDistance, kMinCameraZoom, kMaxCameraZoom, "%.2f"))
                    {
                        // Menu slider acts as an explicit camera command, so keep current zoom in sync.
                        viewControls.zoomDistance = viewControls.zoomTargetDistance;
                    }
                    if (ImGui::DragFloat2("Pan", &viewControls.panOffset.x, 0.01f, -10.0f, 10.0f, "%.2f"))
                    {
                        viewControls.panTargetOffset = viewControls.panOffset;
                    }
                    ImGui::DragFloat2("World Rotation", &viewControls.worldRotationDegrees.x, 0.5f, -180.0f, 180.0f, "%.1f deg");
                    ImGui::Checkbox("Pan Mode (Ctrl+P)", &panModeEnabled);

                    if (ImGui::Button("Reset Camera"))
                    {
                        viewControls = sparks::render::ViewControls{};
                        panModeEnabled = false;
                    }

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Transform"))
                {
                    if (ImGui::MenuItem("Move (W)", nullptr, transformMode == TransformMode::Move))
                    {
                        transformMode = TransformMode::Move;
                    }
                    if (ImGui::MenuItem("Rotate (E)", nullptr, transformMode == TransformMode::Rotate))
                    {
                        transformMode = TransformMode::Rotate;
                    }
                    if (ImGui::MenuItem("Scale (R)", nullptr, transformMode == TransformMode::Scale))
                    {
                        transformMode = TransformMode::Scale;
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Weather"))
                {
                    WeatherSettings weatherSettings = weatherSystem.settings();
                    bool changed = false;

                    if (ImGui::BeginMenu("Rain"))
                    {
                        static constexpr const char *kRainConceptNames[] = {
                            "Vertical Drizzle Curtain",
                            "Layered Depth Rain",
                            "Directional Storm Sheets",
                            "Wind-Gust Bursts",
                            "Tropical Monsoon",
                            "Neon City Rain",
                        };
                        static constexpr const char *kRainConceptNotes[] = {
                            "Fine vertical streaks with subtle impacts.",
                            "Balanced all-around profile with depth feel.",
                            "Fast slanted sheets driven by strong wind.",
                            "Unstable gusty rain with bursty motion.",
                            "Dense heavy drops with aggressive splashes.",
                            "Bright cyan-leaning rain for reflective scenes.",
                        };

                        int selectedPreset = std::clamp(weatherSettings.rainConcept, 0, 5);
                        if (ImGui::Combo("Rain Concept", &selectedPreset, kRainConceptNames, IM_ARRAYSIZE(kRainConceptNames)))
                        {
                            applyWeatherPreset(selectedPreset);
                            weatherSettings = weatherSystem.settings();
                        }
                        ImGui::TextUnformatted(kRainConceptNotes[std::clamp(selectedPreset, 0, 5)]);
                        ImGui::TextUnformatted("Hotkeys: 1-6");

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
                        changed |= ImGui::SliderFloat("Rain Length Min", &weatherSettings.rainLengthMin, 0.1f, 8.0f, "%.2f");
                        changed |= ImGui::SliderFloat("Rain Length Max", &weatherSettings.rainLengthMax, 0.1f, 12.0f, "%.2f");
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

                        ImGui::EndMenu();
                    }
                    if (changed)
                    {
                        weatherSystem.setSettings(weatherSettings);
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Environment"))
                {
                    bool envChanged = false;
                    envChanged |= ImGui::SliderFloat("Latitude", &environmentSettings.latitude, -90.0f, 90.0f, "%.4f");
                    envChanged |= ImGui::SliderFloat("Longitude", &environmentSettings.longitude, -180.0f, 180.0f, "%.4f");
                    envChanged |= ImGui::SliderFloat("UTC Time (hours)", &environmentSettings.utcTime, 0.0f, 24.0f, "%.4f");
                    envChanged |= ImGui::SliderFloat2("Wind Direction (XZ)", &environmentSettings.windDirection.x, -1.0f, 1.0f, "%.2f");
                    envChanged |= ImGui::SliderFloat("Wind Speed (m/s)", &environmentSettings.windSpeed, 0.0f, 30.0f, "%.2f");
                    // Normalize wind direction
                    if (glm::length(environmentSettings.windDirection) > 0.001f)
                    {
                        environmentSettings.windDirection = glm::normalize(environmentSettings.windDirection);
                    }
                    else
                    {
                        environmentSettings.windDirection = glm::vec2(1.0f, 0.0f);
                    }
                    envChanged |= ImGui::Checkbox("Cinematic Mode", &environmentSettings.enableCinematic);
                    ImGui::Separator();

                    if (ImGui::BeginMenu("Skydome"))
                    {
                        if (ImGui::Button("Preset: Night Sky"))
                        {
                            environmentSettings.enableSkydome = true;
                            // Night sky: very dark blue/black
                            environmentSettings.skyHorizonColor = glm::vec3(0.04f, 0.07f, 0.16f);
                            environmentSettings.skyZenithColor = glm::vec3(0.01f, 0.02f, 0.08f);
                            // No sun
                            environmentSettings.enableSun = false;
                            environmentSettings.sunDiscSize = 0.2f; // match clamp min
                            environmentSettings.sunIntensity = 0.0f;
                            environmentSettings.sunHaloSize = 0.10f;
                            environmentSettings.sunHaloStrength = 0.13f;
                            environmentSettings.sunColor = glm::vec3(1.00f, 0.98f, 0.92f);
                            // Stars: very visible
                            environmentSettings.starDensity = 0.002f; // realistic, sparse
                            // Clouds: subtle, if any
                            environmentSettings.skyCloudColor = glm::vec3(0.18f, 0.19f, 0.22f);
                            environmentSettings.skyCloudAmount = 0.08f;
                            environmentSettings.skyCloudScale = 1.18f;
                            // Force ImGui to update checkbox state
                            ImGui::SetItemDefaultFocus();
                            envChanged = true;
                        }
                        if (ImGui::Button("Preset: Clear Sky"))
                        {
                            environmentSettings.enableSkydome = true;
                            // Sky colors: soft blue gradient, lighter at horizon, deeper at zenith
                            environmentSettings.skyHorizonColor = glm::vec3(0.78f, 0.89f, 0.98f); // very light blue
                            environmentSettings.skyZenithColor = glm::vec3(0.32f, 0.60f, 0.92f);  // deep blue
                            // Clouds: puffy, bright, scattered
                            environmentSettings.skyCloudColor = glm::vec3(0.98f, 0.99f, 1.00f); // bright white
                            environmentSettings.skyCloudAmount = 0.22f;                         // more visible, but still sparse
                            environmentSettings.skyCloudScale = 1.18f;                          // slightly larger, more separation
                            // Sun: small, bright, sharp disc, subtle halo (reduced intensity)
                            environmentSettings.enableSun = true;
                            environmentSettings.sunDiscSize = 0.08f;
                            environmentSettings.sunIntensity = 1.15f;
                            environmentSettings.sunHaloSize = 0.10f;
                            environmentSettings.sunHaloStrength = 0.13f;
                            environmentSettings.sunColor = glm::vec3(1.00f, 0.98f, 0.92f);
                            // Stars: make visible
                            environmentSettings.starDensity = 0.002f; // realistic, sparse
                            ImGui::SetItemDefaultFocus();
                            envChanged |= ImGui::SliderFloat("Star Density", &environmentSettings.starDensity, 0.0f, 0.2f, "%.3f");
                            envChanged = true;
                        }
                        if (ImGui::Button("Preset: Scattered Cumulus"))
                        {
                            environmentSettings.enableSkydome = true;
                            environmentSettings.skyHorizonColor = glm::vec3(0.72f, 0.84f, 0.96f);
                            environmentSettings.skyZenithColor = glm::vec3(0.30f, 0.50f, 0.80f);
                            environmentSettings.skyCloudColor = glm::vec3(0.97f, 0.97f, 1.00f);
                            environmentSettings.skyCloudAmount = 0.65f;
                            environmentSettings.skyCloudScale = 1.20f;
                            envChanged = true;
                        }
                        if (ImGui::Button("Preset: Heavy Overcast"))
                        {
                            environmentSettings.enableSkydome = true;
                            environmentSettings.skyHorizonColor = glm::vec3(0.68f, 0.72f, 0.78f);
                            environmentSettings.skyZenithColor = glm::vec3(0.52f, 0.57f, 0.64f);
                            environmentSettings.skyCloudColor = glm::vec3(0.88f, 0.89f, 0.91f);
                            environmentSettings.skyCloudAmount = 1.40f;
                            environmentSettings.skyCloudScale = 0.80f;
                            envChanged = true;
                        }
                        if (ImGui::Button("Preset: Golden Hour"))
                        {
                            environmentSettings.enableSkydome = true;
                            environmentSettings.skyHorizonColor = glm::vec3(0.92f, 0.62f, 0.38f);
                            environmentSettings.skyZenithColor = glm::vec3(0.28f, 0.40f, 0.70f);
                            environmentSettings.skyCloudColor = glm::vec3(1.00f, 0.78f, 0.52f);
                            environmentSettings.skyCloudAmount = 0.80f;
                            environmentSettings.skyCloudScale = 1.40f;
                            envChanged = true;
                        }
                        ImGui::Separator();
                        envChanged |= ImGui::Checkbox("Enable Skydome", &environmentSettings.enableSkydome);
                        envChanged |= ImGui::SliderFloat("Skydome Radius", &environmentSettings.skydomeRadius, 20.0f, 4000.0f, "%.1f");
                        envChanged |= ImGui::ColorEdit3("Sky Horizon", &environmentSettings.skyHorizonColor.x);
                        envChanged |= ImGui::ColorEdit3("Sky Zenith", &environmentSettings.skyZenithColor.x);
                        envChanged |= ImGui::ColorEdit3("Sky Cloud Color", &environmentSettings.skyCloudColor.x);
                        envChanged |= ImGui::SliderFloat("Sky Cloud Amount", &environmentSettings.skyCloudAmount, 0.0f, 1.5f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Sky Cloud Scale", &environmentSettings.skyCloudScale, 0.1f, 8.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Cloud Speed", &environmentSettings.skyCloudSpeed, 0.0f, 4.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Cloud Shadow Strength", &environmentSettings.skyCloudShadowStrength, 0.0f, 1.5f, "%.2f");
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Sun"))
                    {
                        glm::vec3 lightDir = environmentSettings.terrainLightDirection;
                        float lightLen = glm::length(lightDir);
                        if (lightLen < 0.001f)
                        {
                            lightDir = glm::vec3(0.30f, 0.72f, -0.46f);
                            lightLen = glm::length(lightDir);
                        }
                        const glm::vec3 lightN = lightDir / lightLen;
                        float sunElevationDeg = glm::degrees(glm::asin(glm::clamp(lightN.y, -1.0f, 1.0f)));
                        float sunAzimuthDeg = glm::degrees(std::atan2(lightN.x, lightN.z));

                        if (ImGui::Button("Apply 10AM Preset"))
                        {
                            sunElevationDeg = 42.0f;
                            sunAzimuthDeg = 145.0f;

                            const float elevRad = glm::radians(sunElevationDeg);
                            const float azimRad = glm::radians(sunAzimuthDeg);
                            const float horizontal = std::cos(elevRad);
                            const glm::vec3 rebuiltDir(
                                std::sin(azimRad) * horizontal,
                                std::sin(elevRad),
                                std::cos(azimRad) * horizontal);
                            environmentSettings.terrainLightDirection = rebuiltDir * lightLen;

                            environmentSettings.enableSun = true;
                            environmentSettings.sunIntensity = 1.30f;
                            environmentSettings.waterSunStrength = 2.40f;
                            environmentSettings.sunHeatStrength = 0.45f;
                            environmentSettings.sunRayStrength = 1.22f;
                            environmentSettings.lensFlareStrength = 0.92f;
                            environmentSettings.sunColor = glm::vec3(1.00f, 0.94f, 0.79f);
                            environmentSettings.enableFog = true;
                            environmentSettings.fogColor = glm::vec3(0.69f, 0.76f, 0.83f);
                            environmentSettings.fogNear = 12.0f;
                            environmentSettings.fogFar = 126.0f;
                            environmentSettings.fogStrength = 0.50f;
                            envChanged = true;
                        }
                        ImGui::SameLine();
                        ImGui::TextUnformatted("(Calibrated morning light)");

                        if (ImGui::Button("Apply Clear Noon Preset"))
                        {
                            sunElevationDeg = 72.0f;
                            sunAzimuthDeg = 178.0f;

                            const float elevRad = glm::radians(sunElevationDeg);
                            const float azimRad = glm::radians(sunAzimuthDeg);
                            const float horizontal = std::cos(elevRad);
                            const glm::vec3 rebuiltDir(
                                std::sin(azimRad) * horizontal,
                                std::sin(elevRad),
                                std::cos(azimRad) * horizontal);
                            environmentSettings.terrainLightDirection = rebuiltDir * lightLen;

                            environmentSettings.enableSun = true;
                            environmentSettings.sunIntensity = 1.10f;
                            environmentSettings.waterSunStrength = 1.85f;
                            environmentSettings.sunHeatStrength = 0.22f;
                            environmentSettings.sunRayStrength = 0.42f;
                            environmentSettings.lensFlareStrength = 0.36f;
                            environmentSettings.sunColor = glm::vec3(1.00f, 0.98f, 0.92f);
                            environmentSettings.enableFog = true;
                            environmentSettings.fogColor = glm::vec3(0.71f, 0.81f, 0.90f);
                            environmentSettings.fogNear = 42.0f;
                            environmentSettings.fogFar = 240.0f;
                            environmentSettings.fogStrength = 0.16f;
                            envChanged = true;
                        }

                        if (ImGui::Button("Apply Cinematic Sunset Preset"))
                        {
                            sunElevationDeg = 12.0f;
                            sunAzimuthDeg = 118.0f;

                            const float elevRad = glm::radians(sunElevationDeg);
                            const float azimRad = glm::radians(sunAzimuthDeg);
                            const float horizontal = std::cos(elevRad);
                            const glm::vec3 rebuiltDir(
                                std::sin(azimRad) * horizontal,
                                std::sin(elevRad),
                                std::cos(azimRad) * horizontal);
                            environmentSettings.terrainLightDirection = rebuiltDir * lightLen;

                            environmentSettings.enableSun = true;
                            environmentSettings.sunIntensity = 1.78f;
                            environmentSettings.waterSunStrength = 2.85f;
                            environmentSettings.sunHeatStrength = 0.82f;
                            environmentSettings.sunRayStrength = 1.92f;
                            environmentSettings.lensFlareStrength = 1.45f;
                            environmentSettings.sunColor = glm::vec3(1.00f, 0.66f, 0.40f);
                            environmentSettings.enableFog = true;
                            environmentSettings.fogColor = glm::vec3(0.83f, 0.58f, 0.45f);
                            environmentSettings.fogNear = 9.0f;
                            environmentSettings.fogFar = 118.0f;
                            environmentSettings.fogStrength = 0.68f;
                            envChanged = true;
                        }

                        envChanged |= ImGui::Checkbox("Enable Sun", &environmentSettings.enableSun);
                        envChanged |= ImGui::SliderFloat("Sun Disc Size", &environmentSettings.sunDiscSize, 0.2f, 8.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Sun Intensity", &environmentSettings.sunIntensity, 0.0f, 4.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Sun Halo Size", &environmentSettings.sunHaloSize, 0.01f, 1.0f, "%.3f");
                        envChanged |= ImGui::SliderFloat("Sun Halo Strength", &environmentSettings.sunHaloStrength, 0.0f, 2.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Water Sun Strength", &environmentSettings.waterSunStrength, 0.0f, 6.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Object Sun Glow", &environmentSettings.objectSunGlowStrength, 0.0f, 1.6f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Sun Ray Strength", &environmentSettings.sunRayStrength, 0.0f, 2.5f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Lens Flare Strength", &environmentSettings.lensFlareStrength, 0.0f, 2.5f, "%.2f");
                        envChanged |= ImGui::ColorEdit3("Sun Color", &environmentSettings.sunColor.x);
                        envChanged |= ImGui::SliderFloat("Heat Strength", &environmentSettings.sunHeatStrength, 0.0f, 3.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Dust Amount", &environmentSettings.dustAmount, 0.0f, 1.5f, "%.2f");
                        envChanged |= ImGui::ColorEdit3("Dust Color", &environmentSettings.dustColor.x);
                        bool sunAngleChanged = false;
                        sunAngleChanged |= ImGui::SliderFloat("Sun Elevation", &sunElevationDeg, -5.0f, 89.0f, "%.1f deg");
                        sunAngleChanged |= ImGui::SliderFloat("Sun Azimuth", &sunAzimuthDeg, -180.0f, 180.0f, "%.1f deg");
                        if (sunAngleChanged)
                        {
                            const float elevRad = glm::radians(sunElevationDeg);
                            const float azimRad = glm::radians(sunAzimuthDeg);
                            const float horizontal = std::cos(elevRad);
                            const glm::vec3 rebuiltDir(
                                std::sin(azimRad) * horizontal,
                                std::sin(elevRad),
                                std::cos(azimRad) * horizontal);
                            environmentSettings.terrainLightDirection = rebuiltDir * lightLen;
                            envChanged = true;
                        }
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Terrain"))
                    {
                        if (ImGui::Button("Apply Steep Hill Preset"))
                        {
                            environmentSettings.enableTerrain = true;
                            environmentSettings.terrainSize = 240.0f;
                            environmentSettings.terrainHeight = -0.95f;
                            environmentSettings.terrainColorA = glm::vec3(0.14f, 0.19f, 0.13f);
                            environmentSettings.terrainColorB = glm::vec3(0.22f, 0.27f, 0.19f);
                            environmentSettings.terrainPatchScale = 0.16f;
                            environmentSettings.terrainRoughness = 1.55f;
                            environmentSettings.fbxShadowSoftness = 1.15f;
                            envChanged = true;
                        }
                        if (ImGui::Button("Apply Natural Hills Preset"))
                        {
                            environmentSettings.enableTerrain = true;
                            environmentSettings.terrainSize = 260.0f;
                            environmentSettings.terrainHeight = -0.70f;
                            environmentSettings.terrainColorA = glm::vec3(0.15f, 0.20f, 0.14f);
                            environmentSettings.terrainColorB = glm::vec3(0.25f, 0.30f, 0.22f);
                            environmentSettings.terrainPatchScale = 0.13f;
                            environmentSettings.terrainRoughness = 1.15f;
                            environmentSettings.fbxShadowSoftness = 1.10f;
                            envChanged = true;
                        }
                        if (ImGui::Button("Apply Open Plains Preset"))
                        {
                            environmentSettings.enableTerrain = true;
                            environmentSettings.terrainSize = 320.0f;
                            environmentSettings.terrainHeight = -0.60f;
                            environmentSettings.terrainColorA = glm::vec3(0.19f, 0.22f, 0.15f);
                            environmentSettings.terrainColorB = glm::vec3(0.28f, 0.31f, 0.22f);
                            environmentSettings.terrainPatchScale = 0.09f;
                            environmentSettings.terrainRoughness = 0.72f;
                            environmentSettings.fbxShadowSoftness = 1.00f;
                            envChanged = true;
                        }
                        ImGui::Separator();
                        envChanged |= ImGui::Checkbox("Enable Terrain", &environmentSettings.enableTerrain);
                        envChanged |= ImGui::SliderFloat("Terrain Size", &environmentSettings.terrainSize, 20.0f, 4000.0f, "%.1f");
                        envChanged |= ImGui::SliderFloat("Terrain Height", &environmentSettings.terrainHeight, -20.0f, 20.0f, "%.2f");
                        envChanged |= ImGui::ColorEdit3("Terrain Color A", &environmentSettings.terrainColorA.x);
                        envChanged |= ImGui::ColorEdit3("Terrain Color B", &environmentSettings.terrainColorB.x);
                        envChanged |= ImGui::SliderFloat("Terrain Patch Scale", &environmentSettings.terrainPatchScale, 0.01f, 4.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Terrain Roughness", &environmentSettings.terrainRoughness, 0.0f, 3.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("FBX Shadow Softness", &environmentSettings.fbxShadowSoftness, 0.4f, 2.6f, "%.2f");
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Water"))
                    {
                        if (ImGui::Button("Apply Clear Lake Preset"))
                        {
                            environmentSettings.enableWater = true;
                            environmentSettings.waterLevel = 0.0f;
                            environmentSettings.waterColor = glm::vec3(0.12f, 0.44f, 0.60f);
                            environmentSettings.waterOpacity = 0.34f;
                            environmentSettings.waveAmplitude = 0.09f;
                            environmentSettings.waveFrequency = 1.45f;
                            environmentSettings.waterFoamIntensity = 1.0f;
                            envChanged = true;
                        }
                        if (ImGui::Button("Apply Windy Ocean Preset"))
                        {
                            environmentSettings.enableWater = true;
                            environmentSettings.waterLevel = 0.0f;
                            environmentSettings.waterColor = glm::vec3(0.09f, 0.32f, 0.52f);
                            environmentSettings.waterOpacity = 0.46f;
                            environmentSettings.waveAmplitude = 0.26f;
                            environmentSettings.waveFrequency = 2.35f;
                            environmentSettings.waterFoamIntensity = 1.0f;
                            envChanged = true;
                        }
                        ImGui::Separator();
                        envChanged |= ImGui::Checkbox("Enable Water", &environmentSettings.enableWater);
                        envChanged |= ImGui::SliderFloat("Water Level", &environmentSettings.waterLevel, -10.0f, 10.0f, "%.2f");
                        envChanged |= ImGui::ColorEdit3("Water Color", &environmentSettings.waterColor.x);
                        envChanged |= ImGui::SliderFloat("Water Opacity", &environmentSettings.waterOpacity, 0.1f, 1.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Wave Amplitude", &environmentSettings.waveAmplitude, 0.0f, 1.0f, "%.3f");
                        envChanged |= ImGui::SliderFloat("Wave Frequency", &environmentSettings.waveFrequency, 0.1f, 5.0f, "%.2f");
                        envChanged |= ImGui::SliderFloat("Foam Intensity", &environmentSettings.waterFoamIntensity, 0.0f, 2.0f, "%.2f");
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("Fog"))
                    {
                        if (ImGui::Button("Apply Clear Air Preset"))
                        {
                            environmentSettings.enableFog = true;
                            environmentSettings.fogColor = glm::vec3(0.72f, 0.82f, 0.92f);
                            environmentSettings.fogNear = 55.0f;
                            environmentSettings.fogFar = 300.0f;
                            environmentSettings.fogStrength = 0.12f;
                            envChanged = true;
                        }
                        if (ImGui::Button("Apply Morning Mist Preset"))
                        {
                            environmentSettings.enableFog = true;
                            environmentSettings.fogColor = glm::vec3(0.74f, 0.79f, 0.85f);
                            environmentSettings.fogNear = 14.0f;
                            environmentSettings.fogFar = 140.0f;
                            environmentSettings.fogStrength = 0.48f;
                            envChanged = true;
                        }
                        if (ImGui::Button("Apply Heavy Cinematic Fog Preset"))
                        {
                            environmentSettings.enableFog = true;
                            environmentSettings.fogColor = glm::vec3(0.64f, 0.66f, 0.70f);
                            environmentSettings.fogNear = 6.0f;
                            environmentSettings.fogFar = 82.0f;
                            environmentSettings.fogStrength = 0.76f;
                            envChanged = true;
                        }
                        ImGui::Separator();
                        envChanged |= ImGui::Checkbox("Enable Fog", &environmentSettings.enableFog);
                        envChanged |= ImGui::ColorEdit3("Fog Color", &environmentSettings.fogColor.x);
                        envChanged |= ImGui::SliderFloat("Fog Near", &environmentSettings.fogNear, 1.0f, 250.0f, "%.1f");
                        envChanged |= ImGui::SliderFloat("Fog Far", &environmentSettings.fogFar, 5.0f, 500.0f, "%.1f");
                        envChanged |= ImGui::SliderFloat("Fog Strength", &environmentSettings.fogStrength, 0.0f, 1.0f, "%.2f");
                        ImGui::EndMenu();
                    }


                    if (envChanged)
                    {
                        renderer.setEnvironmentSettings(environmentSettings);
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            const glm::vec3 weatherCenter = fixedRainCenter;
            std::vector<WeatherSystem::CollisionBox> rainCollisionBoxes;
            if (importedModel.has_value())
            {
                glm::vec3 modelMin(0.0f);
                glm::vec3 modelMax(0.0f);
                computeImportedModelBounds(*importedModel, modelMin, modelMax);
                WeatherSystem::CollisionBox box;
                box.min = modelMin;
                box.max = modelMax;
                rainCollisionBoxes.push_back(box);
            }
            WeatherSystem::TerrainSurface terrainSurface;
            terrainSurface.enabled = environmentSettings.enableTerrain;
            terrainSurface.size = environmentSettings.terrainSize;
            terrainSurface.height = environmentSettings.terrainHeight;
            terrainSurface.patchScale = environmentSettings.terrainPatchScale;
            terrainSurface.roughness = environmentSettings.terrainRoughness;
            
            sectionStart = glfwGetTime();
            weatherSystem.update(deltaSeconds, weatherCenter, rainCollisionBoxes, environmentSettings.terrainHeight, terrainSurface);
            sectionWeatherTime += glfwGetTime() - sectionStart;
            renderer.setWeatherRain(
                weatherSystem.rainLineVertices(),
                weatherSystem.splashPoints(),
                weatherSystem.dropletPoints(),
                weatherSystem.ripplePoints(),
                weatherSystem.settings().rainConcept,
                false,
                glm::vec3(1.0f, 1.0f, 1.0f),
                glm::vec3(1.0f, 1.0f, 1.0f),
                glm::vec3(1.0f, 1.0f, 1.0f),
                glm::vec3(1.0f, 1.0f, 1.0f),
                1.0f,
                1.0f,
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
            if (leftPaneWidth < 0.0f)
            {
                leftPaneWidth = workspaceSize.x * 0.72f;
            }
            leftPaneWidth = std::clamp(leftPaneWidth, 200.0f, workspaceSize.x - 220.0f);

            ImGui::BeginChild("ViewportPane", ImVec2(leftPaneWidth, contentHeight), true);
            {
                if (ImGui::BeginTabBar("SceneTabs"))
                {
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
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            if (ImGui::IsItemActive())
            {
                leftPaneWidth = std::clamp(leftPaneWidth + io.MouseDelta.x, 200.0f, workspaceSize.x - 220.0f);
            }

            ImGui::SameLine();

            ui::drawRightPane(contentHeight, io, renderer, environmentSettings, selectedCloudIndices, importedModel, importedModelSelected);

            ui::drawStatusBar(transformMode, panModeEnabled, importStatus, statusBarHeight);

            ImGui::End();

            ImGui::PopStyleVar(2);

            sectionImGuiTime += glfwGetTime() - sectionStart;
            
            sectionStart = glfwGetTime();
            ImGui::Render();

            int displayWidth = 0;
            int displayHeight = 0;
            glfwGetFramebufferSize(m_window, &displayWidth, &displayHeight);
            glViewport(0, 0, displayWidth, displayHeight);
            glClearColor(0.05f, 0.06f, 0.07f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(m_window);
            sectionGLTime += glfwGetTime() - sectionStart;
            sectionFrameCount++;

            // Print section profiling every 2 seconds
            if (glfwGetTime() - lastProfileTime > 2.0) {
                double totalMs = (sectionAnimTime + sectionWeatherTime + sectionImGuiTime + sectionGLTime) / sectionFrameCount * 1000.0;
                printf("[Section Profiling] Anim: %.2f ms, Weather: %.2f ms, ImGui: %.2f ms, GL: %.2f ms, Total: %.2f ms (avg per frame)\n",
                    sectionAnimTime / sectionFrameCount * 1000.0,
                    sectionWeatherTime / sectionFrameCount * 1000.0,
                    sectionImGuiTime / sectionFrameCount * 1000.0,
                    sectionGLTime / sectionFrameCount * 1000.0,
                    totalMs);
                lastProfileTime = glfwGetTime();
                sectionAnimTime = 0.0;
                sectionWeatherTime = 0.0;
                sectionImGuiTime = 0.0;
                sectionGLTime = 0.0;
                sectionFrameCount = 0;
            }
        }

        return 0;
    }

    void Application::initializeWindow()
    {
        if (glfwInit() == GLFW_FALSE)
        {
            throw std::runtime_error("Failed to initialize GLFW.");
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

        m_window = glfwCreateWindow(1600, 900, "SparksEngine - FBX Editor", nullptr, nullptr);
        if (m_window == nullptr)
        {
            throw std::runtime_error("Failed to create GLFW window.");
        }

        glfwMakeContextCurrent(m_window);
        glfwMaximizeWindow(m_window);
        // Disable VSync for maximum FPS (may cause tearing)
        glfwSwapInterval(0);

        const int loaded = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress));
        if (loaded == 0)
        {
            throw std::runtime_error("Failed to initialize GLAD.");
        }
    }

    void Application::initializeImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        if (!ImGui_ImplGlfw_InitForOpenGL(m_window, true))
        {
            throw std::runtime_error("Failed to initialize ImGui GLFW backend.");
        }

        if (!ImGui_ImplOpenGL3_Init("#version 460"))
        {
            throw std::runtime_error("Failed to initialize ImGui OpenGL backend.");
        }
    }

    void Application::shutdownImGui()
    {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

} // namespace sparks::core