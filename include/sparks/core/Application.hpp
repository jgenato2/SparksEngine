#pragma once


#include "sparks/render/Renderer.hpp"
struct GLFWwindow;

namespace sparks::core {

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run();

private:
    void initializeWindow();
    void initializeImGui();
    void shutdownImGui();

    GLFWwindow* m_window{nullptr};
    sparks::render::EnvironmentSettings m_environmentSettings;
};

}  // namespace sparks::core
