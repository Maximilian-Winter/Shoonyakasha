# examples/cpp/api/instancing_test/CMakeLists.txt
#
# एकं रूपं बहुधा भाति — one form appearing manifold
#
cmake_minimum_required(VERSION 3.21)
project(InstancingTest)

add_executable(InstancingTest
    test_app.cpp
    InstancingTestApp.h
    InstancingTestApp.cpp
)

target_link_libraries(InstancingTest PRIVATE Shoonyakasha)

target_include_directories(InstancingTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(InstancingTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(InstancingTest PRIVATE /W4)
else()
    target_compile_options(InstancingTest PRIVATE -Wall -Wextra -pedantic)
endif()

# Shaders (shared module: cmake/CompileShaders.cmake)
target_compile_shaders(InstancingTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
