//
// Physics Test - Entry Point
//
// 重力之試 — Testing the laws of gravity
//

#include "PhysicsTestApp.h"
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
    std::cout << "  Physics Test - Bullet3 Integration Demo\n";
    std::cout << "=========================================================\n";
    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD   - Move camera\n";
    std::cout << "    Q/E    - Down/Up\n";
    std::cout << "    RMB    - Look around\n";
    std::cout << "    ESC    - Toggle mouse capture\n";
    std::cout << "    P      - Play / Pause physics\n";
    std::cout << "    R      - Reset scene (paused)\n";
    std::cout << "    SPACE  - Spawn object (launched forward)\n";
    std::cout << "    1/2/3  - Camera modes (Free/Orbit/FirstPerson)\n";
    std::cout << "\n";
    std::cout << "  Physics starts PAUSED - press P to begin!\n";
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << std::endl;

    try {
        ApplicationConfig config;
        config.title = "Physics Test - Bullet3 Integration";
        config.logFile = "physics_test.log";
        config.hdrEnvironmentPath = "cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";
        config.pipelineJsonPath = "pipeline.json";

        PhysicsTestApp app(config);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
