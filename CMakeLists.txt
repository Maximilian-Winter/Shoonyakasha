# CMakeLists.txt
cmake_minimum_required(VERSION 3.12)
project(Shoonyakasha VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_EXAMPLES "Build example projects" OFF)
option(BUILD_TESTS "Build test projects" OFF)
option(BUILD_PYTHON "Build Python/Cython bindings" OFF)

find_package(Vulkan REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)
find_package(Bullet CONFIG REQUIRED)

include_directories(${Vulkan_INCLUDE_DIR})

set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} "C:\\Program Files (x86)\\GLFW")
find_package(glfw3 REQUIRED)
include_directories("C:\\Program Files (x86)\\GLFW\\include")

file(GLOB_RECURSE ENGINE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

file(GLOB_RECURSE ENGINE_HEADERS
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
)

add_library(Shoonyakasha STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

# Required to link this static library into the Python extension module
# (a shared object) on GCC/Clang. No-op on MSVC.
set_target_properties(Shoonyakasha PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(Shoonyakasha
        PUBLIC
        $<INSTALL_INTERFACE:include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyobjloader
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf
)

target_link_libraries(Shoonyakasha
        PUBLIC ${Vulkan_LIBRARY}
        PUBLIC "C:\\Program Files (x86)\\GLFW\\lib\\glfw3.lib"
        PUBLIC nlohmann_json::nlohmann_json
        PUBLIC EnTT::EnTT
        PUBLIC ${BULLET_LIBRARIES}
)

target_include_directories(Shoonyakasha PUBLIC ${BULLET_INCLUDE_DIRS})

# Enable test-support constructors in facade (PRIVATE — does not leak to consumers)
target_compile_definitions(Shoonyakasha PRIVATE SHOONYAKASHA_TESTING)

# Install the library
install(TARGETS Shoonyakasha
        EXPORT ShoonyakashaTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
)

# Install the headers
install(FILES ${ENGINE_HEADERS} DESTINATION include)

# Export the targets
install(EXPORT ShoonyakashaTargets
        FILE ShoonyakashaTargets.cmake
        NAMESPACE Shoonyakasha::
        DESTINATION lib/cmake/Shoonyakasha
)

# Create a ConfigVersion.cmake file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

# Configure the Config.cmake file
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ShoonyakashaConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfig.cmake"
        INSTALL_DESTINATION lib/cmake/Shoonyakasha
)

# Install the Config.cmake and ConfigVersion.cmake files
install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ShoonyakashaConfigVersion.cmake"
        DESTINATION lib/cmake/Shoonyakasha
)

# Example applications
add_subdirectory(examples/particle_test)
add_subdirectory(examples/bloom_test)
add_subdirectory(examples/declarative_sponza_test)
add_subdirectory(examples/ssbo_data_flow_example)
add_subdirectory(examples/particle_flow_example)
add_subdirectory(examples/skinned_mesh_test)
add_subdirectory(examples/physics_test)
add_subdirectory(examples/pbr_physics_particles)
add_subdirectory(examples/facade_test)

# Python/Cython bindings
if(BUILD_PYTHON)
    add_subdirectory(python)
endif()

# Test suite
if(BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
