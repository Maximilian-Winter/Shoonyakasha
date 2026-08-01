# examples/particle_test/CMakeLists.txt
cmake_minimum_required(VERSION 3.12)
project(ShoonyakashaParticleTest)

add_executable(ShoonyakashaParticleTest
    test_app.cpp
    ParticleTestApp.h
    ParticleTestApp.cpp
)

target_link_libraries(ShoonyakashaParticleTest PRIVATE Shoonyakasha)

target_include_directories(ShoonyakashaParticleTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(ShoonyakashaParticleTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

if(MSVC)
    target_compile_options(ShoonyakashaParticleTest PRIVATE /W4)
else()
    target_compile_options(ShoonyakashaParticleTest PRIVATE -Wall -Wextra -pedantic)
endif()

# Shader compilation

target_compile_shaders(ShoonyakashaParticleTest "${CMAKE_CURRENT_SOURCE_DIR}/shaders")

# Copy pipeline JSON to build directory
add_custom_command(TARGET ShoonyakashaParticleTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/particle_pipeline.json"
        "$<TARGET_FILE_DIR:ShoonyakashaParticleTest>/particle_pipeline.json"
    COMMENT "Copying particle_pipeline.json to build directory"
)

# Copy compiled shaders to build directory
add_custom_command(TARGET ShoonyakashaParticleTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ShoonyakashaParticleTest>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:ShoonyakashaParticleTest>/shaders"
    COMMENT "Copying shaders to build directory"
)
