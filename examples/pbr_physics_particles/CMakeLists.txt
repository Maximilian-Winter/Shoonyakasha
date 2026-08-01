# examples/pbr_physics_particles/CMakeLists.txt
#
# Combined PBR + Physics + Particles + Bloom
#
cmake_minimum_required(VERSION 3.12)
project(PbrPhysicsParticles)

add_executable(PbrPhysicsParticles
    test_app.cpp
    CombinedExampleApp.h
    CombinedExampleApp.cpp
)

target_link_libraries(PbrPhysicsParticles PRIVATE Shoonyakasha)

target_include_directories(PbrPhysicsParticles PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(PbrPhysicsParticles PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(PbrPhysicsParticles PRIVATE /W4)
else()
    target_compile_options(PbrPhysicsParticles PRIVATE -Wall -Wextra -pedantic)
endif()

# ═══════════════════════════════════════════════════════════════
# Shader Compilation
# ═══════════════════════════════════════════════════════════════

# Compile main shaders

# Compile IBL shaders

# Shaders (shared module: cmake/CompileShaders.cmake)
target_compile_shaders(PbrPhysicsParticles
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
