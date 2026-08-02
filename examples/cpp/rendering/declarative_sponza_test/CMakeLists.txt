# examples/cpp/rendering/declarative_sponza_test/CMakeLists.txt
#
# 玄武司察  深潛而見真
# The Dark Warrior governs investigation — testing declarative v3 frame graph
#
cmake_minimum_required(VERSION 3.12)
project(DeclarativeSponzaTest)

add_executable(DeclarativeSponzaTest
    test_app.cpp
    DeclarativeSponzaApp.h
    DeclarativeSponzaApp.cpp
)

target_link_libraries(DeclarativeSponzaTest PRIVATE Shoonyakasha)

target_include_directories(DeclarativeSponzaTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(DeclarativeSponzaTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(DeclarativeSponzaTest PRIVATE /W4)
else()
    target_compile_options(DeclarativeSponzaTest PRIVATE -Wall -Wextra -pedantic)
endif()

# ═══════════════════════════════════════════════════════════════
# Shader Compilation
# ═══════════════════════════════════════════════════════════════

# Compile shaders from the shaders directory

# Compile IBL shaders

# Shaders (shared module: cmake/CompileShaders.cmake)
target_compile_shaders(DeclarativeSponzaTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
