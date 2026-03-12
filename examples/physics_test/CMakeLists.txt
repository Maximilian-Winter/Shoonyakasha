# examples/physics_test/CMakeLists.txt
#
# 重力之試 — Testing the laws of gravity
#
cmake_minimum_required(VERSION 3.12)
project(PhysicsTest)

add_executable(PhysicsTest
    test_app.cpp
    PhysicsTestApp.h
    PhysicsTestApp.cpp
)

target_link_libraries(PhysicsTest PRIVATE Shoonyakasha)

target_include_directories(PhysicsTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(PhysicsTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(PhysicsTest PRIVATE /W4)
else()
    target_compile_options(PhysicsTest PRIVATE -Wall -Wextra -pedantic)
endif()

# Find glslc from Vulkan SDK
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Make sure the Vulkan SDK is installed.")
endif()

# ═══════════════════════════════════════════════════════════════
# Shader Compilation
# ═══════════════════════════════════════════════════════════════

function(target_compile_shaders TARGET SHADER_DIR OUTPUT_DIR)
    file(GLOB SHADERS
        "${SHADER_DIR}/*.vert"
        "${SHADER_DIR}/*.frag"
        "${SHADER_DIR}/*.comp"
        "${SHADER_DIR}/*.geom"
    )

    # Create unique target name based on directory
    get_filename_component(DIR_NAME ${SHADER_DIR} NAME)
    set(SHADER_TARGET "${TARGET}_shaders_${DIR_NAME}")

    foreach(SHADER ${SHADERS})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.spv")

        add_custom_command(
            OUTPUT ${SPV_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
            COMMAND ${GLSLC} ${SHADER} -o ${SPV_OUTPUT}
            DEPENDS ${SHADER}
            COMMENT "Compiling shader: ${SHADER_NAME}"
            VERBATIM
        )
        list(APPEND SPV_FILES ${SPV_OUTPUT})
    endforeach()

    if(SPV_FILES)
        add_custom_target(${SHADER_TARGET} DEPENDS ${SPV_FILES})
        add_dependencies(${TARGET} ${SHADER_TARGET})
    endif()
endfunction()

# Compile shaders from the shaders directory
target_compile_shaders(PhysicsTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

# Compile IBL shaders
target_compile_shaders(PhysicsTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
