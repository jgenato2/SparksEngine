#include "sparks/core/Application.hpp"

#include <algorithm>
#include <array>
#include <optional>
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
                ImGui::SliderFloat("Zoom", &viewControls.zoomDistance, kMinCameraZoom, kMaxCameraZoom, "%.2f");
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
                ui::drawSceneTab(
                    renderer,
                    io,
                    viewControls,
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

        ui::drawRightPane(contentHeight, io, renderer, importedModel, importedModelSelected);

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