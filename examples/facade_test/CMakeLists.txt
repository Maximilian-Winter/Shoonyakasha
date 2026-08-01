# examples/facade_test/CMakeLists.txt
#
# 碼道驗證 — Verifying the Way of Code
#
# Proof that the facade API is sufficient for a complete application.
# This target includes NO internal engine headers — only Facade/*.h.
#
cmake_minimum_required(VERSION 3.21)
project(FacadeTest)

add_executable(FacadeTest main.cpp)

target_link_libraries(FacadeTest PRIVATE Shoonyakasha)

target_include_directories(FacadeTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(FacadeTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(FacadeTest PRIVATE /W4)
else()
    target_compile_options(FacadeTest PRIVATE -Wall -Wextra -pedantic)
endif()

# Shaders (shared module: cmake/CompileShaders.cmake). Without this the example
# built fine and then failed at runtime on a missing .spv, which is why it was
# the one example that could not run.
target_compile_shaders(FacadeTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)
