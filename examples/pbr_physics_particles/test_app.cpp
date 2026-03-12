//
// Combined PBR + Physics + Particles + Bloom — Entry Point
//
// 萬法歸一 — All phenomena return to One
//

#include "CombinedExampleApp.h"
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

    std::cout << "=========================================================\n";
    std::cout << "  Combined PBR + Physics + Particles + Bloom Demo\n";
    std::cout << "=========================================================\n";
    std::cout << "\n";
    std::cout << "  Features:\n";
    std::cout << "    - Procedural PBR meshes (boxes, spheres, ground)\n";
    std::cout << "    - Bullet3 physics simulation\n";
    std::cout << "    - 75K GPU compute particles (impact-reactive mist)\n";
    std::cout << "    - Deferred rendering with IBL\n";
    std::cout << "    - Bloom post-processing\n";
    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD   - Move camera\n";
    std::cout << "    Q/E    - Down / Up\n";
    std::cout << "    RMB    - Look around\n";
    std::cout << "    ESC    - Toggle mouse capture\n";
    std::cout << "    P      - Play / Pause physics\n";
    std::cout << "    R      - Reset scene (paused)\n";
    std::cout << "    T      - Toggle particles on/off\n";
    std::cout << "    SPACE  - Spawn object (launched forward)\n";
    std::cout << "    1/2/3  - Camera modes (Free/Orbit/FirstPerson)\n";
    std::cout << "\n";
    std::cout << "  Physics starts PAUSED — press P to begin!\n";
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << std::endl;

    try {
        ApplicationConfig config;
        config.width = 1920;
        config.height = 1080;
        config.title = "PBR + Physics + Particles + Bloom";
        config.logFile = "combined_example.log";
        config.hdrEnvironmentPath = "cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";
        config.pipelineJsonPath = "pipeline.json";
        config.renderGraphParameters["particleCount"] = 75000;

        CombinedExampleApp app(config);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
