# examples/cpp/rendering/default_pipeline/CMakeLists.txt
#
# Sponza (or a colonnade of boxes) on the default pipeline, through the facade
# API alone. No shaders and no pipeline JSON of its own: an empty
# pipelineJsonPath loads the one under python/shoonyakasha/pipelines/default.
#
cmake_minimum_required(VERSION 3.21)
project(DefaultPipelineExample)

add_executable(DefaultPipelineExample main.cpp)

target_link_libraries(DefaultPipelineExample PRIVATE Shoonyakasha)

target_include_directories(DefaultPipelineExample PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(DefaultPipelineExample PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(DefaultPipelineExample PRIVATE /W4)
else()
    target_compile_options(DefaultPipelineExample PRIVATE -Wall -Wextra -pedantic)
endif()
