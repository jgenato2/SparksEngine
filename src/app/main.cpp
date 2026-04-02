#include <exception>
#include <iostream>

#include "sparks/core/Application.hpp"

int main() {
    try {
        sparks::core::Application app;
        return app.run();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }
}
