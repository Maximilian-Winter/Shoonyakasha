//
// Instancing Test - Entry Point
//
// Visual confirmation for two changes that are hard to trust from a log:
//   1. glTF geometry is uploaded once and shared between every node that
//      references it, with reference-counted lifetime.
//   2. Node transforms live on entities in a scene graph instead of being baked
//      into vertex positions.
//

#include "InstancingTestApp.h"
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

int main(int argc, char** argv) {
    bool selfTest = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--selftest") selfTest = true;
    }

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "=========================================================\n";
    std::cout << "  Instancing Test - shared geometry and the scene graph\n";
    std::cout << "=========================================================\n";
    std::cout << "\n";
    std::cout << "  What to look for:\n";
    std::cout << "\n";
    std::cout << "    SHARING      The window title reports how many GPU buffers\n";
    std::cout << "                 are alive. 18 boxes load from one mesh, so it\n";
    std::cout << "                 should read 2 -- one vertex, one index. Press\n";
    std::cout << "                 SPACE to add 24 more clones and watch the count\n";
    std::cout << "                 stay at 2.\n";
    std::cout << "\n";
    std::cout << "    HIERARCHY    Only the root node is animated. If the arms and\n";
    std::cout << "                 their satellites swing with it, the scene graph\n";
    std::cout << "                 is composing transforms. Press H to stop it.\n";
    std::cout << "\n";
    std::cout << "    LOCAL ORIGIN The small satellites tumble in place. Baked into\n";
    std::cout << "                 world space they would swing around the middle of\n";
    std::cout << "                 the scene instead. Press J to stop them.\n";
    std::cout << "\n";
    std::cout << "    NODE FORMS   The tilted box above the ring is a node stored as\n";
    std::cout << "                 a matrix rather than translation/rotation/scale,\n";
    std::cout << "                 so it exercises the decomposition. The box below\n";
    std::cout << "                 the ring is mirrored by a negative scale.\n";
    std::cout << "\n";
    std::cout << "    LIFETIME     X destroys half the clones. 'Awaiting free' in the\n";
    std::cout << "                 title rises then drains a few frames later -- a\n";
    std::cout << "                 buffer is not freed while a frame may still be\n";
    std::cout << "                 using it. C destroys them all; the count returns\n";
    std::cout << "                 to 2, because the loaded boxes still hold it.\n";
    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD / Q / E  - Move camera        RMB - Look    ESC - Capture\n";
    std::cout << "    SPACE - Spawn 24 clones (sharing one mesh)\n";
    std::cout << "    X     - Destroy half the clones\n";
    std::cout << "    C     - Destroy all clones\n";
    std::cout << "    R     - Release one clone's mesh (siblings must survive)\n";
    std::cout << "    F     - Wait for the device and flush the delete queue\n";
    std::cout << "    H / J - Toggle carousel rotation / satellite spin\n";
    std::cout << "\n  Run with --selftest to drive the whole sequence unattended.\n";
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << std::endl;

    try {
        ApplicationConfig config;
        config.title = "Instancing Test";
        config.logFile = "instancing_test.log";
        // Shared with physics_test rather than duplicated: it is the only HDR
        // tracked in the repository, and it is 99 MB.
        config.hdrEnvironmentPath = "../physics_test/cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";
        config.pipelineJsonPath = "pipeline.json";

        InstancingTestApp app(config, selfTest);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
