# examples/skinned_mesh_test/CMakeLists.txt
#
# 骨之舞  動之道
# The dance of bones — testing skeletal animation
#
cmake_minimum_required(VERSION 3.12)
project(SkinnedMeshTest)

add_executable(SkinnedMeshTest
    test_app.cpp
    SkinnedMeshApp.h
    SkinnedMeshApp.cpp
)

target_link_libraries(SkinnedMeshTest PRIVATE Shoonyakasha)

target_include_directories(SkinnedMeshTest PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(SkinnedMeshTest PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(SkinnedMeshTest PRIVATE /W4)
else()
    target_compile_options(SkinnedMeshTest PRIVATE -Wall -Wextra -pedantic)
endif()

# Find glslc from Vulkan SDK
find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/Bin $ENV{VULKAN_SDK}/bin)
if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found! Make sure the Vulkan SDK is installed.")
endif()

# Shader compilation
function(target_compile_shaders TARGET SHADER_DIR OUTPUT_DIR)
    file(GLOB SHADERS
        "${SHADER_DIR}/*.vert"
        "${SHADER_DIR}/*.frag"
        "${SHADER_DIR}/*.comp"
        "${SHADER_DIR}/*.geom"
    )

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

target_compile_shaders(SkinnedMeshTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)

target_compile_shaders(SkinnedMeshTest
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ibl"
)
