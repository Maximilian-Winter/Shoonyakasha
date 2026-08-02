# examples/cpp/compute/ssbo_data_flow_example/CMakeLists.txt
#
# 保存之輪 — The Wheel of Preservation
# Phase 4: SSBO Data Flow — save GPU compute results to disk, load them back
#
cmake_minimum_required(VERSION 3.12)
project(SSBODataFlowExample)

add_executable(SSBODataFlowExample
    test_app.cpp
    SSBODataFlowApp.h
    SSBODataFlowApp.cpp
)

target_link_libraries(SSBODataFlowExample PRIVATE Shoonyakasha)

target_include_directories(SSBODataFlowExample PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(SSBODataFlowExample PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(SSBODataFlowExample PRIVATE /W4)
else()
    target_compile_options(SSBODataFlowExample PRIVATE -Wall -Wextra -pedantic)
endif()

# ═══════════════════════════════════════════════════════════════
# Shader Compilation
# ═══════════════════════════════════════════════════════════════

# Compile shaders from the shaders directory

# Compile IBL shaders

# Shaders (shared module: cmake/CompileShaders.cmake)
target_compile_shaders(SSBODataFlowExample
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
