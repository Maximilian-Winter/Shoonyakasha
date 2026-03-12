//
// Skinned Mesh Test - Entry Point
//
// 骨之舞 — The Dance of Bones
//

#include "SkinnedMeshApp.h"
#include <iostream>
#include <stdexcept>
#include <filesystem>

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
    std::cout << "  Skinned Mesh Test - Skeletal Animation Demo\n";
    std::cout << "  ============================================\n";
    std::cout << "\n";
    std::cout << "  Loads a skinned glTF model (Fox) and plays bone animations.\n";
    std::cout << "  GPU skinning via bone matrix SSBO in vertex shader.\n";
    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD       - Move camera\n";
    std::cout << "    Q/E        - Down/Up\n";
    std::cout << "    RMB        - Look around\n";
    std::cout << "    1/2/3      - Camera modes (Free/Orbit/FirstPerson)\n";
    std::cout << "    4/5/6      - Switch animation clips\n";
    std::cout << "    Space      - Toggle animation pause\n";
    std::cout << "    +/-        - Speed up / slow down animation\n";
    std::cout << "    ESC        - Toggle mouse capture\n";
    std::cout << "\n";

    try {
        ApplicationConfig config;
        config.width = 1280;
        config.height = 720;
        config.title = "Skinned Mesh - Skeletal Animation";
        config.logFile = "skinned_mesh.log";
        config.pipelineJsonPath = "skinned_pipeline.json";

        // IBL — search shared paths
        std::vector<std::string> hdrPaths = {
            "cubemaps_hdrs/charolettenbrunn_park_4k.hdr",
            "../declarative_sponza_test/cubemaps_hdrs/charolettenbrunn_park_4k.hdr",
        };
        for (const auto& path : hdrPaths) {
            if (std::filesystem::exists(path)) {
                config.hdrEnvironmentPath = path;
                break;
            }
        }

        // Smaller IBL params for this test
        config.iblParams.environmentSize = 512;
        config.iblParams.irradianceSize = 32;
        config.iblParams.prefilterSize = 256;
        config.iblParams.brdfLUTSize = 512;
        config.iblParams.irradianceSamples = 1024;
        config.iblParams.prefilterSamples = 512;
        config.iblParams.brdfSamples = 512;

        config.resourceCacheSize = 512 * 1024 * 1024;

        SkinnedMeshApp app(config);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
