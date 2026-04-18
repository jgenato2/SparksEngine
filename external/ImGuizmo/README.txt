ImGuizmo integration placeholder.

Download ImGuizmo from: https://github.com/CedricGuillemet/ImGuizmo

Place ImGuizmo.h and ImGuizmo.cpp here, then add to your CMakeLists.txt:

add_library(ImGuizmo STATIC external/ImGuizmo/ImGuizmo.cpp)
target_include_directories(ImGuizmo PUBLIC external/ImGuizmo)

target_link_libraries(your_target PRIVATE ImGuizmo)

See ImGuizmo's README for usage details.
