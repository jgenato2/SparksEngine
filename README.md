# SparksEngine

Modern C++ cube renderer with a clean architecture and an editor-like UI:
- Left panel: live cube viewport
- Right panel: property controls for cube configuration

## Tech Stack
- C++23
- CMake (FetchContent dependencies)
- OpenGL 4.6
- GLFW
- GLAD (OpenGL loader)
- Dear ImGui (properties panel and layout)
- GLM (math)

## Build

Prerequisite (first setup):

```powershell
python -m pip install --user jinja2
```

GLAD uses Python to generate OpenGL loader sources during the build.

```powershell
cmake --preset vs2026
cmake --build --preset build-release
```

## Run

```powershell
./build/Release/SparksEngine.exe
```

If you use a single-config generator (e.g., Ninja), run:

```powershell
./build/SparksEngine.exe
```

## Manual Run In VS Code

1. Run task: `CMake: Configure (vs2026)`
2. Run task: `CMake: Build Release`
3. Run task: `Run: SparksEngine (Release)`

Or debug with `Launch SparksEngine (Release)` from Run and Debug.
