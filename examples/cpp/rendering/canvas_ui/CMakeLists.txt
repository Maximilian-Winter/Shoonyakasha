# examples/cpp/rendering/canvas_ui/CMakeLists.txt
#
# A screen overlay canvas on the default pipeline. No shaders and no pipeline
# JSON of its own: the UI shaders ship beside the default pipeline.
#
cmake_minimum_required(VERSION 3.21)
project(CanvasUIExample)

add_executable(CanvasUIExample main.cpp)

target_link_libraries(CanvasUIExample PRIVATE Shoonyakasha)

target_include_directories(CanvasUIExample PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(CanvasUIExample PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    # The source has UTF-8 string literals.
    target_compile_options(CanvasUIExample PRIVATE /W4 /utf-8)
else()
    target_compile_options(CanvasUIExample PRIVATE -Wall -Wextra -pedantic)
endif()
