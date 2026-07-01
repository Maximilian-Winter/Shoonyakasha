# python/CMakeLists.txt
#
# 碼道之築 — Building the Way of Code
#
# Cython bindings for the Shoonyakasha engine facade.
# Compiles _shoonyakasha.pyx → .cpp → Python extension module.
#

cmake_minimum_required(VERSION 3.12)

# ── Find Python ───────────────────────────────────────────────
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
message(STATUS "[Python Bindings] Python ${Python3_VERSION} found: ${Python3_EXECUTABLE}")
message(STATUS "[Python Bindings] Python include: ${Python3_INCLUDE_DIRS}")
message(STATUS "[Python Bindings] Python library: ${Python3_LIBRARIES}")

# ── Find Cython ───────────────────────────────────────────────
find_program(CYTHON_EXECUTABLE NAMES cython cython3)
if(NOT CYTHON_EXECUTABLE)
    message(FATAL_ERROR
        "Cython not found. Install with: pip install cython\n"
        "Ensure Cython is on your PATH.")
endif()
message(STATUS "[Python Bindings] Cython found: ${CYTHON_EXECUTABLE}")

# ── Cython compilation: .pyx → .cpp ──────────────────────────
set(PYX_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha/_shoonyakasha.pyx")
set(CYX_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/_shoonyakasha.cpp")

# Collect all .pxd files as dependencies
file(GLOB PXD_FILES "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha/*.pxd")

# Collect bridge headers as dependencies
file(GLOB BRIDGE_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha/*.h")

add_custom_command(
    OUTPUT "${CYX_OUTPUT}"
    COMMAND "${CYTHON_EXECUTABLE}"
        --cplus
        -3
        -I "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha"
        -o "${CYX_OUTPUT}"
        "${PYX_SOURCE}"
    DEPENDS
        "${PYX_SOURCE}"
        ${PXD_FILES}
        ${BRIDGE_HEADERS}
    COMMENT "[Python Bindings] Running Cython on _shoonyakasha.pyx"
    VERBATIM
)

# ── Build extension module ────────────────────────────────────
Python3_add_library(_shoonyakasha MODULE "${CYX_OUTPUT}")

target_link_libraries(_shoonyakasha PRIVATE
    Shoonyakasha
)

target_include_directories(_shoonyakasha PRIVATE
    "${CMAKE_SOURCE_DIR}/include"                       # Facade headers
    "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha"          # Bridge headers (_glm_bridge.h etc.)
    "${Python3_INCLUDE_DIRS}"
)

# On Windows/MSVC, Python extension modules need .pyd suffix
if(MSVC)
    set_target_properties(_shoonyakasha PROPERTIES
        SUFFIX ".pyd"
        DEBUG_POSTFIX ""
    )
endif()

# ── Output location ──────────────────────────────────────────
# Place the built module next to __init__.py for easy import
set_target_properties(_shoonyakasha PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha"
)

# Also handle multi-config generators (MSVC)
foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
    set_target_properties(_shoonyakasha PROPERTIES
        "LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}"
            "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha"
        "RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER}"
            "${CMAKE_CURRENT_SOURCE_DIR}/shoonyakasha"
    )
endforeach()

# ── Install (for `pip install .` via scikit-build-core) ───────
# scikit-build-core stages this install() tree into the wheel, merged
# with the pure-Python files it copies from wheel.packages (see
# pyproject.toml) - both target the same "shoonyakasha" package dir, so
# they combine into one final package. Harmless/unused for a plain local
# `cmake --build` dev workflow (nothing calls `cmake --install` there).
install(TARGETS _shoonyakasha
    LIBRARY DESTINATION shoonyakasha
    RUNTIME DESTINATION shoonyakasha
)
