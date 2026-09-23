# tests/CMakeLists.txt — Shoonyakasha Test Suite
cmake_minimum_required(VERSION 3.21)

# Collect all test sources.
# CONFIGURE_DEPENDS matches the root CMakeLists. Without it a newly added test
# file is silently not built until someone re-runs configure by hand — you write
# a test, it never runs, and nothing tells you.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ecs/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/framegraph/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/facade/*.cpp"
)

add_executable(ShoonyakashaTests ${TEST_SOURCES})

target_link_libraries(ShoonyakashaTests
    PRIVATE
        Shoonyakasha
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(ShoonyakashaTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(ShoonyakashaTests PRIVATE SHOONYAKASHA_TESTING)

# SPIR-V fixtures for ShaderInterfaceValidatorTest, built next to their sources.
include("${PROJECT_SOURCE_DIR}/cmake/CompileShaders.cmake")
target_compile_shaders(ShoonyakashaTests "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
target_compile_definitions(ShoonyakashaTests PRIVATE
    SHOONYAKASHA_TEST_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    SHOONYAKASHA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(ShoonyakashaTests PRIVATE /W4)
endif()

include(GoogleTest)
gtest_discover_tests(ShoonyakashaTests)
