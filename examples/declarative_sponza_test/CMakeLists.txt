# examples/declarative_sponza_test/CMakeLists.txt
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
target_compile_shaders(DeclarativeSponzaTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

# Compile IBL shaders
target_compile_shaders(DeclarativeSponzaTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
