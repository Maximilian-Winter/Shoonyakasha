//
// Bloom Test Application Entry Point
//
// 明暗相融  光影交織
// Light and dark merge — testing async compute bloom
//

#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "BloomTestApp.h"

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try {
        BloomTestApp app;
        app.init();
        app.run();
        app.cleanup();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
