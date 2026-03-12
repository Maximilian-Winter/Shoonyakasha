//
// Facade Test - Complete application using ONLY the facade API
//
// 碼道驗證 — Verifying the Way of Code
//
// This example proves the facade layer is sufficient for a full application.
// NO ApplicationBase, NO EnTT, NO Vulkan types — only Facade headers.
//

#include "Facade/EngineAPI.h"
#include "Facade/SceneAPI.h"
#include "Facade/InputAPI.h"
#include "Facade/PhysicsAPI.h"

#include <iostream>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace Shoonyakasha::Facade;

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    std::cout << "=========================================================\n";
    std::cout << "  Facade Test - Pure Facade API Demo\n";
    std::cout << "=========================================================\n";
    std::cout << "\n";
    std::cout << "  This app uses ONLY the Facade API layer.\n";
    std::cout << "  No ApplicationBase, no EnTT, no Vulkan in this file.\n";
    std::cout << "\n";
    std::cout << "  Controls:\n";
    std::cout << "    WASD   - Move camera\n";
    std::cout << "    RMB    - Look around\n";
    std::cout << "    ESC    - Toggle mouse capture\n";
    std::cout << "    L      - Create point light at camera position\n";
    std::cout << "    P      - Toggle physics\n";
    std::cout << "    1/2/3  - Camera modes\n";
    std::cout << "\n";
    std::cout << "=========================================================\n";
    std::cout << std::endl;

    try {
        // ─── Engine configuration (no Vulkan types) ──────────────
        EngineConfig config;
        config.title = "Facade Test - Pure API Demo";
        config.width = 1920;
        config.height = 1080;
        config.logFile = "facade_test.log";
        config.hdrEnvironmentPath = "cubemaps_hdrs/kloofendal_28d_misty_8k.hdr";
        config.pipelineJsonPath = "pipeline.json";

        EngineAPI engine(config);

        // Track state for the example
        int lightCount = 0;

        // ─── onInit callback ─────────────────────────────────────
        engine.setOnInit([&]() {
            std::cout << "[Facade] onInit — setting up scene\n";

            // Create camera (returns EntityHandle, not entt::entity)
            EntityHandle camera = engine.createCamera(
                glm::vec3(0.f, 5.f, 15.f),  // position
                60.f,                         // fov
                8.f,                          // speed
                0.1f,                         // near
                500.f                         // far
            );
            std::cout << "[Facade] Camera entity: " << camera << "\n";

            // Create directional light
            engine.createDirectionalLight(
                glm::vec3(-0.5f, -1.f, -0.3f),
                glm::vec3(1.f, 0.975f, 0.95f),
                3.f
            );

            // Load a glTF scene
            GltfResult result = engine.loadGltfScene("../../examples/declarative_sponza_test/Sponza/glTF/Sponza.gltf");
            if (result.success) {
                std::cout << "[Facade] Loaded scene: "
                          << result.entities.size() << " entities, "
                          << result.totalVertices << " vertices, "
                          << result.totalTextures << " textures\n";
            } else {
                std::cout << "[Facade] Failed to load scene: " << result.error << "\n";
                // Try alternative path
                result = engine.loadGltfScene("Sponza/glTF/Sponza.gltf");
                if (result.success) {
                    std::cout << "[Facade] Loaded from alt path: "
                              << result.entities.size() << " entities\n";
                }
            }

            // Demonstrate SceneAPI: query and manipulate entities
            SceneAPI& scene = engine.getScene();
            auto allEntities = scene.getAllEntities();
            std::cout << "[Facade] Total entities: " << allEntities.size() << "\n";

            // Enable physics (disabled by default)
            PhysicsAPI& physics = engine.getPhysics();
            physics.setGravity(glm::vec3(0.f, -9.81f, 0.f));
            std::cout << "[Facade] Physics gravity: ("
                      << physics.getGravity().x << ", "
                      << physics.getGravity().y << ", "
                      << physics.getGravity().z << ")\n";
        });

        // ─── onPostInit callback ──────────────────────────────────
        engine.setOnPostInit([&]() {
            std::cout << "[Facade] onPostInit — engine fully initialized\n";
        });

        // ─── onUpdate callback ────────────────────────────────────
        engine.setOnUpdate([&](float dt) {
            // Demonstrate InputAPI polling
            InputAPI& input = engine.getInput();

            // Example: check if a key is held
            if (input.isKeyDown(76)) {  // 'L' key (GLFW_KEY_L = 76)
                // Throttle: only on first press (simple cooldown)
                static float cooldown = 0.f;
                cooldown -= dt;
                if (cooldown <= 0.f) {
                    // Create a point light at camera position
                    SceneAPI& scene = engine.getScene();
                    EntityHandle cam = engine.getCameraEntity();
                    glm::vec3 camPos = scene.getPosition(cam);

                    engine.createPointLight(
                        camPos,
                        glm::vec3(1.f, 0.8f, 0.6f),
                        5.f,
                        20.f
                    );
                    lightCount++;
                    std::cout << "[Facade] Created point light #" << lightCount
                              << " at (" << camPos.x << ", " << camPos.y
                              << ", " << camPos.z << ")\n";
                    cooldown = 0.5f;
                }
            }
        });

        // ─── onKeyPressed callback ────────────────────────────────
        engine.setOnKeyPressed([&](int keyCode) {
            if (keyCode == 80) {  // 'P' key
                PhysicsAPI& physics = engine.getPhysics();
                bool wasEnabled = physics.isEnabled();
                physics.setEnabled(!wasEnabled);
                std::cout << "[Facade] Physics "
                          << (wasEnabled ? "PAUSED" : "ENABLED") << "\n";
            }
        });

        // ─── onCleanup callback ──────────────────────────────────
        engine.setOnCleanup([&]() {
            std::cout << "[Facade] Cleanup — created " << lightCount
                      << " dynamic lights\n";
        });

        // ─── Run! ─────────────────────────────────────────────────
        engine.run();

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
