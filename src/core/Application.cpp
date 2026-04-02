#include "sparks/core/Application.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#define GLM_ENABLE_EXPERIMENTAL
#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "sparks/core/CubeProperties.hpp"
#include "sparks/render/Renderer.hpp"
#include "sparks/ui/PropertyPanel.hpp"

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

    std::array<sparks::core::CubeProperties, 2> sceneObjects{};
    sceneObjects[0].position = glm::vec3(-0.9f, 0.0f, 0.0f);
    sceneObjects[1].position = glm::vec3(0.9f, 0.0f, -0.8f);
    sceneObjects[1].baseColor = glm::vec3(0.93f, 0.55f, 0.24f);
    sceneObjects[1].rotationEulerDegrees = glm::vec3(12.0f, -24.0f, 0.0f);
    sceneObjects[1].scale = glm::vec3(0.85f, 0.85f, 0.85f);

    std::array<bool, 2> selectedObjects{false, false};

    sparks::render::Renderer renderer;
    sparks::ui::PropertyPanel propertyPanel;
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
    std::array<sparks::core::CubeProperties, 2> transformDragStartObjects{};
    bool selectionDragging = false;
    ImVec2 selectionStart(0.0f, 0.0f);
    ImVec2 selectionEnd(0.0f, 0.0f);

    renderer.initialize();

    while (!glfwWindowShouldClose(m_window)) {
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
            if (ImGui::BeginMenu("Camera")) {
                ImGui::SliderFloat("Zoom", &viewControls.zoomDistance, 1.5f, 20.0f, "%.2f");
                ImGui::DragFloat2("Pan", &viewControls.panOffset.x, 0.01f, -10.0f, 10.0f, "%.2f");
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
            ImGui::EndMenuBar();
        }

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
                if (ImGui::BeginTabItem("Scene")) {
                    const bool viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

                    if (viewportHovered && io.MouseWheel != 0.0f) {
                        viewControls.zoomDistance = std::clamp(viewControls.zoomDistance - io.MouseWheel * 0.35f, 1.5f, 20.0f);
                    }

                    if (viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        viewControls.worldRotationDegrees.x = std::clamp(
                            viewControls.worldRotationDegrees.x + io.MouseDelta.y * 0.25f,
                            -89.0f,
                            89.0f);
                        viewControls.worldRotationDegrees.y += io.MouseDelta.x * 0.25f;
                    }

                    if (panModeEnabled && viewportHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        const float panSpeed = 0.004f * viewControls.zoomDistance;
                        viewControls.panOffset.x -= io.MouseDelta.x * panSpeed;
                        viewControls.panOffset.y += io.MouseDelta.y * panSpeed;
                    }

                    const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                    const int viewportWidth = static_cast<int>(viewportSize.x > 1.0f ? viewportSize.x : 1.0f);
                    const int viewportHeight = static_cast<int>(viewportSize.y > 1.0f ? viewportSize.y : 1.0f);

                    renderer.setViewportSize(viewportWidth, viewportHeight);
                    renderer.render(sceneObjects, selectedObjects, viewControls);

                    ImGui::Image(
                        static_cast<ImTextureID>(static_cast<intptr_t>(renderer.viewportTexture())),
                        viewportSize,
                        ImVec2(0.0f, 1.0f),
                        ImVec2(1.0f, 0.0f));

                    const ImVec2 imgMin = ImGui::GetItemRectMin();
                    const ImVec2 imgMax = ImGui::GetItemRectMax();
                    const bool sceneImageHovered = ImGui::IsItemHovered();
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    const float aspectRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
                    const glm::vec3 cameraTarget(viewControls.panOffset.x, viewControls.panOffset.y, 0.0f);
                    const glm::vec3 cameraPos(cameraTarget.x, cameraTarget.y, viewControls.zoomDistance);
                    const glm::mat4 view = glm::lookAt(
                        cameraPos,
                        cameraTarget,
                        glm::vec3(0.0f, 1.0f, 0.0f));
                    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), aspectRatio, 0.1f, 100.0f);

                    glm::mat4 world(1.0f);
                    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
                    world = glm::rotate(world, glm::radians(viewControls.worldRotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
                    const glm::vec3 viewRotateAxis = glm::normalize(glm::vec3(glm::inverse(world) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));

                    auto computeObjectScreenBounds = [&](const sparks::core::CubeProperties& props, ImVec2& outMin, ImVec2& outMax) {
                        static constexpr std::array<glm::vec3, 8> kLocalCorners = {
                            glm::vec3(-0.5f, -0.5f, -0.5f),
                            glm::vec3( 0.5f, -0.5f, -0.5f),
                            glm::vec3( 0.5f,  0.5f, -0.5f),
                            glm::vec3(-0.5f,  0.5f, -0.5f),
                            glm::vec3(-0.5f, -0.5f,  0.5f),
                            glm::vec3( 0.5f, -0.5f,  0.5f),
                            glm::vec3( 0.5f,  0.5f,  0.5f),
                            glm::vec3(-0.5f,  0.5f,  0.5f),
                        };

                        glm::mat4 model(1.0f);
                        model = glm::translate(model, props.position);
                        const glm::vec3 rot = glm::radians(props.rotationEulerDegrees);
                        model = glm::rotate(model, rot.x, glm::vec3(1.0f, 0.0f, 0.0f));
                        model = glm::rotate(model, rot.y, glm::vec3(0.0f, 1.0f, 0.0f));
                        model = glm::rotate(model, rot.z, glm::vec3(0.0f, 0.0f, 1.0f));
                        model = glm::scale(model, glm::vec3(props.scale));

                        const glm::mat4 mvp = projection * view * world * model;
                        float minX = std::numeric_limits<float>::max();
                        float minY = std::numeric_limits<float>::max();
                        float maxX = -std::numeric_limits<float>::max();
                        float maxY = -std::numeric_limits<float>::max();
                        bool hasPoint = false;

                        for (const glm::vec3& corner : kLocalCorners) {
                            const glm::vec4 clip = mvp * glm::vec4(corner, 1.0f);
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
                    for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
                        if (selectedObjects[static_cast<std::size_t>(i)]) {
                            const auto& obj = sceneObjects[static_cast<std::size_t>(i)];
                            selectedCenter += obj.position;
                            const glm::vec3 extent = obj.scale; // Cube half-extent per axis
                            boundMin = glm::min(boundMin, obj.position - extent);
                            boundMax = glm::max(boundMax, obj.position + extent);
                            ++selectedCount;
                        }
                    }
                    const bool hasSelection = selectedCount > 0;
                    if (hasSelection) {
                        selectedCenter /= static_cast<float>(selectedCount);
                        selectedCenter = (boundMin + boundMax) * 0.5f; // Use actual bounding box center
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
                            return 64.0f;
                        }
                        if (axis == TransformAxis::Z) {
                            return 74.0f;
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
                        transformDragStartObjects = sceneObjects;
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
                                transformDragStartObjects = sceneObjects;
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

                        for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
                            if (!selectedObjects[static_cast<std::size_t>(i)]) {
                                continue;
                            }

                            const auto& startObj = transformDragStartObjects[static_cast<std::size_t>(i)];
                            auto& obj = sceneObjects[static_cast<std::size_t>(i)];

                            if (transformMode == TransformMode::Move) {
                                if (activeTransformAxis == TransformAxis::View) {
                                    const glm::vec3 startWorld = screenToWorldAtDepth(transformDragStartMouse, selectedCenterNdcZ);
                                    const glm::vec3 currentWorld = screenToWorldAtDepth(io.MousePos, selectedCenterNdcZ);
                                    const glm::vec3 worldDelta = currentWorld - startWorld;
                                    obj.position = startObj.position + worldDelta;
                                    continue;
                                }

                                const float moveFactor = 0.004f * viewControls.zoomDistance;
                                if (activeTransformAxis == TransformAxis::X) {
                                    obj.position.x = startObj.position.x + dragAmount * moveFactor;
                                } else if (activeTransformAxis == TransformAxis::Y) {
                                    obj.position.y = startObj.position.y + dragAmount * moveFactor;
                                } else if (activeTransformAxis == TransformAxis::Z) {
                                    obj.position.z = startObj.position.z + dragAmount * moveFactor;
                                }
                            } else if (transformMode == TransformMode::Rotate) {
                                glm::vec3 rotationAxis(0.0f, 0.0f, 1.0f);
                                if (activeTransformAxis == TransformAxis::X) {
                                    rotationAxis = glm::vec3(1.0f, 0.0f, 0.0f);
                                } else if (activeTransformAxis == TransformAxis::Y) {
                                    rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f);
                                } else if (activeTransformAxis == TransformAxis::Z) {
                                    rotationAxis = glm::vec3(0.0f, 0.0f, 1.0f);
                                } else if (activeTransformAxis == TransformAxis::View) {
                                    rotationAxis = viewRotateAxis;
                                }

                                const float angleRadians = glm::radians(rotateAmountDeg);
                                const glm::quat rotation = glm::angleAxis(angleRadians, rotationAxis);

                                // Rotate the selection boundary box and move each object with it.
                                const glm::vec3 relativePos = startObj.position - transformDragStartSelectedCenter;
                                const glm::vec3 rotatedRelPos = rotation * relativePos;
                                obj.position = transformDragStartSelectedCenter + rotatedRelPos;

                                // Keep each object's local orientation fixed while rotating the boundary box.
                                obj.rotationEulerDegrees = startObj.rotationEulerDegrees;
                            } else {
                                const float scaleFactor = 1.0f + dragAmount * 0.004f;
                                // Blender-like: scale both object size and pivot-relative layout.
                                const glm::vec3 startRel = startObj.position - transformDragStartSelectedCenter;
                                glm::vec3 scaledRel = startRel;
                                glm::vec3 nextScale = startObj.scale;
                                if (activeTransformAxis == TransformAxis::X) {
                                    scaledRel.x = startRel.x * scaleFactor;
                                    nextScale.x = std::clamp(startObj.scale.x * scaleFactor, 0.1f, 5.0f);
                                } else if (activeTransformAxis == TransformAxis::Y) {
                                    scaledRel.y = startRel.y * scaleFactor;
                                    nextScale.y = std::clamp(startObj.scale.y * scaleFactor, 0.1f, 5.0f);
                                } else if (activeTransformAxis == TransformAxis::Z) {
                                    scaledRel.z = startRel.z * scaleFactor;
                                    nextScale.z = std::clamp(startObj.scale.z * scaleFactor, 0.1f, 5.0f);
                                }
                                obj.position = transformDragStartSelectedCenter + scaledRel;
                                obj.scale = nextScale;
                            }
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
                            // Click: select single object or deselect all
                            int bestIndex = -1;
                            float bestArea = std::numeric_limits<float>::max();

                            for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
                                ImVec2 boxMin(0.0f, 0.0f);
                                ImVec2 boxMax(0.0f, 0.0f);
                                if (!computeObjectScreenBounds(sceneObjects[static_cast<std::size_t>(i)], boxMin, boxMax)) {
                                    continue;
                                }

                                if (io.MousePos.x >= boxMin.x && io.MousePos.x <= boxMax.x && io.MousePos.y >= boxMin.y && io.MousePos.y <= boxMax.y) {
                                    const float area = (boxMax.x - boxMin.x) * (boxMax.y - boxMin.y);
                                    if (area < bestArea) {
                                        bestArea = area;
                                        bestIndex = i;
                                    }
                                }
                            }

                            // Deselect all, then select closest object if found
                            std::fill(selectedObjects.begin(), selectedObjects.end(), false);
                            if (bestIndex >= 0) {
                                selectedObjects[static_cast<std::size_t>(bestIndex)] = true;
                            }
                        } else {
                            // Box select: deselect all first, then select objects within box
                            std::fill(selectedObjects.begin(), selectedObjects.end(), false);
                            for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
                                ImVec2 boxMin(0.0f, 0.0f);
                                ImVec2 boxMax(0.0f, 0.0f);
                                if (!computeObjectScreenBounds(sceneObjects[static_cast<std::size_t>(i)], boxMin, boxMax)) {
                                    continue;
                                }

                                const bool intersects = !(boxMax.x < minX || boxMin.x > maxX || boxMax.y < minY || boxMin.y > maxY);
                                if (intersects) {
                                    selectedObjects[static_cast<std::size_t>(i)] = true;
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

        ImGui::BeginChild("RightPane", ImVec2(0.0f, contentHeight), false, ImGuiWindowFlags_NoScrollbar);
        static float hierarchyHeight = 170.0f;
        hierarchyHeight = std::clamp(hierarchyHeight, 110.0f, contentHeight - 140.0f);

        ImGui::BeginChild("HierarchyPane", ImVec2(0.0f, hierarchyHeight), true);
        ImGui::TextUnformatted("Hierarchy");
        ImGui::Separator();
        for (int i = 0; i < static_cast<int>(sceneObjects.size()); ++i) {
            char label[48];
            std::snprintf(label, sizeof(label), "Cube %d", i + 1);
            const bool isSelected = selectedObjects[static_cast<std::size_t>(i)];
            if (ImGui::Selectable(label, isSelected)) {
                if (!io.KeyCtrl) {
                    std::fill(selectedObjects.begin(), selectedObjects.end(), false);
                    selectedObjects[static_cast<std::size_t>(i)] = true;
                } else {
                    selectedObjects[static_cast<std::size_t>(i)] = !selectedObjects[static_cast<std::size_t>(i)];
                }
            }
        }
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
        propertyPanel.draw(sceneObjects, selectedObjects);
        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("StatusBar", ImVec2(0.0f, statusBarHeight), false, ImGuiWindowFlags_NoScrollbar);
        const char* modeLabel = (transformMode == TransformMode::Move)
            ? "Move"
            : (transformMode == TransformMode::Rotate ? "Rotate" : "Scale");
        ImGui::Text(
            "Tips: LMB Click=Select | LMB Drag=Box Multi-select | Drag Selected=Transform (%s) | W/E/R=Mode | Scroll=Zoom | RMB Drag=Rotate World | Ctrl+P=Pan Mode (%s)",
            modeLabel,
            panModeEnabled ? "ON" : "OFF");
        ImGui::EndChild();

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

    m_window = glfwCreateWindow(1600, 900, "SparksEngine - Cube Editor", nullptr, nullptr);
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
