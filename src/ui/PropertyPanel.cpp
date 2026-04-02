#include "sparks/ui/PropertyPanel.hpp"

#include <algorithm>

#include <imgui.h>
#include <glm/common.hpp>

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
        ImGui::TextUnformatted("No object selected.");
        return;
    }

    if (ImGui::BeginTabBar("PropertyTabs")) {
        if (ImGui::BeginTabItem("Object")) {
            const sparks::core::CubeProperties baseline = objects[static_cast<std::size_t>(firstSelectedIndex)];
            sparks::core::CubeProperties editable = baseline;

            int selectedCount = 0;
            for (bool selected : selectedObjects) {
                selectedCount += selected ? 1 : 0;
            }
            ImGui::Text("Selected: %d", selectedCount);
            ImGui::Separator();

            ImGui::TextUnformatted("Appearance");
            ImGui::Separator();
            const bool baseColorChanged = ImGui::ColorEdit3("Base Color", &editable.baseColor.x);
            const bool backgroundChanged = ImGui::ColorEdit3("Background", &editable.backgroundColor.x);

            ImGui::Spacing();
            ImGui::TextUnformatted("Transform");
            ImGui::Separator();
            const bool positionChanged = ImGui::DragFloat3("Position", &editable.position.x, 0.01f, -5.0f, 5.0f);
            const bool rotationChanged = ImGui::DragFloat3("Rotation", &editable.rotationEulerDegrees.x, 0.5f, -180.0f, 180.0f);
            const bool scaleChanged = ImGui::DragFloat3("Scale", &editable.scale.x, 0.01f, 0.1f, 5.0f, "%.2f");

            ImGui::Spacing();
            ImGui::TextUnformatted("Render");
            ImGui::Separator();
            const bool wireframeChanged = ImGui::Checkbox("Wireframe", &editable.wireframe);

            bool resetClicked = false;
            if (ImGui::Button("Reset Defaults")) {
                editable = sparks::core::CubeProperties{};
                resetClicked = true;
            }

            const glm::vec3 deltaPosition = editable.position - baseline.position;
            const glm::vec3 deltaRotation = editable.rotationEulerDegrees - baseline.rotationEulerDegrees;
            const glm::vec3 scaleRatio = glm::vec3(
                (baseline.scale.x > 0.0001f) ? (editable.scale.x / baseline.scale.x) : 1.0f,
                (baseline.scale.y > 0.0001f) ? (editable.scale.y / baseline.scale.y) : 1.0f,
                (baseline.scale.z > 0.0001f) ? (editable.scale.z / baseline.scale.z) : 1.0f);

            for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
                if (selectedObjects[static_cast<std::size_t>(i)]) {
                    auto& target = objects[static_cast<std::size_t>(i)];

                    if (resetClicked) {
                        target = sparks::core::CubeProperties{};
                        continue;
                    }

                    if (baseColorChanged) {
                        target.baseColor = editable.baseColor;
                    }
                    if (backgroundChanged) {
                        target.backgroundColor = editable.backgroundColor;
                    }
                    if (positionChanged) {
                        target.position += deltaPosition;
                    }
                    if (rotationChanged) {
                        target.rotationEulerDegrees += deltaRotation;
                    }
                    if (scaleChanged) {
                        target.scale = glm::clamp(target.scale * scaleRatio, glm::vec3(0.1f), glm::vec3(5.0f));
                    }
                    if (wireframeChanged) {
                        target.wireframe = editable.wireframe;
                    }
                }
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

}  // namespace sparks::ui
