# CMakeLists.txt
#
# 3.21 rather than 3.12: scikit-build-core already requires it, and 3.12
# compatibility mode silently reverts a decade of policies to their legacy
# behaviour. Nothing here needs that.
cmake_minimum_required(VERSION 3.21)

# C is listed explicitly because ENGINE_SOURCES globs *.c. It was previously
# being enabled as a side effect of a dependency's own project() call, which
# works until it doesn't.
project(Shoonyakasha VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_EXAMPLES "Build example projects" OFF)
option(BUILD_TESTS "Build test projects" OFF)
option(BUILD_PYTHON "Build Python/Cython bindings" OFF)

# When the Python wheel is the product, the C++ library's install/export rules
# are not wanted: they would drop include/, lib/ and lib/cmake/ into the wheel
# root (i.e. straight into site-packages). pyproject.toml turns this OFF.
option(SHOONYAKASHA_INSTALL "Generate install and export rules for the C++ library" ON)

# ─────────────────────────────────────────────────────────────────────────────
# Dependencies
#
# All of these come from vcpkg (see vcpkg.json). Every one is consumed through
# its imported target rather than a raw path variable. The imported targets
# carry their own include directories, which is what keeps absolute paths into
# <build-dir>/vcpkg_installed/ out of the export set — the manifest-mode change
# that broke the generate step.
# ─────────────────────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

# CONFIGURE_DEPENDS makes the build system re-glob when files appear or vanish.
# A glob without it goes stale the moment you add a source file.
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

file(GLOB_RECURSE ENGINE_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(Shoonyakasha STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# Required to link this static library into the Python extension module
# (a shared object) on GCC/Clang. No-op on MSVC.
set_target_properties(Shoonyakasha PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(Shoonyakasha
        PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyobjloader
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
)

# BULLET_LIBRARIES expands to vcpkg's imported target names (BulletDynamics,
# BulletCollision, LinearMath, ...), not to file paths — so it is safe to
# export. Those targets already carry include/bullet; the separate
# target_include_directories() call that used to add it by hand is gone, and
# with it the "path is prefixed in the build directory" error.
target_link_libraries(Shoonyakasha
        PUBLIC
        Vulkan::Vulkan
        glfw
        glm::glm
        nlohmann_json::nlohmann_json
        EnTT::EnTT
        ${BULLET_LIBRARIES}
)

# Test-support constructors. Previously unconditional, which meant every
# shipped wheel and every release build carried them. They are only needed by
# the target that actually tests against them.
if(BUILD_TESTS)
    target_compile_definitions(Shoonyakasha PRIVATE SHOONYAKASHA_TESTING)
endif()

# Warnings. Every example target and the test target already build at /W4 or
# -Wall -Wextra; the engine library itself did not, which left the largest and
# most error-prone part of the codebase compiling at the compiler default.
# PRIVATE so consumers are not forced into our warning level.
# No -Werror yet — turn it on once the existing warnings are cleared.
if(MSVC)
    target_compile_options(Shoonyakasha PRIVATE /W4)
else()
    target_compile_options(Shoonyakasha PRIVATE -Wall -Wextra)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Install / export
# ─────────────────────────────────────────────────────────────────────────────
if(SHOONYAKASHA_INSTALL)
    install(TARGETS Shoonyakasha
            EXPORT ShoonyakashaTargets
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib
            RUNTIME DESTINATION bin
    )

    # install(FILES ${ENGINE_HEADERS}) flattened the tree: include/foo/bar.hpp
    # landed as include/bar.hpp, so #include <foo/bar.hpp> worked in-tree and
    # broke for anyone consuming the install. DIRECTORY preserves the layout.
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
            DESTINATION include
            FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
    )

    # VulkanMemoryAllocator sits on the PUBLIC include interface, so it has to
    # travel with the installed headers or consumers cannot compile them.
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator/"
            DESTINATION include
            FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
    )

    install(EXPORT ShoonyakashaTargets
            FILE ShoonyakashaTargets.cmake
            NAMESPACE Shoonyakasha::
            DESTINATION lib/cmake/Shoonyakasha
    )

    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
            "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfigVersion.cmake"
            VERSION ${PROJECT_VERSION}
            COMPATIBILITY SameMajorVersion
    )

    configure_package_config_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ShoonyakashaConfig.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfig.cmake"
            INSTALL_DESTINATION lib/cmake/Shoonyakasha
    )

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfig.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfigVersion.cmake"
            DESTINATION lib/cmake/Shoonyakasha
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Example applications
#
# These were unconditional, so -DBUILD_EXAMPLES=OFF bought nothing and every
# pip install built all nine of them.
# ─────────────────────────────────────────────────────────────────────────────
if(BUILD_EXAMPLES)
    # One shared GLSL -> SPIR-V step for every example, replacing nine private
    # copies that had drifted apart. Included here so glslc is located once.
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake")

    add_subdirectory(examples/particle_test)
    add_subdirectory(examples/bloom_test)
    add_subdirectory(examples/declarative_sponza_test)
    add_subdirectory(examples/ssbo_data_flow_example)
    add_subdirectory(examples/particle_flow_example)
    add_subdirectory(examples/skinned_mesh_test)
    add_subdirectory(examples/physics_test)
    add_subdirectory(examples/pbr_physics_particles)
    add_subdirectory(examples/facade_test)
    add_subdirectory(examples/instancing_test)
endif()

# Python/Cython bindings
if(BUILD_PYTHON)
    add_subdirectory(python)
endif()

# Test suite
if(BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    enable_testing()
    add_subdirectory(tests)
endif()
