"""Prepare disposable native screenshot sources; never edits engine/example sources."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/docs-capture'
OUT.mkdir(parents=True, exist_ok=True)


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f'Capture hook needs updating: {old!r}')
    return text.replace(old, new, 1)


def read(path):
    return (ROOT / path).read_text(encoding='utf-8')


source = read('src/App/ApplicationBase.cpp')
source = replace_once(source, '#include <cmath>', '#include <cmath>\n#include <cstdlib>')
source = replace_once(source, '    m_startTime =', '    m_config.width = 1280;\n    m_config.height = 720;\n    m_startTime =')
source = replace_once(source, '    while (!m_window->shouldClose()) {', '''    if (std::getenv("DOCS_CAPTURE_BOX")) {
        auto& transform = getRegistry().get<ECS::TransformComponent>(getCameraEntity());
        transform.position = glm::vec3(1.6f, 1.0f, 3.2f);
        transform.rotation = glm::vec3(-0.27f, 0.46f, 0.0f);
        transform.isDirty = true;
    }
    if (std::getenv("DOCS_CAPTURE_PLAY")) onKeyPressed(GLFW_KEY_P);
    const auto captureStart = std::chrono::steady_clock::now();
    const char* capturePath = std::getenv("DOCS_CAPTURE_PATH");
    const char* captureDelay = std::getenv("DOCS_CAPTURE_SECONDS");
    const double captureSeconds = captureDelay ? std::atof(captureDelay) : 3.0;
    while (!m_window->shouldClose()) {''')
source = replace_once(source, '        render();\n    }', '''        render();
        if (capturePath && std::chrono::duration<double>(std::chrono::steady_clock::now() - captureStart).count() >= captureSeconds) {
            if (!captureScreenshot(capturePath)) throw std::runtime_error("Documentation capture failed");
            break;
        }
    }''')
(OUT / 'ApplicationBase.cpp').write_text(source, encoding='utf-8')

replacements = [('Shoonyakasha', 'ApplicationBase', 'src/App')]
for folder, stem, target in [
    ('rendering/bloom_test', 'BloomTestApp', 'ShoonyakashaBloomTest'),
    ('compute/particle_test', 'ParticleTestApp', 'ShoonyakashaParticleTest'),
]:
    source = read(f'examples/cpp/{folder}/{stem}.cpp')
    source = '#include "Vulkan/FrameGraph/RenderTargetSaver.h"\n#include <cstdlib>\n' + source
    if stem == 'ParticleTestApp':
        source = replace_once(source, '(3840, 2160,', '(1280, 720,')
    marker = '    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;'
    source = replace_once(source, marker, '''    static const auto captureStart = std::chrono::steady_clock::now();
    const char* capturePath = std::getenv("DOCS_CAPTURE_PATH");
    const char* delay = std::getenv("DOCS_CAPTURE_SECONDS");
    if (capturePath && std::chrono::duration<double>(std::chrono::steady_clock::now()-captureStart).count() >= (delay ? std::atof(delay) : 3.0)) {
        vkDeviceWaitIdle(m_device->getLogicalDevice());
        FrameGraph::RenderTargetSaver saver(*m_device);
        if (!saver.save(m_swapChain->getSwapChainImage(imageIndex), m_swapChain->getSwapChainImageFormat(), m_swapChain->getSwapChainExtent(), VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, capturePath, m_logger.get())) throw std::runtime_error("Documentation capture failed");
        glfwSetWindowShouldClose(m_window->getWindow(), GLFW_TRUE);
    }
''' + marker)
    (OUT / f'{stem}.cpp').write_text(source, encoding='utf-8')
    replacements.append((target, stem, f'examples/cpp/{folder}'))

# The normal Sponza demo requires an optional model. Substitute only in this copy.
source = read('examples/cpp/rendering/declarative_sponza_test/DeclarativeSponzaApp.cpp')
source = replace_once(source, '"NewSponza_Main_glTF_003.gltf"', '"models/Box.gltf"')
(OUT / 'DeclarativeSponzaApp.cpp').write_text(source, encoding='utf-8')
replacements.append(('DeclarativeSponzaTest', 'DeclarativeSponzaApp', 'examples/cpp/rendering/declarative_sponza_test'))

hook = ''
for target, stem, folder in replacements:
    hook += f'''function(capture_{stem})
    get_target_property(sources {target} SOURCES)
    list(FILTER sources EXCLUDE REGEX "{stem}[.]cpp$")
    list(APPEND sources "${{CMAKE_CURRENT_FUNCTION_LIST_DIR}}/{stem}.cpp")
    set_property(TARGET {target} PROPERTY SOURCES "${{sources}}")
    target_include_directories({target} PRIVATE "${{CMAKE_SOURCE_DIR}}/{folder}")
endfunction()
cmake_language(DEFER CALL capture_{stem})
'''
(OUT / 'capture.cmake').write_text(hook, encoding='utf-8')
print(f'Capture hook: {(OUT / "capture.cmake").as_posix()}')
