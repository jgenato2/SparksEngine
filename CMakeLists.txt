# Add tools folder
add_subdirectory(tools)
cmake_minimum_required(VERSION 3.26)
cmake_policy(SET CMP0091 NEW)
project(SparksEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "Use DLL runtime for GLFW" FORCE)
endif()

include(FetchContent)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

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

FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG v5.4.3
)

FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG master
)

FetchContent_Declare(
  tinyfiledialogs
  GIT_REPOSITORY https://github.com/native-toolkit/libtinyfiledialogs.git
  GIT_TAG master
)

FetchContent_MakeAvailable(glfw glm imgui glad assimp stb tinyfiledialogs)

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

add_library(tinyfiledialogs_lib STATIC
  ${tinyfiledialogs_SOURCE_DIR}/tinyfiledialogs.c
)

target_include_directories(tinyfiledialogs_lib PUBLIC ${tinyfiledialogs_SOURCE_DIR})

if(MSVC)
  set_property(TARGET tinyfiledialogs_lib PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

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
  src/core/ApplicationUi.cpp
  src/core/ApplicationImport.cpp
  src/core/FbxImport.cpp
  src/core/WeatherSystem.cpp
  src/core/Rigging.cpp
  src/render/Renderer.cpp
  src/render/ShaderFacade.cpp
  src/render/shaders/textured/TexturedProgram.cpp
  src/render/shaders/skydome/SkydomeProgram.cpp
  src/render/shaders/terrain/TerrainProgram.cpp
  src/render/shaders/water/WaterProgram.cpp
  src/render/shaders/cloud/CloudProgram.cpp
  src/render/shaders/underwater/UnderwaterProgram.cpp
  src/render/WeatherRenderer.cpp
  src/render/terrain/DiamondSquareTerrain.cpp
  src/render/CloudShadowMap.cpp
  src/render/terrain/TerrainUtils.cpp
  src/render/GLUtils.cpp
  src/ui/PropertyPanel.cpp
)

target_include_directories(SparksEngine PRIVATE
  include
  # Assimp headers
  ${assimp_SOURCE_DIR}/include
  ${assimp_SOURCE_DIR}/include/assimp
  # GLM headers
  ${glm_SOURCE_DIR}
  ${glm_SOURCE_DIR}/glm
  ${glm_SOURCE_DIR}/glm/gtx
  ${glm_SOURCE_DIR}/glm/gtc
  # stb_image.h
  ${stb_SOURCE_DIR}
  # imgui
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  # tinyfiledialogs
  ${tinyfiledialogs_SOURCE_DIR}
  # FBX SDK include directory
  "C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.9/include"
)
target_link_libraries(SparksEngine PRIVATE glfw glm::glm glad imgui_lib tinyfiledialogs_lib assimp OpenGL::GL)


# FBX SDK linking (Windows only)
if (MSVC)
  target_link_directories(SparksEngine PRIVATE
    $<$<CONFIG:Debug>:C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.9/lib/x64/debug>
    $<$<CONFIG:Release>:C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.9/lib/x64/release>
    $<TARGET_FILE_DIR:SparksEngine>
  )
  target_link_libraries(SparksEngine PRIVATE
    libfbxsdk-md.lib
    libxml2-md.lib
    zlib-md.lib
    legacy_stdio_definitions.lib
    user32.lib
  )
endif()

if(MSVC)
  set_property(TARGET SparksEngine PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  target_compile_options(SparksEngine PRIVATE /W4 /permissive- /FS)
  target_compile_options(glfw PRIVATE /FS)
  target_compile_options(imgui_lib PRIVATE /FS)
  target_compile_options(glad PRIVATE /FS)
  target_compile_options(tinyfiledialogs_lib PRIVATE /FS)
  target_link_options(SparksEngine PRIVATE /NODEFAULTLIB:LIBCMT)
else()
  target_compile_options(SparksEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()
