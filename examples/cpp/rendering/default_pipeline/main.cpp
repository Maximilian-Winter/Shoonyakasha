//
// Default pipeline example - Sponza on the pipeline the engine ships
//
// Uses only the facade API. EngineConfig::pipelineJsonPath is left empty, so
// the engine loads its default pipeline: deferred PBR with cascaded sun
// shadows, contact shadows, ambient occlusion, bloom and automatic exposure.
// Without Sponza (python tools/fetch_assets.py sponza) a colonnade of boxes
// stands in for it.
//
// Usage:
//     DefaultPipelineExample [low|medium|high]    quality preset, default high
//
// Keys: WASD/Q/E + right mouse to fly, 1/2/3 for the low/medium/high preset.
//
// The default pipeline is found through $SHOONYAKASHA_DEFAULT_PIPELINE, or
// else in the source tree the engine was built from.
//
// Sponza 2022 Scene, commissioned by Frank Meinl, sponsored by Anton Kaplanyan.
// Intel Sample Library. See assets/README.md about its licence.
//

#include "Facade/EngineAPI.h"
#include "Facade/SceneAPI.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace Shoonyakasha::Facade;

namespace {

const std::string kSponza = "models/NewSponza_Main_glTF_003.gltf";
const std::string kBox = "models/Box.gltf";

/// Pitch and yaw that point a camera at `eye` towards `target`.
glm::vec3 lookRotation(const glm::vec3& eye, const glm::vec3& target) {
    const glm::vec3 d = glm::normalize(target - eye);
    return {std::asin(d.y), std::atan2(-d.x, -d.z), 0.f};
}

EntityHandle rootOf(SceneAPI& scene, EntityHandle entity) {
    for (EntityHandle parent = scene.getParent(entity); parent != NullEntity; parent = scene.getParent(entity)) {
        entity = parent;
    }
    return entity;
}

/// A box of `size` centred on `center`, from the bundled unit cube.
void box(EngineAPI& engine, const glm::vec3& center, const glm::vec3& size, const glm::vec4& color) {
    SceneAPI& scene = engine.getScene();
    const GltfResult result = engine.loadGltfScene(kBox);
    for (EntityHandle entity : result.entities) {
        const EntityHandle root = rootOf(scene, entity);
        // Box.gltf's root node is turned 90 degrees about X; without it the
        // scale lines up with world axes.
        scene.setRotation(root, glm::vec3(0.f));
        scene.setScale(root, size);
        scene.setPosition(root, center);
        scene.setMaterialVec4(entity, "baseColorFactor", color);
        scene.setMaterialFloat(entity, "metallicFactor", 0.f);
        scene.setMaterialFloat(entity, "roughnessFactor", 0.85f);
    }
}

/// Stand-in for Sponza: a floor, two rows of pillars under beams, walls.
void buildColonnade(EngineAPI& engine) {
    const glm::vec4 stone(0.55f, 0.5f, 0.42f, 1.f), wall(0.62f, 0.55f, 0.46f, 1.f);
    box(engine, {0.f, -0.1f, 0.f}, {30.f, 0.2f, 14.f}, {0.45f, 0.42f, 0.38f, 1.f});
    for (float side : {-3.5f, 3.5f}) {
        for (int x = -10; x <= 10; x += 4) {
            box(engine, {static_cast<float>(x), 2.5f, side}, {0.7f, 5.f, 0.7f}, stone);
        }
        box(engine, {0.f, 5.3f, side}, {22.f, 0.6f, 1.2f}, stone);
        box(engine, {0.f, 3.f, side * 1.9f}, {30.f, 6.f, 0.4f}, wall);
    }
    box(engine, {12.5f, 3.f, 0.f}, {0.4f, 6.f, 14.f}, wall);
    box(engine, {1.f, 0.5f, 0.8f}, {1.f, 1.f, 1.f}, {0.7f, 0.2f, 0.15f, 1.f});
}

} // namespace

int main(int argc, char** argv) {
    const std::string preset = argc > 1 ? argv[1] : "high";

    try {
        EngineConfig config;
        config.title = "Shoonyakasha - Sponza (default pipeline)";
        config.width = 1600;
        config.height = 900;
        config.logFile = "default_pipeline.log";
        config.hdrEnvironmentPath = "env/kloofendal_misty_1k.hdr";
        // config.pipelineJsonPath stays empty: the default pipeline.

        EngineAPI engine(config);

        engine.setOnInit([&] {
            const glm::vec3 eye(-10.5f, 1.8f, -0.6f), target(4.f, 2.6f, 0.4f);
            const EntityHandle camera = engine.createCamera(eye, 60.f, 4.f, 0.05f, 300.f);
            SceneAPI& scene = engine.getScene();
            scene.setRotation(camera, lookRotation(eye, target));

            // The first directional light with its shadow flag set is the sun.
            const EntityHandle sun = engine.createDirectionalLight(
                glm::vec3(0.3f, -1.f, 0.35f), glm::vec3(1.f, 0.93f, 0.82f), 6.f);
            scene.setLightCastShadows(sun, true);
            engine.setSunShadowSettings(4, 40.f, 0.7f, 2048, 50.f);
            engine.createPointLight({-4.f, 2.2f, 2.8f}, {1.f, 0.62f, 0.3f}, 4.f, 8.f);
            engine.createPointLight({4.f, 2.2f, -2.8f}, {1.f, 0.62f, 0.3f}, 4.f, 8.f);

            if (assetExists(kSponza)) {
                GltfOptions options;
                options.maxTextureSize = 2048;
                const GltfResult result = engine.loadGltfScene(kSponza, options);
                std::cout << "Sponza: " << result.entities.size() << " entities, "
                          << result.totalVertices << " vertices\n";
            } else {
                std::cout << kSponza << " is not in the asset root; showing a colonnade of boxes.\n";
                buildColonnade(engine);
            }

            if (!engine.applyPipelinePreset(preset)) {
                std::cerr << "No preset '" << preset << "'\n";
            }
        });

        engine.setOnKeyPressed([&](int key) {
            // GLFW key codes for 1, 2 and 3.
            static const char* presets[] = {"low", "medium", "high"};
            if (key >= '1' && key <= '3') {
                engine.applyPipelinePreset(presets[key - '1']);
                std::cout << "preset " << presets[key - '1'] << "\n";
            }
        });

        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
