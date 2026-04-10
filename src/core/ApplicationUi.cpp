#include "sparks/core/ApplicationUi.hpp"
#include "sparks/core/FbxImport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <map>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/glm.hpp>

namespace sparks::core::ui {

using sparks::core::FbxAnimationSettings;
namespace {

constexpr float kMinCameraZoom = 1.5f;
constexpr float kMaxCameraZoom = 250.0f;
constexpr float kCameraFarPlane = 1000.0f;

using rigging::RigBone;
using rigging::VertexGroupInfo;
using rigging::applyDefaultHumanoidMapping;
using rigging::enforceHumanoidTPose;
constexpr auto& kHumanoidSlotNames = rigging::kHumanoidSlotNames;

std::string formatVec3(const glm::vec3& value) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "(%.2f, %.2f, %.2f)", value.x, value.y, value.z);
    return std::string(buffer);
}

std::uint32_t randomCloudPlaneSeed() {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<std::uint32_t> dist(1u, 0xffffffffu);
    return dist(rng);
}

void applyCloudTypePreset(render::CloudObjectSettings& cloud, const int cloudType) {
    cloud.cloudType = std::clamp(cloudType, 0, 3);

    const float horizontalSpan = std::max(std::max(std::abs(cloud.scale.x), std::abs(cloud.scale.z)), 1.0f);
    if (cloud.cloudType == 0) {
        cloud.opacity = 0.88f;
        cloud.softness = 0.68f;
        cloud.detail = 2.1f;
        cloud.motionSpeed = 1.8f;
        cloud.glowStrength = 0.34f;
        cloud.planeFade = 0.92f;
        cloud.planeCount = 1;
        cloud.cubeSpread = 1.00f;
        cloud.scale.y = std::clamp(horizontalSpan * 0.50f, 0.75f, 8.0f);
        cloud.color = glm::vec3(0.93f, 0.96f, 1.0f);
    } else if (cloud.cloudType == 1) {
        cloud.opacity = 0.74f;
        cloud.softness = 0.92f;
        cloud.detail = 1.25f;
        cloud.motionSpeed = 0.70f;
        cloud.glowStrength = 0.14f;
        cloud.planeFade = 0.98f;
        cloud.planeCount = 1;
        cloud.cubeSpread = 1.42f;
        cloud.scale.y = std::clamp(horizontalSpan * 0.22f, 0.35f, 3.5f);
        cloud.color = glm::vec3(0.85f, 0.91f, 0.98f);
    } else if (cloud.cloudType == 2) {
        cloud.opacity = 0.54f;
        cloud.softness = 0.95f;
        cloud.detail = 3.0f;
        cloud.motionSpeed = 3.25f;
        cloud.glowStrength = 0.18f;
        cloud.planeFade = 0.96f;
        cloud.planeCount = 1;
        cloud.cubeSpread = 1.88f;
        cloud.scale.y = std::clamp(horizontalSpan * 0.14f, 0.20f, 2.2f);
        cloud.color = glm::vec3(0.90f, 0.96f, 1.0f);
    } else {
        // Wispy high-altitude streaks, similar to mare's-tail cirrus.
        cloud.opacity = 0.42f;
        cloud.softness = 0.97f;
        cloud.detail = 3.8f;
        cloud.motionSpeed = 2.6f;
        cloud.glowStrength = 0.10f;
        cloud.planeFade = 0.98f;
        cloud.planeCount = 1;
        cloud.cubeSpread = 2.05f;
        cloud.scale.y = std::clamp(horizontalSpan * 0.10f, 0.14f, 1.4f);
        cloud.color = glm::vec3(0.90f, 0.95f, 1.0f);
    }
}

bool endsWith(const std::string& value, const char* suffix) {
    const std::size_t valueLen = value.size();
    const std::size_t suffixLen = std::char_traits<char>::length(suffix);
    return valueLen >= suffixLen && value.compare(valueLen - suffixLen, suffixLen, suffix) == 0;
}

glm::vec3 importedModelWorldDimensions(const render::ImportedModelData& model) {
    return glm::abs(model.dimensions * model.scale);
}

float sampleOceanWaveHeight(const render::EnvironmentSettings& environmentSettings, const glm::vec2& positionXZ, const float timeSeconds) {
    if (!environmentSettings.enableWater) {
        return environmentSettings.waterLevel;
    }

    const float amp = std::max(environmentSettings.waveAmplitude, 0.001f);
    const float wf = std::max(environmentSettings.waveFrequency, 0.01f);
    const float baseWavelength = 30.0f / wf;
    const float gravity = 9.81f;
    const float pi = 3.14159265359f;

    auto addWave = [&](const glm::vec2& direction, const float amplitudeScale, const float wavelengthScale, const float speedScale) {
        const glm::vec2 dir = glm::normalize(direction);
        const float wavelength = baseWavelength * wavelengthScale;
        const float k = 2.0f * pi / std::max(wavelength, 0.0001f);
        const float angularVelocity = std::sqrt(gravity * k) * speedScale;
        return amp * amplitudeScale * std::sin(k * glm::dot(dir, positionXZ) + angularVelocity * timeSeconds);
    };

    float height = environmentSettings.waterLevel;
    height += addWave(glm::vec2( 1.00f,  0.42f), 1.00f, 1.00f, 0.88f);
    height += addWave(glm::vec2(-0.55f,  1.00f), 0.68f, 0.65f, 0.95f);
    height += addWave(glm::vec2( 0.80f, -0.62f), 0.38f, 0.40f, 1.10f);
    height += addWave(glm::vec2(-0.90f,  0.45f), 0.28f, 0.30f, 1.22f);
    height += addWave(glm::vec2( 0.40f,  1.00f), 0.14f, 0.16f, 1.30f);
    height += addWave(glm::vec2( 1.00f, -0.22f), 0.10f, 0.12f, 1.45f);
    return height;
}

void updateFloatingImportedModel(render::ImportedModelData& model, const render::EnvironmentSettings& environmentSettings, render::Renderer& renderer) {
    if (!model.floatOnWater || !environmentSettings.enableWater) {
        return;
    }

    const glm::vec3 worldDimensions = importedModelWorldDimensions(model);
    const float halfHeight = worldDimensions.y * 0.5f;
    const float draft = worldDimensions.y * 0.18f;
    const float timeSeconds = static_cast<float>(ImGui::GetTime());
    const float waterHeight = sampleOceanWaveHeight(environmentSettings, glm::vec2(model.position.x, model.position.z), timeSeconds);
    const float bobPhase = timeSeconds * std::max(model.floatBobFrequency, 0.0f)
        + model.position.x * 0.15f
        + model.position.z * 0.09f;
    const float bobOffset = std::sin(bobPhase) * std::max(model.floatBobAmplitude, 0.0f);

    model.position.y = waterHeight + halfHeight - draft + model.floatHeightOffset + bobOffset;
    renderer.setImportedModelTransform(model.position, model.rotationEulerDegrees, model.scale);
}

glm::quat composeRotationXYZDegrees(const glm::vec3& eulerDegrees) {
    const glm::vec3 r = glm::radians(eulerDegrees);
    const glm::quat qx = glm::angleAxis(r.x, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::quat qy = glm::angleAxis(r.y, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat qz = glm::angleAxis(r.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::normalize(qx * qy * qz);
}

glm::vec3 eulerDegreesFromQuatXYZ(const glm::quat& q) {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    glm::extractEulerAngleXYZ(glm::toMat4(glm::normalize(q)), x, y, z);
    return glm::degrees(glm::vec3(x, y, z));
}

}  // namespace

void drawStatusBar(const TransformMode transformMode, const bool panModeEnabled, const std::string& importStatus, const float statusBarHeight) {
    ImGui::Separator();
    ImGui::BeginChild("StatusBar", ImVec2(0.0f, statusBarHeight), false, ImGuiWindowFlags_NoScrollbar);
    const char* modeLabel = (transformMode == TransformMode::Move)
        ? "Move"
        : (transformMode == TransformMode::Rotate ? "Rotate" : "Scale");
    ImGui::Text(
        "Tips: LMB Click=Select | LMB Drag=Box Multi-select | Drag Selected=Transform (%s) | Scroll=Zoom | RMB Drag=Rotate View | WASD=Fly | Q/E=Vertical | Shift=Boost | Ctrl+P=Pan Mode (%s)",
        modeLabel,
        panModeEnabled ? "ON" : "OFF");
    if (!importStatus.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted("|");
        ImGui::SameLine();
        ImGui::TextUnformatted(importStatus.c_str());
    }
    ImGui::EndChild();
}

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
    bool& importedModelSelected) {
    if (!ImGui::BeginTabItem("Scene")) {
        return;
    }

    const bool viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    // Smoothly converge current zoom toward target zoom (Blender-like non-instant dolly feel).
    const float zoomFollowT = 1.0f - std::exp(-10.0f * io.DeltaTime);
    viewControls.zoomDistance = std::clamp(
        glm::mix(viewControls.zoomDistance, viewControls.zoomTargetDistance, zoomFollowT),
        kMinCameraZoom,
        kMaxCameraZoom);

    // Smooth focus transitions (used by Z focus) for pivot X/Y and Z.
    const float focusFollowT = 1.0f - std::exp(-12.0f * io.DeltaTime);
    viewControls.panOffset = glm::mix(viewControls.panOffset, viewControls.panTargetOffset, focusFollowT);
    viewControls.orbitTargetZ = glm::mix(viewControls.orbitTargetZ, viewControls.orbitTargetZTarget, focusFollowT);

    if (viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        viewControls.worldRotationDegrees.x = std::clamp(
            viewControls.worldRotationDegrees.x + io.MouseDelta.y * 0.25f,
            -89.0f,
            89.0f);
        viewControls.worldRotationDegrees.y += io.MouseDelta.x * 0.25f;
    }

    if (viewportHovered) {
        glm::vec3 move(0.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            move.z += 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            move.z -= 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            move.x += 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            move.x -= 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            move.y += 1.0f;
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            move.y -= 1.0f;
        }

        if (glm::dot(move, move) > 0.0f) {
            const float yaw = glm::radians(viewControls.worldRotationDegrees.y);
            const float pitch = glm::radians(viewControls.worldRotationDegrees.x);
            const glm::vec3 forward(
                std::sin(yaw) * std::cos(pitch),
                -std::sin(pitch),
                -std::cos(yaw) * std::cos(pitch));
            const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::cross(forward, worldUp);
            if (glm::dot(right, right) < 0.000001f) {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            } else {
                right = glm::normalize(right);
            }
            const glm::vec3 up = worldUp;

            const glm::vec3 moveDir = glm::normalize(right * move.x + up * move.y + forward * move.z);
            const float sprint = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f;
            const float baseSpeed = std::max(6.0f, viewControls.zoomDistance * 1.60f);
            const float moveStep = baseSpeed * sprint * io.DeltaTime;
            const glm::vec3 delta = moveDir * moveStep;

            viewControls.panOffset.x += delta.x;
            viewControls.panOffset.y += delta.y;
            viewControls.orbitTargetZ += delta.z;

            viewControls.panTargetOffset = viewControls.panOffset;
            viewControls.orbitTargetZTarget = viewControls.orbitTargetZ;
        }
    }

    if (panModeEnabled && viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float panSpeed = 0.004f * viewControls.zoomDistance;
        viewControls.panOffset.x -= io.MouseDelta.x * panSpeed;
        viewControls.panOffset.y += io.MouseDelta.y * panSpeed;
        viewControls.panTargetOffset = viewControls.panOffset;
    }

    const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
    const int viewportWidth = static_cast<int>((viewportSize.x > 1.0f) ? viewportSize.x : 1.0f);
    const int viewportHeight = static_cast<int>((viewportSize.y > 1.0f) ? viewportSize.y : 1.0f);


    // --- FBX Animation Import Settings UI ---
    static FbxAnimationSettings fbxAnimSettings;
    static bool animPlaying = false;
    static float animTime = 0.0f;
    static float animDuration = 1.0f; // Set from actual animation
    static int lastSelectedAnim = -1;

    if (ImGui::CollapsingHeader("FBX Animation Import Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Import Animations", &fbxAnimSettings.importAnimations);
        ImGui::InputFloat("Sample Rate (Hz)", &fbxAnimSettings.animationSampleRate, 1.0f, 5.0f, "%.1f");
        ImGui::Checkbox("Import All Animations", &fbxAnimSettings.importAllAnimations);
        int numAnims = (importedModel ? static_cast<int>(importedModel->animations.size()) : 0);
        if (numAnims > 0) {
            // Build animation name list
            std::vector<const char*> animNames;
            animNames.reserve(numAnims);
            for (const auto& anim : importedModel->animations) {
                animNames.push_back(anim.name.c_str());
            }
            int selectedAnim = std::clamp(fbxAnimSettings.selectedAnimationIndex, 0, numAnims - 1);
            if (ImGui::Combo("Available Animations", &selectedAnim, animNames.data(), numAnims)) {
                fbxAnimSettings.selectedAnimationIndex = selectedAnim;
            }
            // Reset playback time if animation changed
            if (selectedAnim != lastSelectedAnim) {
                animTime = 0.0f;
                lastSelectedAnim = selectedAnim;
            }
            // Show current animation name and index
            ImGui::Text("Current: [%d] %s", selectedAnim, animNames[selectedAnim]);

            // --- Detailed Animation Stack/Channel List ---
            if (ImGui::TreeNode("Animation Stack Details (FBX Structure)")) {
                for (int i = 0; i < numAnims; ++i) {
                    const auto& anim = importedModel->animations[i];
                    if (ImGui::TreeNode((std::string("[Stack] ") + anim.name).c_str())) {
                        ImGui::Text("Duration: %.2f, Ticks/s: %.2f, Channels: %d", anim.duration, anim.ticksPerSecond, (int)anim.channels.size());
                        for (const auto& channel : anim.channels) {
                            if (ImGui::TreeNode((std::string("[Node] ") + channel.boneName).c_str())) {
                                ImGui::Text("Keyframes: %d", (int)channel.keyframes.size());
                                ImGui::TreePop();
                            }
                        }
                        ImGui::TreePop();
                    }
                }
                ImGui::TreePop();
            }
        } else if (!fbxAnimSettings.importAllAnimations) {
            ImGui::InputInt("Selected Animation Index", &fbxAnimSettings.selectedAnimationIndex);
        }

        // --- Animation Playback Controls ---
        int selectedAnimIndex = 0;
        if (importedModel && !importedModel->animations.empty()) {
            // Clamp selected animation index to available animations
            selectedAnimIndex = std::clamp(fbxAnimSettings.selectedAnimationIndex, 0, static_cast<int>(importedModel->animations.size()) - 1);
            animDuration = importedModel->animations[selectedAnimIndex].duration / std::max(importedModel->animations[selectedAnimIndex].ticksPerSecond, 0.001f);
        }
        if (ImGui::Button(animPlaying ? "Pause" : "Play")) {
            animPlaying = !animPlaying;
        }
        ImGui::SameLine();
        ImGui::SliderFloat("Time", &animTime, 0.0f, animDuration, "%.2f");
        if (importedModel && !importedModel->animations.empty()) {
            auto& anim = importedModel->animations[selectedAnimIndex];
            // Build a map from bone name to channel for fast lookup
            std::map<std::string, const render::ImportedModelData::AnimationChannel*> channelMap;
            for (const auto& ch : anim.channels) {
                channelMap[ch.boneName] = &ch;
            }
            // Resize boneTransforms if needed
            if (importedModel->boneTransforms.size() != importedModel->boneParentIndices.size())
                importedModel->boneTransforms.resize(importedModel->boneParentIndices.size(), glm::mat4(1.0f));
            // For each bone, compute interpolated transform
            for (size_t i = 0; i < importedModel->boneParentIndices.size(); ++i) {
                std::string boneName;
                if (i < importedModel->boneNames.size())
                    boneName = importedModel->boneNames[i];
                glm::vec3 pos(0.0f);
                glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
                glm::vec3 scale(1.0f);
                // Find channel for this bone
                auto it = channelMap.find(boneName);
                if (it != channelMap.end()) {
                    const auto& keyframes = it->second->keyframes;
                    // Find two keyframes to interpolate
                    if (!keyframes.empty()) {
                        const float t = animTime * anim.ticksPerSecond;
                        size_t k0 = 0, k1 = 0;
                        for (size_t k = 1; k < keyframes.size(); ++k) {
                            if (keyframes[k].time > t) { k1 = k; k0 = k - 1; break; }
                        }
                        if (k1 == 0) { k0 = 0; k1 = 0; }
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
        if (animPlaying) {
            animTime += ImGui::GetIO().DeltaTime;
            if (animTime > animDuration) animTime = 0.0f;
        }
    }

    if (importedModel.has_value()) {
        updateFloatingImportedModel(*importedModel, environmentSettings, renderer);
    }

    renderer.setViewportSize(viewportWidth, viewportHeight);
    renderer.render(viewControls);

    ImGui::Image(
        static_cast<ImTextureID>(static_cast<intptr_t>(renderer.viewportTexture())),
        viewportSize,
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f));

    const ImVec2 imgMin = ImGui::GetItemRectMin();
    const ImVec2 imgMax = ImGui::GetItemRectMax();
    const bool sceneImageHovered = ImGui::IsItemHovered();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    if (sceneImageHovered && io.MouseWheel != 0.0f) {
        const float zoomFactor = std::pow(0.85f, io.MouseWheel);
        viewControls.zoomTargetDistance = std::clamp(
            viewControls.zoomTargetDistance * zoomFactor,
            kMinCameraZoom,
            kMaxCameraZoom);
    }

    const float aspectRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
    const glm::vec3 cameraTarget(viewControls.panOffset.x, viewControls.panOffset.y, viewControls.orbitTargetZ);
    const glm::vec3 cameraPos(cameraTarget.x, cameraTarget.y, cameraTarget.z + viewControls.zoomDistance);
    const glm::mat4 view = glm::lookAt(
        cameraPos,
        cameraTarget,
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, kCameraFarPlane);

    glm::mat4 world(1.0f);
    world = glm::translate(world, cameraTarget);
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    world = glm::translate(world, -cameraTarget);
    const glm::vec3 viewRotateAxis = glm::normalize(glm::vec3(glm::inverse(world) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));

    auto computeImportedScreenBounds = [&](const render::ImportedModelData& modelData, ImVec2& outMin, ImVec2& outMax) {
        const glm::vec3 halfExtent = importedModelWorldDimensions(modelData) * 0.5f;
        if (halfExtent.x <= 0.0f || halfExtent.y <= 0.0f || halfExtent.z <= 0.0f) {
            return false;
        }

        static constexpr std::array<glm::vec3, 8> kLocalCorners = {
            glm::vec3(-1.0f, -1.0f, -1.0f),
            glm::vec3( 1.0f, -1.0f, -1.0f),
            glm::vec3( 1.0f,  1.0f, -1.0f),
            glm::vec3(-1.0f,  1.0f, -1.0f),
            glm::vec3(-1.0f, -1.0f,  1.0f),
            glm::vec3( 1.0f, -1.0f,  1.0f),
            glm::vec3( 1.0f,  1.0f,  1.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f),
        };

        glm::mat4 model(1.0f);
        model = glm::translate(model, modelData.position);
        const glm::vec3 rot = glm::radians(modelData.rotationEulerDegrees);
        model = glm::rotate(model, rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rot.z, glm::vec3(0.0f, 0.0f, 1.0f));

        const glm::mat4 mvp = projection * view * world * model;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        bool hasPoint = false;

        for (const glm::vec3& corner : kLocalCorners) {
            const glm::vec3 localCorner = corner * halfExtent;
            const glm::vec4 clip = mvp * glm::vec4(localCorner, 1.0f);
            if (clip.w <= 0.0f) {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float sx = imgMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
            const float sy = imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
            hasPoint = true;
        }

        if (!hasPoint) {
            return false;
        }

        outMin = ImVec2(minX, minY);
        outMax = ImVec2(maxX, maxY);
        return true;
    };

    auto computeCloudScreenBounds = [&](const render::CloudObjectSettings& cloud, ImVec2& outMin, ImVec2& outMax) {
        const glm::vec3 halfExtent = glm::abs(cloud.scale) * 0.5f;
        if (halfExtent.x <= 0.0f || halfExtent.y <= 0.0f || halfExtent.z <= 0.0f) {
            return false;
        }

        static constexpr std::array<glm::vec3, 8> kLocalCorners = {
            glm::vec3(-1.0f, -1.0f, -1.0f),
            glm::vec3( 1.0f, -1.0f, -1.0f),
            glm::vec3( 1.0f,  1.0f, -1.0f),
            glm::vec3(-1.0f,  1.0f, -1.0f),
            glm::vec3(-1.0f, -1.0f,  1.0f),
            glm::vec3( 1.0f, -1.0f,  1.0f),
            glm::vec3( 1.0f,  1.0f,  1.0f),
            glm::vec3(-1.0f,  1.0f,  1.0f),
        };

        glm::mat4 model(1.0f);
        model = glm::translate(model, cloud.position);
        const glm::vec3 rot = glm::radians(cloud.rotationEulerDegrees);
        model = glm::rotate(model, rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
        model = glm::rotate(model, rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, rot.z, glm::vec3(0.0f, 0.0f, 1.0f));

        const glm::mat4 mvp = projection * view * world * model;
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = -std::numeric_limits<float>::max();
        float maxY = -std::numeric_limits<float>::max();
        bool hasPoint = false;

        for (const glm::vec3& corner : kLocalCorners) {
            const glm::vec3 localCorner = corner * halfExtent;
            const glm::vec4 clip = mvp * glm::vec4(localCorner, 1.0f);
            if (clip.w <= 0.0f) {
                continue;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float sx = imgMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
            const float sy = imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;
            minX = std::min(minX, sx);
            minY = std::min(minY, sy);
            maxX = std::max(maxX, sx);
            maxY = std::max(maxY, sy);
            hasPoint = true;
        }

        if (!hasPoint) {
            return false;
        }

        outMin = ImVec2(minX, minY);
        outMax = ImVec2(maxX, maxY);
        return true;
    };

    auto projectPointToScreen = [&](const glm::vec3& worldPosition) {
        const glm::vec4 clip = projection * view * world * glm::vec4(worldPosition, 1.0f);
        if (clip.w <= 0.0f) {
            return ImVec2(-10000.0f, -10000.0f);
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        const float sx = imgMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        const float sy = imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y;
        return ImVec2(sx, sy);
    };

    auto axisDirection2D = [&](TransformAxis axis) {
        glm::vec3 axisVec(1.0f, 0.0f, 0.0f);
        if (axis == TransformAxis::Y) {
            axisVec = glm::vec3(0.0f, 1.0f, 0.0f);
        } else if (axis == TransformAxis::Z) {
            axisVec = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        const glm::vec3 rotated = glm::normalize(glm::vec3(world * glm::vec4(axisVec, 0.0f)));
        ImVec2 dir(rotated.x, -rotated.y);
        const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.0001f) {
            return ImVec2(1.0f, 0.0f);
        }
        return ImVec2(dir.x / len, dir.y / len);
    };

    auto distanceToSegmentSq = [](const ImVec2& p, const ImVec2& a, const ImVec2& b) {
        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float apx = p.x - a.x;
        const float apy = p.y - a.y;
        const float abLenSq = abx * abx + aby * aby;
        const float t = (abLenSq > 0.0f)
            ? std::clamp((apx * abx + apy * aby) / abLenSq, 0.0f, 1.0f)
            : 0.0f;
        const float cx = a.x + abx * t;
        const float cy = a.y + aby * t;
        const float dx = p.x - cx;
        const float dy = p.y - cy;
        return dx * dx + dy * dy;
    };

    auto screenToWorldAtDepth = [&](const ImVec2& screenPos, const float ndcDepth) {
        const float x = ((screenPos.x - imgMin.x) / viewportSize.x) * 2.0f - 1.0f;
        const float y = 1.0f - ((screenPos.y - imgMin.y) / viewportSize.y) * 2.0f;
        const glm::vec4 clip(x, y, ndcDepth, 1.0f);
        const glm::mat4 invMvp = glm::inverse(projection * view * world);
        const glm::vec4 worldPos = invMvp * clip;
        if (std::abs(worldPos.w) < 0.0001f) {
            return glm::vec3(0.0f);
        }
        return glm::vec3(worldPos) / worldPos.w;
    };

    int selectedCount = 0;
    glm::vec3 selectedCenter(0.0f);
    glm::vec3 boundMin(std::numeric_limits<float>::max());
    glm::vec3 boundMax(std::numeric_limits<float>::lowest());
    if (importedModelSelected && importedModel.has_value()) {
        selectedCenter += importedModel->position;
        const glm::vec3 halfExtent = importedModelWorldDimensions(*importedModel) * 0.5f;
        boundMin = glm::min(boundMin, importedModel->position - halfExtent);
        boundMax = glm::max(boundMax, importedModel->position + halfExtent);
        ++selectedCount;
    }
    for (const int cloudIdx : selectedCloudIndices) {
        if (cloudIdx >= 0 && cloudIdx < static_cast<int>(environmentSettings.cloudObjects.size())) {
            const auto& cloud = environmentSettings.cloudObjects[cloudIdx];
            selectedCenter += cloud.position;
            const glm::vec3 halfExtent = glm::abs(cloud.scale) * 0.5f;
            boundMin = glm::min(boundMin, cloud.position - halfExtent);
            boundMax = glm::max(boundMax, cloud.position + halfExtent);
            ++selectedCount;
        }
    }
    const bool hasSelection = selectedCount > 0;
    if (hasSelection) {
        selectedCenter /= static_cast<float>(selectedCount);
        selectedCenter = (boundMin + boundMax) * 0.5f;
    }

    const ImVec2 gizmoCenter = hasSelection ? projectPointToScreen(selectedCenter) : ImVec2(-10000.0f, -10000.0f);
    float selectedCenterNdcZ = 0.0f;
    if (hasSelection) {
        const glm::vec4 centerClip = projection * view * world * glm::vec4(selectedCenter, 1.0f);
        if (std::abs(centerClip.w) > 0.0001f) {
            selectedCenterNdcZ = centerClip.z / centerClip.w;
        }
    }
    const float gizmoLength = 64.0f;
    const ImVec2 xDir2D = axisDirection2D(TransformAxis::X);
    const ImVec2 yDir2D = axisDirection2D(TransformAxis::Y);
    const ImVec2 zDir2D = axisDirection2D(TransformAxis::Z);
    const ImVec2 gizmoXTip(gizmoCenter.x + xDir2D.x * gizmoLength, gizmoCenter.y + xDir2D.y * gizmoLength);
    const ImVec2 gizmoYTip(gizmoCenter.x + yDir2D.x * gizmoLength, gizmoCenter.y + yDir2D.y * gizmoLength);
    const ImVec2 gizmoZTip(gizmoCenter.x + zDir2D.x * gizmoLength, gizmoCenter.y + zDir2D.y * gizmoLength);
    auto ringRadiusForAxis = [&](TransformAxis axis) {
        if (axis == TransformAxis::X) {
            return 54.0f;
        }
        if (axis == TransformAxis::Y) {
            return 74.0f;
        }
        if (axis == TransformAxis::Z) {
            return 64.0f;
        }
        return 88.0f;
    };

    TransformAxis hoveredGizmoAxis = TransformAxis::None;
    if (sceneImageHovered && hasSelection) {
        float bestDistSq = 10.0f * 10.0f;
        if (transformMode == TransformMode::Rotate) {
            bestDistSq = 12.0f;
            const float dx = io.MousePos.x - gizmoCenter.x;
            const float dy = io.MousePos.y - gizmoCenter.y;
            const float mouseRadius = std::sqrt(dx * dx + dy * dy);

            const float ringDistX = std::abs(mouseRadius - ringRadiusForAxis(TransformAxis::X));
            if (ringDistX < bestDistSq) {
                bestDistSq = ringDistX;
                hoveredGizmoAxis = TransformAxis::X;
            }

            const float ringDistY = std::abs(mouseRadius - ringRadiusForAxis(TransformAxis::Y));
            if (ringDistY < bestDistSq) {
                bestDistSq = ringDistY;
                hoveredGizmoAxis = TransformAxis::Y;
            }

            const float ringDistZ = std::abs(mouseRadius - ringRadiusForAxis(TransformAxis::Z));
            if (ringDistZ < bestDistSq) {
                bestDistSq = ringDistZ;
                hoveredGizmoAxis = TransformAxis::Z;
            }

            const float ringDistView = std::abs(mouseRadius - ringRadiusForAxis(TransformAxis::View));
            if (ringDistView < bestDistSq) {
                bestDistSq = ringDistView;
                hoveredGizmoAxis = TransformAxis::View;
            }
        } else {
            const float dx = io.MousePos.x - gizmoCenter.x;
            const float dy = io.MousePos.y - gizmoCenter.y;
            if (dx * dx + dy * dy <= 14.0f * 14.0f) {
                hoveredGizmoAxis = TransformAxis::X;
                bestDistSq = 0.0f;
            }

            const float distX = distanceToSegmentSq(io.MousePos, gizmoCenter, gizmoXTip);
            if (distX < bestDistSq) {
                bestDistSq = distX;
                hoveredGizmoAxis = TransformAxis::X;
            }

            const float distY = distanceToSegmentSq(io.MousePos, gizmoCenter, gizmoYTip);
            if (distY < bestDistSq) {
                bestDistSq = distY;
                hoveredGizmoAxis = TransformAxis::Y;
            }

            const float distZ = distanceToSegmentSq(io.MousePos, gizmoCenter, gizmoZTip);
            if (distZ < bestDistSq) {
                hoveredGizmoAxis = TransformAxis::Z;
            }
        }
    }

    const ImVec2 modeButtonsMin(imgMax.x - 220.0f, imgMin.y);
    const ImVec2 modeButtonsMax(imgMax.x, imgMin.y + 40.0f);
    const bool clickInModeButtons = (io.MousePos.x >= modeButtonsMin.x && io.MousePos.x <= modeButtonsMax.x &&
                                     io.MousePos.y >= modeButtonsMin.y && io.MousePos.y <= modeButtonsMax.y);

    ImGui::SetCursorScreenPos(ImVec2(imgMax.x - 210.0f, imgMin.y + 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if (ImGui::SmallButton("Move")) {
        transformMode = TransformMode::Move;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Rotate")) {
        transformMode = TransformMode::Rotate;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Scale")) {
        transformMode = TransformMode::Scale;
    }
    ImGui::PopStyleVar();

    if (sceneImageHovered && hasSelection && !panModeEnabled && !clickInModeButtons && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        transformMode = TransformMode::Move;
        transformDragging = true;
        activeTransformAxis = TransformAxis::View;
        transformDragStartMouse = io.MousePos;
        transformDragPrevMouse = io.MousePos;
        transformRotateAccumDeg = 0.0f;
        transformDragStartWorldRotation = viewControls.worldRotationDegrees;
        transformDragStartSelectedCenter = selectedCenter;
        transformDragStartImportedModel = importedModel;
        transformDragStartClouds.clear();
        for (const int idx : selectedCloudIndices) {
            if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                transformDragStartClouds.push_back(environmentSettings.cloudObjects[idx]);
            } else {
                transformDragStartClouds.push_back(render::CloudObjectSettings{});
            }
        }
    }

    if (sceneImageHovered && !panModeEnabled && !clickInModeButtons && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hasSelection && hoveredGizmoAxis != TransformAxis::None) {
            transformDragging = true;
            activeTransformAxis = hoveredGizmoAxis;
            transformDragStartMouse = io.MousePos;
            transformDragPrevMouse = io.MousePos;
            transformRotateAccumDeg = 0.0f;
            transformDragStartWorldRotation = viewControls.worldRotationDegrees;
            transformDragStartSelectedCenter = selectedCenter;
            transformDragStartImportedModel = importedModel;
            transformDragStartClouds.clear();
            for (const int idx : selectedCloudIndices) {
                if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                    transformDragStartClouds.push_back(environmentSettings.cloudObjects[idx]);
                } else {
                    transformDragStartClouds.push_back(render::CloudObjectSettings{});
                }
            }
        } else {
            selectionDragging = true;
            selectionStart = io.MousePos;
            selectionEnd = io.MousePos;
        }
    }

    if (transformDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImVec2 axisDir = xDir2D;
        if (activeTransformAxis == TransformAxis::Y) {
            axisDir = yDir2D;
        } else if (activeTransformAxis == TransformAxis::Z) {
            axisDir = zDir2D;
        }

        const ImVec2 delta(io.MousePos.x - transformDragStartMouse.x, io.MousePos.y - transformDragStartMouse.y);
        const float dragAmount = delta.x * axisDir.x + delta.y * axisDir.y;

        float rotateAmountDeg = 0.0f;
        if (transformMode == TransformMode::Rotate) {
            const ImVec2 startVec(transformDragPrevMouse.x - gizmoCenter.x, transformDragPrevMouse.y - gizmoCenter.y);
            const ImVec2 curVec(io.MousePos.x - gizmoCenter.x, io.MousePos.y - gizmoCenter.y);
            const float startLen = std::sqrt(startVec.x * startVec.x + startVec.y * startVec.y);
            const float curLen = std::sqrt(curVec.x * curVec.x + curVec.y * curVec.y);
            if (startLen > 0.001f && curLen > 0.001f) {
                const ImVec2 s(startVec.x / startLen, startVec.y / startLen);
                const ImVec2 c(curVec.x / curLen, curVec.y / curLen);
                const float dot = std::clamp(s.x * c.x + s.y * c.y, -1.0f, 1.0f);
                const float cross = s.x * c.y - s.y * c.x;
                const float angleRad = std::atan2(cross, dot);
                transformRotateAccumDeg += -glm::degrees(angleRad) * 0.75f;
            }
            transformDragPrevMouse = io.MousePos;
            rotateAmountDeg = transformRotateAccumDeg;
        }

        if (importedModelSelected && importedModel.has_value() && transformDragStartImportedModel.has_value()) {
            auto& modelData = *importedModel;
            const auto& startModel = *transformDragStartImportedModel;

            if (transformMode == TransformMode::Move) {
                if (activeTransformAxis == TransformAxis::View) {
                    const glm::vec3 startWorld = screenToWorldAtDepth(transformDragStartMouse, selectedCenterNdcZ);
                    const glm::vec3 currentWorld = screenToWorldAtDepth(io.MousePos, selectedCenterNdcZ);
                    const glm::vec3 worldDelta = currentWorld - startWorld;
                    modelData.position = startModel.position + worldDelta;
                } else {
                    const float moveFactor = 0.004f * viewControls.zoomDistance;
                    modelData.position = startModel.position;
                    if (activeTransformAxis == TransformAxis::X) {
                        modelData.position.x = startModel.position.x + dragAmount * moveFactor;
                    } else if (activeTransformAxis == TransformAxis::Y) {
                        modelData.position.y = startModel.position.y + dragAmount * moveFactor;
                    } else if (activeTransformAxis == TransformAxis::Z) {
                        modelData.position.z = startModel.position.z + dragAmount * moveFactor;
                    }
                }
            } else if (transformMode == TransformMode::Rotate) {
                glm::vec3 rotationAxis(0.0f, 0.0f, 1.0f);
                if (activeTransformAxis == TransformAxis::X) {
                    rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                } else if (activeTransformAxis == TransformAxis::Y) {
                    rotationAxis = glm::vec3(0.0f, 0.0f, 1.0f);
                } else if (activeTransformAxis == TransformAxis::Z) {
                    rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                } else if (activeTransformAxis == TransformAxis::View) {
                    rotationAxis = viewRotateAxis;
                }

                const glm::quat startRotation = composeRotationXYZDegrees(startModel.rotationEulerDegrees);
                float appliedRotateDeg = rotateAmountDeg;
                if (activeTransformAxis == TransformAxis::X) {
                    appliedRotateDeg = -appliedRotateDeg;
                }
                if (activeTransformAxis == TransformAxis::Z) {
                    appliedRotateDeg = -appliedRotateDeg;
                }
                const glm::quat deltaRotation = glm::angleAxis(glm::radians(appliedRotateDeg), glm::normalize(rotationAxis));
                glm::quat finalRotation = deltaRotation * startRotation;
                if (activeTransformAxis != TransformAxis::View) {
                    finalRotation = startRotation * deltaRotation;
                }
                modelData.rotationEulerDegrees = eulerDegreesFromQuatXYZ(finalRotation);
            } else {
                const float scaleFactor = std::max(0.05f, 1.0f + dragAmount * 0.004f);
                modelData.scale = startModel.scale;
                if (activeTransformAxis == TransformAxis::X) {
                    modelData.scale.x = std::clamp(startModel.scale.x * scaleFactor, 0.001f, 1000.0f);
                } else if (activeTransformAxis == TransformAxis::Y) {
                    modelData.scale.y = std::clamp(startModel.scale.y * scaleFactor, 0.001f, 1000.0f);
                } else if (activeTransformAxis == TransformAxis::Z) {
                    modelData.scale.z = std::clamp(startModel.scale.z * scaleFactor, 0.001f, 1000.0f);
                }
            }

            renderer.setImportedModelTransform(modelData.position, modelData.rotationEulerDegrees, modelData.scale);
        } else if (!selectedCloudIndices.empty() && !transformDragStartClouds.empty()) {
            for (int i = 0; i < static_cast<int>(selectedCloudIndices.size()) && i < static_cast<int>(transformDragStartClouds.size()); ++i) {
                const int cloudIdx = selectedCloudIndices[i];
                if (cloudIdx < 0 || cloudIdx >= static_cast<int>(environmentSettings.cloudObjects.size())) {
                    continue;
                }
                auto& cloud = environmentSettings.cloudObjects[cloudIdx];
                const auto& startCloud = transformDragStartClouds[i];

                if (transformMode == TransformMode::Move) {
                    if (activeTransformAxis == TransformAxis::View) {
                        const glm::vec3 startWorld = screenToWorldAtDepth(transformDragStartMouse, selectedCenterNdcZ);
                        const glm::vec3 currentWorld = screenToWorldAtDepth(io.MousePos, selectedCenterNdcZ);
                        const glm::vec3 worldDelta = currentWorld - startWorld;
                        cloud.position = startCloud.position + worldDelta;
                    } else {
                        const float moveFactor = 0.004f * viewControls.zoomDistance;
                        cloud.position = startCloud.position;
                        if (activeTransformAxis == TransformAxis::X) {
                            cloud.position.x = startCloud.position.x + dragAmount * moveFactor;
                        } else if (activeTransformAxis == TransformAxis::Y) {
                            cloud.position.y = startCloud.position.y + dragAmount * moveFactor;
                        } else if (activeTransformAxis == TransformAxis::Z) {
                            cloud.position.z = startCloud.position.z + dragAmount * moveFactor;
                        }
                    }
                } else if (transformMode == TransformMode::Rotate) {
                    glm::vec3 rotationAxis(0.0f, 0.0f, 1.0f);
                    if (activeTransformAxis == TransformAxis::X) {
                        rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                    } else if (activeTransformAxis == TransformAxis::Y) {
                        rotationAxis = glm::vec3(0.0f, 0.0f, 1.0f);
                    } else if (activeTransformAxis == TransformAxis::Z) {
                        rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                    } else if (activeTransformAxis == TransformAxis::View) {
                        rotationAxis = viewRotateAxis;
                    }

                    const glm::quat startRotation = composeRotationXYZDegrees(startCloud.rotationEulerDegrees);
                    float appliedRotateDeg = rotateAmountDeg;
                    if (activeTransformAxis == TransformAxis::X) {
                        appliedRotateDeg = -appliedRotateDeg;
                    }
                    if (activeTransformAxis == TransformAxis::Z) {
                        appliedRotateDeg = -appliedRotateDeg;
                    }
                    const glm::quat deltaRotation = glm::angleAxis(glm::radians(appliedRotateDeg), glm::normalize(rotationAxis));
                    glm::quat finalRotation = deltaRotation * startRotation;
                    if (activeTransformAxis != TransformAxis::View) {
                        finalRotation = startRotation * deltaRotation;
                    }
                    cloud.rotationEulerDegrees = eulerDegreesFromQuatXYZ(finalRotation);
                } else {
                    const float scaleFactor = std::max(0.05f, 1.0f + dragAmount * 0.004f);
                    cloud.scale = startCloud.scale;
                    if (activeTransformAxis == TransformAxis::X) {
                        cloud.scale.x = std::clamp(startCloud.scale.x * scaleFactor, 0.05f, 1000.0f);
                    } else if (activeTransformAxis == TransformAxis::Y) {
                        cloud.scale.y = std::clamp(startCloud.scale.y * scaleFactor, 0.05f, 1000.0f);
                    } else if (activeTransformAxis == TransformAxis::Z) {
                        cloud.scale.z = std::clamp(startCloud.scale.z * scaleFactor, 0.05f, 1000.0f);
                    }
                }
            }

            renderer.setEnvironmentSettings(environmentSettings);
        }
    }

    if (transformDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        transformDragging = false;
        activeTransformAxis = TransformAxis::None;
    }

    if (selectionDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        selectionEnd = io.MousePos;
    }

    if (selectionDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        selectionDragging = false;

        const float minX = std::min(selectionStart.x, selectionEnd.x);
        const float minY = std::min(selectionStart.y, selectionEnd.y);
        const float maxX = std::max(selectionStart.x, selectionEnd.x);
        const float maxY = std::max(selectionStart.y, selectionEnd.y);
        const float width = maxX - minX;
        const float height = maxY - minY;

        static constexpr float kClickDragThreshold = 6.0f;
        if (width < kClickDragThreshold && height < kClickDragThreshold) {
            float bestArea = std::numeric_limits<float>::max();
            bool bestWasImported = false;
            int bestCloudIndex = -1;

            if (importedModel.has_value()) {
                ImVec2 boxMin(0.0f, 0.0f);
                ImVec2 boxMax(0.0f, 0.0f);
                if (computeImportedScreenBounds(*importedModel, boxMin, boxMax) &&
                    io.MousePos.x >= boxMin.x && io.MousePos.x <= boxMax.x &&
                    io.MousePos.y >= boxMin.y && io.MousePos.y <= boxMax.y) {
                    const float area = (boxMax.x - boxMin.x) * (boxMax.y - boxMin.y);
                    if (area < bestArea) {
                        bestArea = area;
                        bestWasImported = true;
                    }
                }
            }

            for (int cloudIndex = 0; cloudIndex < static_cast<int>(environmentSettings.cloudObjects.size()); ++cloudIndex) {
                ImVec2 boxMin(0.0f, 0.0f);
                ImVec2 boxMax(0.0f, 0.0f);
                if (computeCloudScreenBounds(environmentSettings.cloudObjects[cloudIndex], boxMin, boxMax) &&
                    io.MousePos.x >= boxMin.x && io.MousePos.x <= boxMax.x &&
                    io.MousePos.y >= boxMin.y && io.MousePos.y <= boxMax.y) {
                    const float area = (boxMax.x - boxMin.x) * (boxMax.y - boxMin.y);
                    if (area < bestArea) {
                        bestArea = area;
                        bestWasImported = false;
                        bestCloudIndex = cloudIndex;
                    }
                }
            }

            if (io.KeyCtrl && !bestWasImported && bestCloudIndex >= 0) {
                // Ctrl+click: toggle cloud membership without changing FBX selection
                const auto it = std::find(selectedCloudIndices.begin(), selectedCloudIndices.end(), bestCloudIndex);
                if (it != selectedCloudIndices.end()) {
                    selectedCloudIndices.erase(it);
                } else {
                    selectedCloudIndices.push_back(bestCloudIndex);
                    importedModelSelected = false;
                }
            } else {
                importedModelSelected = bestWasImported;
                selectedCloudIndices.clear();
                if (!bestWasImported && bestCloudIndex >= 0) {
                    selectedCloudIndices.push_back(bestCloudIndex);
                }
            }
        } else {
            if (!io.KeyCtrl) {
                importedModelSelected = false;
                selectedCloudIndices.clear();
            }

            if (importedModel.has_value()) {
                ImVec2 boxMin(0.0f, 0.0f);
                ImVec2 boxMax(0.0f, 0.0f);
                if (computeImportedScreenBounds(*importedModel, boxMin, boxMax)) {
                    const bool intersects = !(boxMax.x < minX || boxMin.x > maxX || boxMax.y < minY || boxMin.y > maxY);
                    if (intersects) {
                        importedModelSelected = true;
                    }
                }
            }

            for (int cloudIndex = 0; cloudIndex < static_cast<int>(environmentSettings.cloudObjects.size()); ++cloudIndex) {
                ImVec2 boxMin(0.0f, 0.0f);
                ImVec2 boxMax(0.0f, 0.0f);
                if (computeCloudScreenBounds(environmentSettings.cloudObjects[cloudIndex], boxMin, boxMax)) {
                    const bool intersects = !(boxMax.x < minX || boxMin.x > maxX || boxMax.y < minY || boxMin.y > maxY);
                    if (intersects) {
                        const auto it = std::find(selectedCloudIndices.begin(), selectedCloudIndices.end(), cloudIndex);
                        if (it == selectedCloudIndices.end()) {
                            selectedCloudIndices.push_back(cloudIndex);
                        }
                    }
                }
            }
        }
    }

    if (selectionDragging) {
        const ImVec2 rectMin(std::min(selectionStart.x, selectionEnd.x), std::min(selectionStart.y, selectionEnd.y));
        const ImVec2 rectMax(std::max(selectionStart.x, selectionEnd.x), std::max(selectionStart.y, selectionEnd.y));
        drawList->AddRectFilled(rectMin, rectMax, IM_COL32(80, 140, 220, 45));
        drawList->AddRect(rectMin, rectMax, IM_COL32(80, 170, 255, 220), 0.0f, 0, 1.5f);
    }

    if (hasSelection) {
        auto axisColor = [&](TransformAxis axis) {
            if (axis == TransformAxis::X) {
                return IM_COL32(235, 70, 70, 255);
            }
            if (axis == TransformAxis::Y) {
                return IM_COL32(75, 145, 240, 255);
            }
            return IM_COL32(90, 215, 90, 255);
        };

        auto isHighlighted = [&](TransformAxis axis) {
            return (hoveredGizmoAxis == axis) || (transformDragging && activeTransformAxis == axis);
        };

        const float centerRadius = 8.0f;
        drawList->AddCircleFilled(gizmoCenter, centerRadius, IM_COL32(240, 240, 245, 220), 24);

        const float thicknessX = isHighlighted(TransformAxis::X) ? 4.0f : 2.0f;
        const float thicknessY = isHighlighted(TransformAxis::Y) ? 4.0f : 2.0f;
        const float thicknessZ = isHighlighted(TransformAxis::Z) ? 4.0f : 2.0f;

        auto drawArrowHead = [&](const ImVec2& from, const ImVec2& to, ImU32 color) {
            ImVec2 d(to.x - from.x, to.y - from.y);
            const float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 0.001f) {
                return;
            }
            d.x /= len;
            d.y /= len;
            const ImVec2 n(-d.y, d.x);
            const float arrowLen = 10.0f;
            const float arrowWidth = 5.0f;
            const ImVec2 p0 = to;
            const ImVec2 p1(to.x - d.x * arrowLen + n.x * arrowWidth, to.y - d.y * arrowLen + n.y * arrowWidth);
            const ImVec2 p2(to.x - d.x * arrowLen - n.x * arrowWidth, to.y - d.y * arrowLen - n.y * arrowWidth);
            drawList->AddTriangleFilled(p0, p1, p2, color);
        };

        drawList->AddLine(gizmoCenter, gizmoXTip, axisColor(TransformAxis::X), thicknessX);
        drawList->AddLine(gizmoCenter, gizmoYTip, axisColor(TransformAxis::Y), thicknessY);
        drawList->AddLine(gizmoCenter, gizmoZTip, axisColor(TransformAxis::Z), thicknessZ);

        drawArrowHead(gizmoCenter, gizmoXTip, axisColor(TransformAxis::X));
        drawArrowHead(gizmoCenter, gizmoYTip, axisColor(TransformAxis::Y));
        drawArrowHead(gizmoCenter, gizmoZTip, axisColor(TransformAxis::Z));

        drawList->AddCircleFilled(gizmoXTip, isHighlighted(TransformAxis::X) ? 6.0f : 4.0f, axisColor(TransformAxis::X), 16);
        drawList->AddCircleFilled(gizmoYTip, isHighlighted(TransformAxis::Y) ? 6.0f : 4.0f, axisColor(TransformAxis::Y), 16);
        drawList->AddCircleFilled(gizmoZTip, isHighlighted(TransformAxis::Z) ? 6.0f : 4.0f, axisColor(TransformAxis::Z), 16);

        if (transformMode == TransformMode::Rotate) {
            auto ringColor = [&](TransformAxis axis) {
                if (axis == TransformAxis::X) {
                    return isHighlighted(axis) ? IM_COL32(235, 70, 70, 255) : IM_COL32(235, 70, 70, 160);
                }
                if (axis == TransformAxis::Y) {
                    return isHighlighted(axis) ? IM_COL32(75, 145, 240, 255) : IM_COL32(75, 145, 240, 160);
                }
                return isHighlighted(axis) ? IM_COL32(90, 215, 90, 255) : IM_COL32(90, 215, 90, 160);
            };

            auto drawRing = [&](TransformAxis axis) {
                const float thickness = isHighlighted(axis) ? 3.5f : 2.0f;
                drawList->AddCircle(gizmoCenter, ringRadiusForAxis(axis), ringColor(axis), 64, thickness);
            };

            drawRing(TransformAxis::X);
            drawRing(TransformAxis::Y);
            drawRing(TransformAxis::Z);

            drawList->AddCircle(
                gizmoCenter,
                ringRadiusForAxis(TransformAxis::View),
                isHighlighted(TransformAxis::View) ? IM_COL32(245, 245, 245, 230) : IM_COL32(245, 245, 245, 130),
                96,
                isHighlighted(TransformAxis::View) ? 3.5f : 2.0f);
        }
    }

    const ImVec2 axisCenter(imgMax.x - 42.0f, imgMin.y + 42.0f);

    glm::mat4 axisRotation(1.0f);
    axisRotation = glm::rotate(axisRotation, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    axisRotation = glm::rotate(axisRotation, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));

    auto axisDir2D = [&](const glm::vec3& axis) {
        const glm::vec3 v = glm::normalize(glm::vec3(axisRotation * glm::vec4(axis, 0.0f)));
        return ImVec2(v.x, -v.y);
    };

    auto axisDepth = [&](const glm::vec3& axis) {
        const glm::vec3 v = glm::normalize(glm::vec3(axisRotation * glm::vec4(axis, 0.0f)));
        return v.z;
    };

    drawList->AddCircleFilled(axisCenter, 24.0f, IM_COL32(25, 25, 30, 180), 32);

    const ImVec2 xDir = axisDir2D(glm::vec3(1.0f, 0.0f, 0.0f));
    const ImVec2 yDir = axisDir2D(glm::vec3(0.0f, 1.0f, 0.0f));
    const ImVec2 zDir = axisDir2D(glm::vec3(0.0f, 0.0f, 1.0f));

    const float xLen = 12.0f + 8.0f * (axisDepth(glm::vec3(1.0f, 0.0f, 0.0f)) + 1.0f) * 0.5f;
    const float yLen = 12.0f + 8.0f * (axisDepth(glm::vec3(0.0f, 1.0f, 0.0f)) + 1.0f) * 0.5f;
    const float zLen = 12.0f + 8.0f * (axisDepth(glm::vec3(0.0f, 0.0f, 1.0f)) + 1.0f) * 0.5f;

    const ImVec2 xTip(axisCenter.x + xDir.x * xLen, axisCenter.y + xDir.y * xLen);
    const ImVec2 yTip(axisCenter.x + yDir.x * yLen, axisCenter.y + yDir.y * yLen);
    const ImVec2 zTip(axisCenter.x + zDir.x * zLen, axisCenter.y + zDir.y * zLen);

    drawList->AddLine(axisCenter, xTip, IM_COL32(235, 70, 70, 255), 2.0f);
    drawList->AddLine(axisCenter, yTip, IM_COL32(75, 145, 240, 255), 2.0f);
    drawList->AddLine(axisCenter, zTip, IM_COL32(90, 215, 90, 255), 2.0f);

    auto drawMiniArrow = [&](const ImVec2& from, const ImVec2& to, ImU32 color) {
        ImVec2 d(to.x - from.x, to.y - from.y);
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len < 0.001f) {
            return;
        }
        d.x /= len;
        d.y /= len;
        const ImVec2 n(-d.y, d.x);
        const float arrowLen = 5.5f;
        const float arrowWidth = 3.0f;
        const ImVec2 p0 = to;
        const ImVec2 p1(to.x - d.x * arrowLen + n.x * arrowWidth, to.y - d.y * arrowLen + n.y * arrowWidth);
        const ImVec2 p2(to.x - d.x * arrowLen - n.x * arrowWidth, to.y - d.y * arrowLen - n.y * arrowWidth);
        drawList->AddTriangleFilled(p0, p1, p2, color);
    };

    drawMiniArrow(axisCenter, xTip, IM_COL32(235, 70, 70, 255));
    drawMiniArrow(axisCenter, yTip, IM_COL32(75, 145, 240, 255));
    drawMiniArrow(axisCenter, zTip, IM_COL32(90, 215, 90, 255));

    drawList->AddText(ImVec2(xTip.x + 3.0f, xTip.y - 8.0f), IM_COL32(235, 70, 70, 255), "X");
    drawList->AddText(ImVec2(yTip.x + 3.0f, yTip.y - 8.0f), IM_COL32(75, 145, 240, 255), "Z");
    drawList->AddText(ImVec2(zTip.x + 3.0f, zTip.y - 8.0f), IM_COL32(90, 215, 90, 255), "Y");

    ImGui::EndTabItem();
}

void drawRiggingTab(
    std::vector<RigBone>& rigBones,
    const std::vector<RigBone>& rigImportedSourceBones,
    std::vector<VertexGroupInfo>& rigVertexGroups,
    int& selectedVertexGroup,
    int& selectedRigBone,
    int& rigAvatarDefinition,
    int& rigAnimationType,
    bool& rigOptimizeGameObjects,
    bool& rigHasUnsavedChanges,
    int& rigRootBone,
    std::array<int, 15>& humanoidBoneMap,
    bool importedModelSelected,
    const std::optional<render::ImportedModelData>& importedModel) {
    if (!ImGui::BeginTabItem("Rigging")) {
        return;
    }

    ImGui::TextUnformatted("Rig Import Settings");
    ImGui::SameLine();
    ImGui::TextDisabled("(Humanoid)");
    ImGui::Separator();

    if (ImGui::Button("Apply")) {
        rigHasUnsavedChanges = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        rigBones = rigImportedSourceBones;
        selectedRigBone = 0;
        humanoidBoneMap.fill(-1);
        rigHasUnsavedChanges = false;
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(rigHasUnsavedChanges ? "Status: Modified" : "Status: Up to date");

    const char* avatarDefOptions[] = {"Create From This Model", "Copy From Other Avatar"};
    const char* animTypeOptions[] = {"Humanoid", "Generic", "Legacy"};

    if (ImGui::CollapsingHeader("Avatar Definition & Rig Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginChild("RigConfig", ImVec2(0.0f, 140.0f), true);
    if (ImGui::Combo("Animation Type", &rigAnimationType, animTypeOptions, IM_ARRAYSIZE(animTypeOptions))) {
        rigHasUnsavedChanges = true;
    }
    if (ImGui::Combo("Avatar Definition", &rigAvatarDefinition, avatarDefOptions, IM_ARRAYSIZE(avatarDefOptions))) {
        rigHasUnsavedChanges = true;
    }
    if (ImGui::Checkbox("Optimize Game Objects", &rigOptimizeGameObjects)) {
        rigHasUnsavedChanges = true;
    }
    if (!rigBones.empty()) {
        rigRootBone = std::clamp(rigRootBone, 0, static_cast<int>(rigBones.size()) - 1);
        if (ImGui::BeginCombo("Root Node", rigBones[static_cast<std::size_t>(rigRootBone)].name.c_str())) {
            for (int i = 0; i < static_cast<int>(rigBones.size()); ++i) {
                const bool selected = (rigRootBone == i);
                if (ImGui::Selectable(rigBones[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                    rigRootBone = i;
                    rigHasUnsavedChanges = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("Configure Avatar", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginChild("HumanoidMapping", ImVec2(0.0f, 260.0f), true);
    ImGui::TextUnformatted("Humanoid Bone Mapping");
    ImGui::SameLine();
    ImGui::TextDisabled("(Required + Optional)");
    ImGui::Separator();

    if (ImGui::Button("Auto-map")) {
        applyDefaultHumanoidMapping(humanoidBoneMap, rigBones);
        rigHasUnsavedChanges = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Enforce T-Pose")) {
        enforceHumanoidTPose(rigBones);
        rigHasUnsavedChanges = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        rigBones = rigImportedSourceBones;
        selectedRigBone = 0;
        rigRootBone = 0;
        humanoidBoneMap.fill(-1);
        rigHasUnsavedChanges = true;
    }

    for (int slot = 0; slot < static_cast<int>(kHumanoidSlotNames.size()); ++slot) {
        const int mapped = humanoidBoneMap[static_cast<std::size_t>(slot)];
        const bool mappedOk = mapped >= 0 && mapped < static_cast<int>(rigBones.size());
        ImGui::PushStyleColor(ImGuiCol_Text, mappedOk ? IM_COL32(132, 220, 132, 255) : IM_COL32(255, 170, 120, 255));
        ImGui::Text("%s", kHumanoidSlotNames[static_cast<std::size_t>(slot)]);
        ImGui::PopStyleColor();
        ImGui::SameLine(190.0f);

        std::string comboId = std::string("##HumanoidMap") + std::to_string(slot);
        const char* previewName = mappedOk ? rigBones[static_cast<std::size_t>(mapped)].name.c_str() : "<None>";
        if (ImGui::BeginCombo(comboId.c_str(), previewName)) {
            const bool noneSelected = mapped == -1;
            if (ImGui::Selectable("<None>", noneSelected)) {
                humanoidBoneMap[static_cast<std::size_t>(slot)] = -1;
                rigHasUnsavedChanges = true;
            }
            if (noneSelected) {
                ImGui::SetItemDefaultFocus();
            }

            for (int i = 0; i < static_cast<int>(rigBones.size()); ++i) {
                const bool selected = mapped == i;
                if (ImGui::Selectable(rigBones[static_cast<std::size_t>(i)].name.c_str(), selected)) {
                    humanoidBoneMap[static_cast<std::size_t>(slot)] = i;
                    rigHasUnsavedChanges = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("Skin Weights / Vertex Groups", ImGuiTreeNodeFlags_DefaultOpen)) {
    const float weightsPanelHeight = std::max(140.0f, ImGui::GetContentRegionAvail().y * 0.25f);
    ImGui::BeginChild("VertexGroups", ImVec2(0.0f, weightsPanelHeight), true);
    ImGui::Text("Imported Groups: %d", static_cast<int>(rigVertexGroups.size()));
    ImGui::Separator();

    if (rigVertexGroups.empty()) {
        ImGui::TextDisabled("No FBX vertex groups found in current import.");
    } else {
        selectedVertexGroup = std::clamp(selectedVertexGroup, 0, static_cast<int>(rigVertexGroups.size()) - 1);

        ImGui::BeginChild("VertexGroupList", ImVec2(280.0f, 0.0f), true);
        for (int i = 0; i < static_cast<int>(rigVertexGroups.size()); ++i) {
            const VertexGroupInfo& g = rigVertexGroups[static_cast<std::size_t>(i)];
            char label[256];
            std::snprintf(label, sizeof(label), "%s (%d)", g.name.c_str(), g.weightedVertexCount);
            if (ImGui::Selectable(label, selectedVertexGroup == i)) {
                selectedVertexGroup = i;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("VertexGroupDetails", ImVec2(0.0f, 0.0f), true);
        const VertexGroupInfo& g = rigVertexGroups[static_cast<std::size_t>(selectedVertexGroup)];
        ImGui::Text("Group: %s", g.name.c_str());
        ImGui::Separator();
        ImGui::Text("Weighted Vertices: %d", g.weightedVertexCount);
        ImGui::Text("Total Weight: %.4f", g.totalWeight);
        ImGui::Text("Max Weight: %.4f", g.maxWeight);
        if (g.weightedVertexCount > 0) {
            ImGui::Text("Average Weight: %.4f", g.totalWeight / static_cast<float>(g.weightedVertexCount));
        }
        ImGui::EndChild();
    }

    ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("Muscles & Bone Inspector", ImGuiTreeNodeFlags_DefaultOpen)) {
    const float rigEditorHeight = std::max(180.0f, ImGui::GetContentRegionAvail().y * 0.36f);
    ImGui::BeginChild("RigEditor", ImVec2(0.0f, rigEditorHeight), true);
    ImGui::BeginChild("RigHierarchy", ImVec2(220.0f, 0.0f), true);
    ImGui::TextUnformatted("Bone Hierarchy");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(rigBones.size()); ++i) {
        const bool isSelected = (selectedRigBone == i);
        if (ImGui::Selectable(rigBones[static_cast<std::size_t>(i)].name.c_str(), isSelected)) {
            selectedRigBone = i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("RigProperties", ImVec2(0.0f, 0.0f), true);
    if (!rigBones.empty()) {
        selectedRigBone = std::clamp(selectedRigBone, 0, static_cast<int>(rigBones.size()) - 1);
        RigBone& bone = rigBones[static_cast<std::size_t>(selectedRigBone)];
        ImGui::Text("Bone Inspector: %s", bone.name.c_str());
        ImGui::Separator();
        if (ImGui::DragFloat3("Local Position", &bone.localPosition.x, 0.005f, -1.0f, 1.0f, "%.3f")) {
            rigHasUnsavedChanges = true;
        }
        if (ImGui::DragFloat3("Local Rotation", &bone.localRotationDegrees.x, 0.5f, -180.0f, 180.0f, "%.1f deg")) {
            rigHasUnsavedChanges = true;
        }
        if (ImGui::DragFloat("Bone Length", &bone.length, 0.002f, 0.01f, 0.8f, "%.3f")) {
            rigHasUnsavedChanges = true;
        }
        if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(rigBones.size())) {
            ImGui::Text("Parent: %s", rigBones[static_cast<std::size_t>(bone.parentIndex)].name.c_str());
        } else {
            ImGui::TextUnformatted("Parent: <root>");
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("T-Pose Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::BeginChild("RigPreview", ImVec2(0.0f, 0.0f), true);
    ImGui::TextUnformatted("T-Pose Preview");
    ImGui::Separator();

    std::vector<glm::quat> worldRotations(rigBones.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    std::vector<glm::vec3> boneStarts(rigBones.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> boneEnds(rigBones.size(), glm::vec3(0.0f));

    for (std::size_t i = 0; i < rigBones.size(); ++i) {
        const RigBone& bone = rigBones[i];
        const glm::quat localRot = composeRotationXYZDegrees(bone.localRotationDegrees);
        const bool hasParent = bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(rigBones.size());

        if (hasParent) {
            const std::size_t parent = static_cast<std::size_t>(bone.parentIndex);
            boneStarts[i] = boneEnds[parent] + bone.localPosition;
            worldRotations[i] = glm::normalize(worldRotations[parent] * localRot);
        } else {
            boneStarts[i] = bone.localPosition;
            worldRotations[i] = glm::normalize(localRot);
        }

        boneEnds[i] = boneStarts[i] + worldRotations[i] * glm::vec3(0.0f, bone.length, 0.0f);
    }

    ImDrawList* rigDrawList = ImGui::GetWindowDrawList();
    const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const ImVec2 canvasMax(canvasMin.x + canvasSize.x, canvasMin.y + canvasSize.y);
    rigDrawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(18, 20, 26, 255));
    rigDrawList->AddRect(canvasMin, canvasMax, IM_COL32(62, 70, 84, 255), 0.0f, 0, 1.0f);

    if (!rigBones.empty() && canvasSize.x > 10.0f && canvasSize.y > 10.0f) {
        const bool showSelectedImported = importedModelSelected && importedModel.has_value();
        glm::vec3 selectedObjCenter(0.0f);
        glm::vec3 selectedObjMin(0.0f);
        glm::vec3 selectedObjMax(0.0f);
        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();

        for (std::size_t i = 0; i < rigBones.size(); ++i) {
            minX = std::min(minX, std::min(boneStarts[i].x, boneEnds[i].x));
            minY = std::min(minY, std::min(boneStarts[i].y, boneEnds[i].y));
            maxX = std::max(maxX, std::max(boneStarts[i].x, boneEnds[i].x));
            maxY = std::max(maxY, std::max(boneStarts[i].y, boneEnds[i].y));
        }

        if (showSelectedImported) {
            const glm::vec3 halfExtent = importedModelWorldDimensions(*importedModel) * 0.5f;
            const int clampedBoneIndex = std::clamp(selectedRigBone, 0, static_cast<int>(boneEnds.size()) - 1);
            selectedObjCenter = boneEnds[static_cast<std::size_t>(clampedBoneIndex)];
            selectedObjMin = selectedObjCenter - halfExtent;
            selectedObjMax = selectedObjCenter + halfExtent;
            minX = std::min(minX, selectedObjMin.x);
            minY = std::min(minY, selectedObjMin.y);
            maxX = std::max(maxX, selectedObjMax.x);
            maxY = std::max(maxY, selectedObjMax.y);
        }

        const float width = std::max(0.001f, maxX - minX);
        const float height = std::max(0.001f, maxY - minY);
        const float pad = 22.0f;
        const float sx = (canvasSize.x - pad * 2.0f) / width;
        const float sy = (canvasSize.y - pad * 2.0f) / height;
        const float scale = std::max(1.0f, std::min(sx, sy));

        auto toCanvas = [&](const glm::vec3& p) {
            const float nx = (p.x - minX) * scale + pad;
            const float ny = (p.y - minY) * scale + pad;
            return ImVec2(canvasMin.x + nx, canvasMax.y - ny);
        };

        if (showSelectedImported) {
            const ImVec2 objMin = toCanvas(glm::vec3(selectedObjMin.x, selectedObjMax.y, 0.0f));
            const ImVec2 objMax = toCanvas(glm::vec3(selectedObjMax.x, selectedObjMin.y, 0.0f));
            rigDrawList->AddRectFilled(objMin, objMax, IM_COL32(250, 220, 100, 28));
            rigDrawList->AddRect(objMin, objMax, IM_COL32(250, 220, 100, 220), 0.0f, 0, 1.5f);
            const int clampedBoneIndex = std::clamp(selectedRigBone, 0, static_cast<int>(rigBones.size()) - 1);
            std::string attachLabel = std::string("Selected FBX @ ") + rigBones[static_cast<std::size_t>(clampedBoneIndex)].name;
            rigDrawList->AddText(ImVec2(objMin.x + 6.0f, objMin.y + 4.0f), IM_COL32(255, 236, 160, 255), attachLabel.c_str());

            const glm::quat modelRotation = composeRotationXYZDegrees(importedModel->rotationEulerDegrees);
            const glm::vec3 worldForward = modelRotation * glm::vec3(0.0f, 0.0f, -1.0f);
            glm::vec2 front2D(worldForward.x, worldForward.y);
            float frontLen = std::sqrt(front2D.x * front2D.x + front2D.y * front2D.y);
            if (frontLen < 0.0001f) {
                front2D = glm::vec2(0.0f, 1.0f);
                frontLen = 1.0f;
            }
            front2D /= frontLen;

            const ImVec2 center((objMin.x + objMax.x) * 0.5f, (objMin.y + objMax.y) * 0.5f);
            const float markerLen = std::max(14.0f, std::min(objMax.x - objMin.x, objMax.y - objMin.y) * 0.35f);
            const ImVec2 frontTip(center.x + front2D.x * markerLen, center.y - front2D.y * markerLen);
            rigDrawList->AddLine(center, frontTip, IM_COL32(255, 140, 70, 255), 2.5f);

            const ImVec2 dir((frontTip.x - center.x) / markerLen, (frontTip.y - center.y) / markerLen);
            const ImVec2 normal(-dir.y, dir.x);
            const float arrowLen = 9.0f;
            const float arrowWidth = 4.5f;
            const ImVec2 p1(frontTip.x - dir.x * arrowLen + normal.x * arrowWidth, frontTip.y - dir.y * arrowLen + normal.y * arrowWidth);
            const ImVec2 p2(frontTip.x - dir.x * arrowLen - normal.x * arrowWidth, frontTip.y - dir.y * arrowLen - normal.y * arrowWidth);
            rigDrawList->AddTriangleFilled(frontTip, p1, p2, IM_COL32(255, 140, 70, 255));
            rigDrawList->AddText(ImVec2(frontTip.x + 6.0f, frontTip.y - 10.0f), IM_COL32(255, 190, 130, 255), "Front");
        }

        for (std::size_t i = 0; i < rigBones.size(); ++i) {
            const ImVec2 a = toCanvas(boneStarts[i]);
            const ImVec2 b = toCanvas(boneEnds[i]);
            const bool selected = static_cast<int>(i) == selectedRigBone;
            const bool isLeft = endsWith(rigBones[i].name, "_L");
            const bool isRight = endsWith(rigBones[i].name, "_R");
            ImU32 color = IM_COL32(230, 230, 230, 255);
            if (isLeft) {
                color = IM_COL32(108, 193, 255, 255);
            } else if (isRight) {
                color = IM_COL32(255, 168, 108, 255);
            }
            if (selected) {
                color = IM_COL32(255, 230, 120, 255);
            }

            rigDrawList->AddLine(a, b, color, selected ? 3.5f : 2.0f);
            rigDrawList->AddCircleFilled(a, selected ? 4.0f : 3.0f, IM_COL32(245, 245, 245, 245), 16);
            rigDrawList->AddCircleFilled(b, selected ? 4.0f : 3.0f, color, 16);
        }
    }

    ImGui::Dummy(canvasSize);
    ImGui::EndChild();
    }

    ImGui::EndTabItem();
}

void drawRightPane(
    const float contentHeight,
    ImGuiIO& io,
    render::Renderer& renderer,
    render::EnvironmentSettings& environmentSettings,
    std::vector<int>& selectedCloudIndices,
    std::optional<render::ImportedModelData>& importedModel,
    bool& importedModelSelected) {
    ImGui::BeginChild("RightPane", ImVec2(0.0f, contentHeight), false, ImGuiWindowFlags_NoScrollbar);
    static float hierarchyHeight = 170.0f;
    static std::optional<render::ImportedModelData> importedModelClipboard;
    static std::vector<render::CloudObjectSettings> cloudClipboard;
    hierarchyHeight = std::clamp(hierarchyHeight, 110.0f, contentHeight - 140.0f);

    auto copySelectedObjects = [&]() {
        importedModelClipboard.reset();
        cloudClipboard.clear();

        if (importedModelSelected && importedModel.has_value()) {
            importedModelClipboard = importedModel;
        }

        for (const int idx : selectedCloudIndices) {
            if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                cloudClipboard.push_back(environmentSettings.cloudObjects[idx]);
            }
        }
    };

    auto pasteObjects = [&]() {
        if (!importedModelClipboard.has_value() && cloudClipboard.empty()) {
            return;
        }

        bool pastedImported = false;
        bool pastedClouds = false;

        if (importedModelClipboard.has_value()) {
            importedModel = importedModelClipboard;
            if (importedModel.has_value()) {
                renderer.setImportedModel(*importedModel);
                renderer.setImportedModelTransform(importedModel->position, importedModel->rotationEulerDegrees, importedModel->scale);
            }
            pastedImported = true;
        }

        selectedCloudIndices.clear();
        for (std::size_t i = 0; i < cloudClipboard.size(); ++i) {
            auto pastedCloud = cloudClipboard[i];
            const float offset = 1.6f * static_cast<float>(i + 1);
            pastedCloud.position.x += offset;
            pastedCloud.position.z += offset;
            environmentSettings.cloudObjects.push_back(pastedCloud);
            selectedCloudIndices.push_back(static_cast<int>(environmentSettings.cloudObjects.size()) - 1);
            pastedClouds = true;
        }

        if (pastedClouds) {
            importedModelSelected = false;
            environmentSettings.enableCloudObjects = true;
            renderer.setEnvironmentSettings(environmentSettings);
        } else if (pastedImported) {
            importedModelSelected = true;
        }
    };

    const bool allowCloudHotkeys = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !io.WantTextInput;
    const bool hasHierarchySelection = (importedModelSelected && importedModel.has_value()) || !selectedCloudIndices.empty();
    const bool hasClipboard = importedModelClipboard.has_value() || !cloudClipboard.empty();
    if (allowCloudHotkeys && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && hasHierarchySelection) {
        copySelectedObjects();
    }
    if (allowCloudHotkeys && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && hasClipboard) {
        pasteObjects();
    }

    ImGui::BeginChild("HierarchyPane", ImVec2(0.0f, hierarchyHeight), true);
    ImGui::TextUnformatted("Hierarchy");
    ImGui::Separator();
    if (importedModel.has_value()) {
        if (ImGui::Selectable("Imported FBX", importedModelSelected)) {
            importedModelSelected = true;
            selectedCloudIndices.clear();
        }
    } else {
        ImGui::TextDisabled("No imported model");
    }

    for (int cloudIndex = 0; cloudIndex < static_cast<int>(environmentSettings.cloudObjects.size()); ++cloudIndex) {
        char cloudLabel[48] = {};
        std::snprintf(cloudLabel, sizeof(cloudLabel), "Cloud %d", cloudIndex + 1);
        const bool cloudSelected = std::find(selectedCloudIndices.begin(), selectedCloudIndices.end(), cloudIndex) != selectedCloudIndices.end();
        if (ImGui::Selectable(cloudLabel, cloudSelected)) {
            if (io.KeyCtrl) {
                const auto it = std::find(selectedCloudIndices.begin(), selectedCloudIndices.end(), cloudIndex);
                if (it != selectedCloudIndices.end()) {
                    selectedCloudIndices.erase(it);
                } else {
                    selectedCloudIndices.push_back(cloudIndex);
                    importedModelSelected = false;
                }
            } else {
                selectedCloudIndices = {cloudIndex};
                importedModelSelected = false;
            }
        }
    }

    if (environmentSettings.cloudObjects.empty()) {
        ImGui::TextDisabled("No cloud objects");
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!hasHierarchySelection);
    if (ImGui::Button("Copy")) {
        copySelectedObjects();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasClipboard);
    if (ImGui::Button("Paste")) {
        pasteObjects();
    }
    ImGui::EndDisabled();

    ImGui::EndChild();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.20f, 0.22f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.36f, 0.47f, 0.70f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.36f, 0.47f, 0.70f, 1.0f));
    ImGui::Button("##HierarchySplitter", ImVec2(-1.0f, 4.0f));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive()) {
        hierarchyHeight = std::clamp(hierarchyHeight + io.MouseDelta.y, 110.0f, contentHeight - 140.0f);
    }

    ImGui::BeginChild("PropertiesPane", ImVec2(0.0f, contentHeight), true);
    if (importedModelSelected && importedModel.has_value()) {
        ImGui::TextUnformatted("Imported FBX");
        ImGui::Separator();
        bool transformChanged = false;
        bool materialChanged = false;
        bool floatChanged = false;
        transformChanged |= ImGui::DragFloat3("Position", &importedModel->position.x, 0.01f, -1000.0f, 1000.0f);
        transformChanged |= ImGui::DragFloat3("Rotation", &importedModel->rotationEulerDegrees.x, 0.5f, -360.0f, 360.0f);
        transformChanged |= ImGui::DragFloat3("Scale (X/Y/Z)", &importedModel->scale.x, 0.01f, 0.001f, 1000.0f, "%.3f");
        ImGui::Text("Dimensions: %s", formatVec3(importedModelWorldDimensions(*importedModel)).c_str());
        ImGui::Separator();
        floatChanged |= ImGui::Checkbox("Float On Water", &importedModel->floatOnWater);
        ImGui::BeginDisabled(!importedModel->floatOnWater);
        floatChanged |= ImGui::SliderFloat("Float Height Offset", &importedModel->floatHeightOffset, -4.0f, 4.0f, "%.2f");
        floatChanged |= ImGui::SliderFloat("Float Bob Amplitude", &importedModel->floatBobAmplitude, 0.0f, 1.0f, "%.2f");
        floatChanged |= ImGui::SliderFloat("Float Bob Frequency", &importedModel->floatBobFrequency, 0.0f, 4.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::Separator();

        int shaderMode = importedModel->unlitShading ? 1 : 0;
        if (ImGui::Combo("Shader", &shaderMode, "Lit\0Unlit\0")) {
            importedModel->unlitShading = (shaderMode == 1);
            materialChanged = true;
        }

        materialChanged |= ImGui::SliderFloat("Opacity", &importedModel->opacity, 0.0f, 1.0f, "%.2f");
        materialChanged |= ImGui::SliderFloat("Alpha Cutoff", &importedModel->alphaCutoff, 0.0f, 1.0f, "%.3f");
        materialChanged |= ImGui::Checkbox("Alpha Blend", &importedModel->alphaBlend);
        materialChanged |= ImGui::ColorEdit4("Diffuse", &importedModel->diffuseColor.x);
        materialChanged |= ImGui::ColorEdit3("Emissive", &importedModel->emissiveColor.x);

        if (floatChanged) {
            importedModel->floatBobAmplitude = std::clamp(importedModel->floatBobAmplitude, 0.0f, 1.0f);
            importedModel->floatBobFrequency = std::clamp(importedModel->floatBobFrequency, 0.0f, 4.0f);
            updateFloatingImportedModel(*importedModel, environmentSettings, renderer);
        }
        if (transformChanged) {
            renderer.setImportedModelTransform(importedModel->position, importedModel->rotationEulerDegrees, importedModel->scale);
        }
        if (materialChanged) {
            importedModel->opacity = std::clamp(importedModel->opacity, 0.0f, 1.0f);
            importedModel->alphaCutoff = std::clamp(importedModel->alphaCutoff, 0.0f, 1.0f);
            renderer.setImportedModel(*importedModel);
        }
    } else if (selectedCloudIndices.size() == 1 &&
               selectedCloudIndices[0] >= 0 &&
               selectedCloudIndices[0] < static_cast<int>(environmentSettings.cloudObjects.size())) {
        const int selectedCloudIndex = selectedCloudIndices[0];
        auto& cloud = environmentSettings.cloudObjects[selectedCloudIndex];
        bool cloudChanged = false;

        char cloudLabel[48] = {};
        std::snprintf(cloudLabel, sizeof(cloudLabel), "Cloud %d", selectedCloudIndex + 1);
        ImGui::TextUnformatted(cloudLabel);
        ImGui::Separator();

        cloudChanged |= ImGui::Checkbox("Visible", &cloud.enabled);
        cloudChanged |= ImGui::DragFloat3("Position", &cloud.position.x, 0.05f, -200.0f, 200.0f, "%.2f");
        cloudChanged |= ImGui::DragFloat3("Rotation", &cloud.rotationEulerDegrees.x, 0.5f, -180.0f, 180.0f, "%.1f deg");
        cloudChanged |= ImGui::DragFloat3("Scale", &cloud.scale.x, 0.03f, 0.05f, 20.0f, "%.2f");
        int cloudType = cloud.cloudType;
        if (ImGui::Combo("Cloud Type", &cloudType, "Cumulus\0Stratus\0Cirrus\0Mares Tail\0")) {
            applyCloudTypePreset(cloud, cloudType);
            cloudChanged = true;
        }
        cloudChanged |= ImGui::ColorEdit3("Cloud Color", &cloud.color.x);
        cloudChanged |= ImGui::SliderFloat("Opacity", &cloud.opacity, 0.0f, 1.0f, "%.2f");
        cloudChanged |= ImGui::SliderFloat("Softness", &cloud.softness, 0.2f, 0.98f, "%.2f");
        cloudChanged |= ImGui::SliderFloat("Detail", &cloud.detail, 0.3f, 4.5f, "%.2f");
        cloudChanged |= ImGui::SliderFloat("Motion Speed", &cloud.motionSpeed, 0.0f, 8.0f, "%.2f");
        cloudChanged |= ImGui::SliderFloat("Glow", &cloud.glowStrength, 0.0f, 1.0f, "%.2f");
        cloudChanged |= ImGui::SliderFloat("Plane Fade", &cloud.planeFade, 0.0f, 1.0f, "%.2f");
        cloud.planeCount = 1;
        ImGui::TextDisabled("Plane Count: 1 (single-plane cloud mode)");
        cloudChanged |= ImGui::SliderFloat("Cube Spread", &cloud.cubeSpread, 0.35f, 2.50f, "%.2f");

        if (cloudChanged) {
            renderer.setEnvironmentSettings(environmentSettings);
        }
    } else if (selectedCloudIndices.size() > 1) {
        // Multi-cloud selection — use first selected cloud as shared reference
        int firstValid = -1;
        for (const int idx : selectedCloudIndices) {
            if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                firstValid = idx;
                break;
            }
        }
        if (firstValid >= 0) {
            char multiLabel[48] = {};
            std::snprintf(multiLabel, sizeof(multiLabel), "%d Clouds Selected", static_cast<int>(selectedCloudIndices.size()));
            ImGui::TextUnformatted(multiLabel);
            ImGui::Separator();

            auto& refCloud = environmentSettings.cloudObjects[firstValid];
            bool cloudChanged = false;

            // Appearance controls apply to all selected clouds
            int sharedType = refCloud.cloudType;
            if (ImGui::Combo("Cloud Type", &sharedType, "Cumulus\0Stratus\0Cirrus\0Mares Tail\0")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        applyCloudTypePreset(environmentSettings.cloudObjects[idx], sharedType);
                    }
                }
                cloudChanged = true;
            }
            if (ImGui::ColorEdit3("Cloud Color", &refCloud.color.x)) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].color = refCloud.color;
                    }
                }
                cloudChanged = true;
            }
            if (ImGui::SliderFloat("Opacity", &refCloud.opacity, 0.0f, 1.0f, "%.2f")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].opacity = refCloud.opacity;
                    }
                }
                cloudChanged = true;
            }
            if (ImGui::SliderFloat("Softness", &refCloud.softness, 0.2f, 0.98f, "%.2f")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].softness = refCloud.softness;
                    }
                }
                cloudChanged = true;
            }
            if (ImGui::SliderFloat("Detail", &refCloud.detail, 0.3f, 4.5f, "%.2f")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].detail = refCloud.detail;
                    }
                }
                cloudChanged = true;
            }
            if (ImGui::SliderFloat("Motion Speed", &refCloud.motionSpeed, 0.0f, 8.0f, "%.2f")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].motionSpeed = refCloud.motionSpeed;
                    }
                }
                cloudChanged = true;
            }
            if (ImGui::SliderFloat("Glow", &refCloud.glowStrength, 0.0f, 1.0f, "%.2f")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].glowStrength = refCloud.glowStrength;
                    }
                }
                cloudChanged = true;
            }
            if (ImGui::SliderFloat("Plane Fade", &refCloud.planeFade, 0.0f, 1.0f, "%.2f")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].planeFade = refCloud.planeFade;
                    }
                }
                cloudChanged = true;
            }
            refCloud.planeCount = 1;
            ImGui::TextDisabled("Plane Count: 1 (single-plane cloud mode)");
            if (ImGui::SliderFloat("Cube Spread", &refCloud.cubeSpread, 0.35f, 2.50f, "%.2f")) {
                for (const int idx : selectedCloudIndices) {
                    if (idx >= 0 && idx < static_cast<int>(environmentSettings.cloudObjects.size())) {
                        environmentSettings.cloudObjects[idx].cubeSpread = refCloud.cubeSpread;
                    }
                }
                cloudChanged = true;
            }

            if (cloudChanged) {
                renderer.setEnvironmentSettings(environmentSettings);
            }
        }
    } else {
        ImGui::TextDisabled("Select an imported FBX or cloud to edit properties.");
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

}  // namespace sparks::core::ui
