//
// Particle Flow — Visual Showcase Entry Point
//

#include "ParticleFlowApp.h"
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "\n";
    std::cout << "  Particle Flow — Visual Showcase\n";
    std::cout << "\n";
    std::cout << "  GPU compute particle simulation with dynamic attractors,\n";
    std::cout << "  pulsing gravity, and rich visual effects.\n";
    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD   - Move\n";
    std::cout << "    Q/E    - Down/Up\n";
    std::cout << "    RMB    - Look around\n";
    std::cout << "    F12    - Screenshot\n";
    std::cout << "    1/2/3  - Camera modes (Free/Orbit/FirstPerson)\n";
    std::cout << "\n";
    std::cout << std::endl;

    try {
        ApplicationConfig config;
        config.width = 3840;
        config.height = 2160;
        config.title = "Particle Flow — Visual Showcase";
        config.logFile = "particle_flow.log";
        config.hdrEnvironmentPath = "cubemaps_hdrs/farm_sunset_8k.hdr";
        config.pipelineJsonPath = "ssbo_pipeline.json";
        config.renderGraphParameters["particleCount"] = 100000;

        ParticleFlowApp app(config);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
