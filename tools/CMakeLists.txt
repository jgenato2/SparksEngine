# Minimal CMakeLists.txt for FBX SDK tool
cmake_minimum_required(VERSION 3.10)
project(fbx_list_animstacks)

# Set your FBX SDK root here
set(FBX_SDK_ROOT "C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.9")

# Include and lib directories
include_directories("${FBX_SDK_ROOT}/include")
link_directories("${FBX_SDK_ROOT}/lib/x64/release")


add_executable(fbx_list_animstacks fbx_list_animstacks.cpp)
target_link_libraries(fbx_list_animstacks libfbxsdk-md.lib libxml2-md.lib zlib-md.lib legacy_stdio_definitions.lib user32.lib)

# Ensure MSVC runtime matches FBX SDK (use /MD)
if (MSVC)
	set_property(TARGET fbx_list_animstacks PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

target_compile_definitions(fbx_list_animstacks PRIVATE _CRT_SECURE_NO_WARNINGS)

