# examples/cpp/physics/physics_test/CMakeLists.txt
#
# 重力之試 — Testing the laws of gravity
#
cmake_minimum_required(VERSION 3.12)
project(PhysicsTest)

add_executable(PhysicsTest
    test_app.cpp
    PhysicsTestApp.h
    PhysicsTestApp.cpp
)

target_link_libraries(PhysicsTest PRIVATE Shoonyakasha)

target_include_directories(PhysicsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(PhysicsTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(PhysicsTest PRIVATE /W4)
else()
    target_compile_options(PhysicsTest PRIVATE -Wall -Wextra -pedantic)
endif()

# ═══════════════════════════════════════════════════════════════
# Shader Compilation
# ═══════════════════════════════════════════════════════════════

# Compile shaders from the shaders directory

# Compile IBL shaders

# Shaders (shared module: cmake/CompileShaders.cmake)
target_compile_shaders(PhysicsTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
