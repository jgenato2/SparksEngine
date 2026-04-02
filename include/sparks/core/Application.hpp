#pragma once

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
};

}  // namespace sparks::core
