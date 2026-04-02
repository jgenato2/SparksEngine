cmake_minimum_required(VERSION 3.26)
cmake_policy(SET CMP0091 NEW)
project(SparksEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "Use DLL runtime for GLFW" FORCE)
endif()

include(FetchContent)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.91.8
)

FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG v2.0.8
)

FetchContent_MakeAvailable(glfw glm imgui glad)

find_package(OpenGL REQUIRED)

set(GLAD_SOURCES_DIR ${glad_SOURCE_DIR})
include(${glad_SOURCE_DIR}/cmake/GladConfig.cmake)
glad_add_library(glad REPRODUCIBLE API gl:core=4.6)

add_library(imgui_lib STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui_lib PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_compile_definitions(imgui_lib PRIVATE IMGUI_IMPL_OPENGL_LOADER_GLAD2)
target_link_libraries(imgui_lib PUBLIC glfw glad OpenGL::GL)

if(MSVC)
  set_property(TARGET glfw PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  set_property(TARGET imgui_lib PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  set_property(TARGET glad PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

add_executable(SparksEngine
  src/app/main.cpp
  src/core/Application.cpp
  src/render/Renderer.cpp
  src/ui/PropertyPanel.cpp
)

target_include_directories(SparksEngine PRIVATE include)
target_link_libraries(SparksEngine PRIVATE glfw glm::glm glad imgui_lib OpenGL::GL)

if(MSVC)
  set_property(TARGET SparksEngine PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  target_compile_options(SparksEngine PRIVATE /W4 /permissive-)
else()
  target_compile_options(SparksEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()
