//
// Declarative Sponza Test - Entry Point
//
// 宣言之道 — The Way of Declaration
//

#include "DeclarativeSponzaApp.h"
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

    std::cout << "===============================================================\n";
    std::cout << "  Declarative Sponza Test - v3 Frame Graph Demonstration\n";
    std::cout << "===============================================================\n";
    std::cout << "\n";
    std::cout << "  This example demonstrates the v3 declarative features:\n";
    std::cout << "    - Standard buffers (camera, time, screen) auto-managed\n";
    std::cout << "    - Built-in scene renderers (opaque_geometry, transparent_geometry)\n";
    std::cout << "    - JSON-driven pipeline configuration\n";
    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD   - Move\n";
    std::cout << "    Q/E    - Down/Up\n";
    std::cout << "    RMB    - Look around\n";
    std::cout << "    ESC    - Toggle mouse capture\n";
    std::cout << "    1/2/3  - Camera modes (Free/Orbit/FirstPerson)\n";
    std::cout << "    SPACE  - Toggle horizon lock (Free mode)\n";
    std::cout << "\n";
    std::cout << "===============================================================\n";
    std::cout << std::endl;

    try {
        ApplicationConfig config;
        config.title = "Declarative Sponza - v3 Frame Graph";
        config.logFile = "declarative_sponza.log";
        config.hdrEnvironmentPath = "cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";
        config.pipelineJsonPath = "pbr_ibl_pipeline_v3.json";
        config.renderGraphParameters["particleCount"] = 50000;

        DeclarativeSponzaApp app(config);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
