# examples/cpp/rendering/bloom_test/CMakeLists.txt
cmake_minimum_required(VERSION 3.12)
project(ShoonyakashaBloomTest)

add_executable(ShoonyakashaBloomTest
    test_app.cpp
    BloomTestApp.h
    BloomTestApp.cpp
)

target_link_libraries(ShoonyakashaBloomTest PRIVATE Shoonyakasha)

target_include_directories(ShoonyakashaBloomTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(ShoonyakashaBloomTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(ShoonyakashaBloomTest PRIVATE /W4)
else()
    target_compile_options(ShoonyakashaBloomTest PRIVATE -Wall -Wextra -pedantic)
endif()

# Shader compilation

target_compile_shaders(ShoonyakashaBloomTest "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

# Copy pipeline JSON to build directory
add_custom_command(TARGET ShoonyakashaBloomTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/bloom_pipeline.json"
        "$<TARGET_FILE_DIR:ShoonyakashaBloomTest>/bloom_pipeline.json"
    COMMENT "Copying bloom_pipeline.json to build directory"
)

# Copy compiled shaders to build directory
add_custom_command(TARGET ShoonyakashaBloomTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ShoonyakashaBloomTest>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:ShoonyakashaBloomTest>/shaders"
    COMMENT "Copying shaders to build directory"
)
