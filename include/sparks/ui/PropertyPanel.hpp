#pragma once

#include <span>

#include "sparks/core/CubeProperties.hpp"

namespace sparks::ui {

class PropertyPanel {
public:
    void draw(std::span<sparks::core::CubeProperties> objects, std::span<bool> selectedObjects) const;
};

}  // namespace sparks::ui
