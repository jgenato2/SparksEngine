#include "sparks/core/Application.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
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
    std::array<sparks::core::CubeProperties, 2> sceneObjects{};
    sceneObjects[0].position = glm::vec3(-0.9f, 0.0f, 0.0f);
    sceneObjects[1].position = glm::vec3(0.9f, 0.0f, -0.8f);
    sceneObjects[1].baseColor = glm::vec3(0.93f, 0.55f, 0.24f);
    sceneObjects[1].rotationEulerDegrees = glm::vec3(12.0f, -24.0f, 0.0f);
    sceneObjects[1].scale = 0.85f;

    std::array<bool, 2> selectedObjects{true, false};

    sparks::render::Renderer renderer;
    sparks::ui::PropertyPanel propertyPanel;
    sparks::render::ViewControls viewControls;
    bool panModeEnabled = false;
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
            ImGui::EndMenuBar();
        }

        const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
        const float statusBarHeight = 30.0f;
        const float contentHeight = (workspaceSize.y > statusBarHeight + 6.0f)
            ? (workspaceSize.y - statusBarHeight - 6.0f)
            : workspaceSize.y;
        const float leftPaneWidth = workspaceSize.x * 0.72f;

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

                    if (sceneImageHovered && !panModeEnabled && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        selectionDragging = true;
                        selectionStart = io.MousePos;
                        selectionEnd = io.MousePos;
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

                            std::fill(selectedObjects.begin(), selectedObjects.end(), false);
                            if (bestIndex >= 0) {
                                selectedObjects[static_cast<std::size_t>(bestIndex)] = true;
                            }
                        } else {
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
                    drawList->AddLine(axisCenter, yTip, IM_COL32(90, 215, 90, 255), 2.0f);
                    drawList->AddLine(axisCenter, zTip, IM_COL32(75, 145, 240, 255), 2.0f);

                    drawList->AddText(ImVec2(xTip.x + 3.0f, xTip.y - 8.0f), IM_COL32(235, 70, 70, 255), "X");
                    drawList->AddText(ImVec2(yTip.x + 3.0f, yTip.y - 8.0f), IM_COL32(90, 215, 90, 255), "Y");
                    drawList->AddText(ImVec2(zTip.x + 3.0f, zTip.y - 8.0f), IM_COL32(75, 145, 240, 255), "Z");

                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("PropertiesPane", ImVec2(0.0f, contentHeight), true);
        propertyPanel.draw(sceneObjects, selectedObjects);
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginChild("StatusBar", ImVec2(0.0f, statusBarHeight), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::Text(
            "Tips: LMB Click=Select | LMB Drag=Box Multi-select | Scroll=Zoom | RMB Drag=Rotate World | Ctrl+P=Pan Mode (%s)",
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
