# examples/cpp/compute/particle_flow_example/CMakeLists.txt
#
# Particle Flow — Visual showcase of GPU compute particles
#
cmake_minimum_required(VERSION 3.12)
project(ParticleFlowExample)

add_executable(ParticleFlowExample
    test_app.cpp
    ParticleFlowApp.h
    ParticleFlowApp.cpp
)

target_link_libraries(ParticleFlowExample PRIVATE Shoonyakasha)

target_include_directories(ParticleFlowExample PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(ParticleFlowExample PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(ParticleFlowExample PRIVATE /W4)
else()
    target_compile_options(ParticleFlowExample PRIVATE -Wall -Wextra -pedantic)
endif()

# ═══════════════════════════════════════════════════════════════
# Shader Compilation
# ═══════════════════════════════════════════════════════════════

# Compile shaders from the shaders directory

# Compile IBL shaders

# Shaders (shared module: cmake/CompileShaders.cmake)
target_compile_shaders(ParticleFlowExample
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
