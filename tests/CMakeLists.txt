# tests/CMakeLists.txt — Shoonyakasha Test Suite
cmake_minimum_required(VERSION 3.12)

# Collect all test sources
file(GLOB_RECURSE TEST_SOURCES
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

if(MSVC)
    target_compile_options(ShoonyakashaTests PRIVATE /W4)
endif()

include(GoogleTest)
gtest_discover_tests(ShoonyakashaTests)
