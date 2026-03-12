//
// SSBO Data Flow Example - Entry Point
//
// 保存之輪 — The Wheel of Preservation
//

#include "SSBODataFlowApp.h"
#include <iostream>
#include <stdexcept>
#include <cstring>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // Parse --from-file flag
    bool loadFromFile = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--from-file") == 0) {
            loadFromFile = true;
        }
    }

    std::cout << "\n";
    std::cout << "  SSBO Data Flow Example — Phase 4 Demonstration\n";
    std::cout << "\n";
    std::cout << "  This example demonstrates:\n";
    std::cout << "    - GPU particle compute with declarative SSBO init\n";
    std::cout << "    - Save particle state to disk (press S)\n";
    std::cout << "    - Load particle state from disk (--from-file)\n";
    std::cout << "    - Ring-buffered readback with stats logging\n";
    std::cout << "\n";

    if (loadFromFile) {
        std::cout << "  Mode: LOAD FROM FILE (data/particles.bin)\n";
    } else {
        std::cout << "  Mode: SAVE — press S to save particle state\n";
        std::cout << "         Then re-run with --from-file to load it\n";
    }

    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD   - Move\n";
    std::cout << "    Q/E    - Down/Up\n";
    std::cout << "    RMB    - Look around\n";
    std::cout << "    S      - Save particle state to disk\n";
    std::cout << "    F12    - Screenshot\n";
    std::cout << "    1/2/3  - Camera modes (Free/Orbit/FirstPerson)\n";
    std::cout << "\n";
    std::cout << std::endl;

    try {
        ApplicationConfig config;
        config.width = 3840;
        config.height = 2160;
        config.logFile = "ssbo_data_flow.log";
        config.hdrEnvironmentPath = "cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";
        config.renderGraphParameters["particleCount"] = 150000;

        // Choose pipeline JSON based on mode
        if (loadFromFile) {
            config.title = "SSBO Data Flow — LOAD from file";
            config.pipelineJsonPath = "ssbo_pipeline_from_file.json";
        } else {
            config.title = "SSBO Data Flow — SAVE mode (press S to save)";
            config.pipelineJsonPath = "ssbo_pipeline.json";
        }

        SSBODataFlowApp app(config, loadFromFile);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
