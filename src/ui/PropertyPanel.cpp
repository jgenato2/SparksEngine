#include "sparks/ui/PropertyPanel.hpp"

#include <algorithm>

#include <imgui.h>

namespace sparks::ui {

void PropertyPanel::draw(std::span<sparks::core::CubeProperties> objects, std::span<bool> selectedObjects) const {
    if (objects.empty() || selectedObjects.size() != objects.size()) {
        ImGui::TextUnformatted("No scene objects available.");
        return;
    }

    int firstSelectedIndex = -1;
    for (int i = 0; i < static_cast<int>(selectedObjects.size()); ++i) {
        if (selectedObjects[static_cast<std::size_t>(i)]) {
            firstSelectedIndex = i;
            break;
        }
    }

    if (firstSelectedIndex < 0) {
        selectedObjects[0] = true;
        firstSelectedIndex = 0;
    }

    if (ImGui::BeginTabBar("PropertyTabs")) {
        if (ImGui::BeginTabItem("Selection")) {
            ImGui::TextUnformatted("Objects");
            ImGui::Separator();

            for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
                const bool isSelected = selectedObjects[static_cast<std::size_t>(i)];
                const char* label = (i == 0) ? "Cube A" : "Cube B";
                if (ImGui::Selectable(label, isSelected)) {
                    std::fill(selectedObjects.begin(), selectedObjects.end(), false);
                    selectedObjects[static_cast<std::size_t>(i)] = true;
                }
            }

            ImGui::TextUnformatted("Tip: Drag in Scene viewport for box multi-select.");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cube")) {
            sparks::core::CubeProperties editable = objects[static_cast<std::size_t>(firstSelectedIndex)];

            ImGui::TextUnformatted("Appearance");
            ImGui::Separator();
            ImGui::ColorEdit3("Base Color", &editable.baseColor.x);
            ImGui::ColorEdit3("Background", &editable.backgroundColor.x);

            ImGui::Spacing();
            ImGui::TextUnformatted("Transform");
            ImGui::Separator();
            ImGui::DragFloat3("Position", &editable.position.x, 0.01f, -5.0f, 5.0f);
            ImGui::DragFloat3("Rotation", &editable.rotationEulerDegrees.x, 0.5f, -180.0f, 180.0f);
            ImGui::DragFloat("Scale", &editable.scale, 0.01f, 0.1f, 3.0f, "%.2f");

            ImGui::Spacing();
            ImGui::TextUnformatted("Render");
            ImGui::Separator();
            ImGui::Checkbox("Wireframe", &editable.wireframe);

            if (ImGui::Button("Reset Defaults")) {
                editable = sparks::core::CubeProperties{};
            }

            for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
                if (selectedObjects[static_cast<std::size_t>(i)]) {
                    objects[static_cast<std::size_t>(i)] = editable;
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

}  // namespace sparks::ui
