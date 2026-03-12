//
// Particle Test Application Entry Point
//
// 萬粒如星  星如塵  塵歸虛
// Ten thousand particles like stars — testing async compute
//

#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#include "ParticleTestApp.h"

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    try {
        ParticleTestApp app;
        app.init();
        app.run();
        app.cleanup();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
