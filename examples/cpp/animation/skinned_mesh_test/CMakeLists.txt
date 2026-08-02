# examples/cpp/animation/skinned_mesh_test/CMakeLists.txt
#
# 骨之舞  動之道
# The dance of bones — testing skeletal animation
#
cmake_minimum_required(VERSION 3.12)
project(SkinnedMeshTest)

add_executable(SkinnedMeshTest
    test_app.cpp
    SkinnedMeshApp.h
    SkinnedMeshApp.cpp
)

target_link_libraries(SkinnedMeshTest PRIVATE Shoonyakasha)

target_include_directories(SkinnedMeshTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(SkinnedMeshTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(SkinnedMeshTest PRIVATE /W4)
else()
    target_compile_options(SkinnedMeshTest PRIVATE -Wall -Wextra -pedantic)
endif()

# Shader compilation

# Shaders (shared module: cmake/CompileShaders.cmake)
target_compile_shaders(SkinnedMeshTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
