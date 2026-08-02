# CompileShaders.cmake — one shared GLSL -> SPIR-V step.
#
# Replaces nine near-identical private copies of target_compile_shaders(), one
# per example, which had already drifted: two of them hinted only
# $ENV{VULKAN_SDK}/Bin (the Linux SDK uses lowercase bin) and three had no
# "glslc not found" guard, so a missing compiler failed at build time with an
# opaque "command not found" instead of at configure time with a clear message.
#
# Output still goes next to the sources, deliberately.
#
# The examples run from their own source directories — that is where their
# pipeline JSON, glTF scenes and HDR maps live, and nothing stages those into the
# build tree — and they load "shaders/x.frag.spv" relative to the working
# directory. Writing the SPIR-V to the build tree instead makes the build clean
# but leaves every example loading whatever .spv was last committed, which is a
# worse failure than a dirty working tree: it is exactly the stale-binary bug
# already found in examples/python.
#
# The real fix is an asset-staging step per example (copy the JSON and the
# referenced assets next to the executable, run from there), after which this can
# emit to CMAKE_CURRENT_BINARY_DIR and *.spv can leave the repository. That is a
# larger change than tidying the compile step and is left as follow-up.

find_program(SHOONYAKASHA_GLSLC
    NAMES glslc glslc.exe
    HINTS
        $ENV{VULKAN_SDK}/bin
        $ENV{VULKAN_SDK}/Bin
        $ENV{VULKAN_SDK}/Bin32
    DOC "glslc shader compiler from the Vulkan SDK"
)

if(NOT SHOONYAKASHA_GLSLC)
    message(FATAL_ERROR
        "glslc not found. It ships with the Vulkan SDK; set VULKAN_SDK or put glslc on PATH. "
        "Configure with -DBUILD_EXAMPLES=OFF to skip the examples entirely.")
endif()

# target_compile_shaders(<target> <shader-dir> [<shader-dir> ...])
#
# Compiles every .vert/.frag/.comp/.geom in each directory and makes <target>
# depend on the results. The .spv lands next to its source, which is where the
# examples look for it at runtime.
function(target_compile_shaders TARGET)
    if(NOT ARGN)
        message(FATAL_ERROR "target_compile_shaders(${TARGET}): no shader directories given")
    endif()

    set(ALL_SPV "")

    foreach(SHADER_DIR ${ARGN})
        # CONFIGURE_DEPENDS so a newly added shader is picked up without a
        # manual re-configure — the same reasoning as the source globs.
        file(GLOB SHADERS CONFIGURE_DEPENDS
            "${SHADER_DIR}/*.vert"
            "${SHADER_DIR}/*.frag"
            "${SHADER_DIR}/*.comp"
            "${SHADER_DIR}/*.geom"
        )

        # Alongside the sources — see the note at the top of this file.
        file(RELATIVE_PATH REL_DIR "${CMAKE_CURRENT_SOURCE_DIR}" "${SHADER_DIR}")
        set(OUTPUT_DIR "${SHADER_DIR}")

        foreach(SHADER ${SHADERS})
            get_filename_component(SHADER_NAME ${SHADER} NAME)
            set(SPV_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.spv")

            add_custom_command(
                OUTPUT ${SPV_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
                COMMAND ${SHOONYAKASHA_GLSLC} ${SHADER} -o ${SPV_OUTPUT}
                DEPENDS ${SHADER}
                COMMENT "Compiling shader: ${REL_DIR}/${SHADER_NAME}"
                VERBATIM
            )
            list(APPEND ALL_SPV ${SPV_OUTPUT})
        endforeach()
    endforeach()

    if(ALL_SPV)
        add_custom_target(${TARGET}_shaders DEPENDS ${ALL_SPV})
        add_dependencies(${TARGET} ${TARGET}_shaders)
    else()
        message(WARNING "target_compile_shaders(${TARGET}): no shaders found in ${ARGN}")
    endif()
endfunction()
