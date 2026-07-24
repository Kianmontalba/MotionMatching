# Optional CMake build. SCons remains the reference build for godot-cpp; this
# exists so the framework can be opened and indexed in CLion, Visual Studio or
# any IDE that expects CMake.
cmake_minimum_required(VERSION 3.22)
project(motion_matching LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MM_BUILD_EDITOR "Compile the editor tooling" ON)
option(MM_BUILD_TESTS "Compile the standalone tests" OFF)

# godot-cpp is expected as a submodule at godot-cpp/.
add_subdirectory(godot-cpp)

file(GLOB MM_SOURCES CONFIGURE_DEPENDS "src/*.cpp")

if(MM_BUILD_EDITOR)
    file(GLOB MM_EDITOR_SOURCES CONFIGURE_DEPENDS "editor/*.cpp")
    list(APPEND MM_SOURCES ${MM_EDITOR_SOURCES})
endif()

add_library(motion_matching SHARED ${MM_SOURCES})

target_include_directories(motion_matching PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/editor"
)

target_link_libraries(motion_matching PRIVATE godot-cpp)

if(MM_BUILD_EDITOR)
    target_compile_definitions(motion_matching PRIVATE TOOLS_ENABLED)
endif()

if(MSVC)
    target_compile_options(motion_matching PRIVATE $<$<CONFIG:Release>:/O2 /fp:fast>)
else()
    target_compile_options(motion_matching PRIVATE $<$<CONFIG:Release>:-O3 -ffast-math>)
endif()

set_target_properties(motion_matching PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/demo/addons/motion_matching/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/demo/addons/motion_matching/bin"
)

if(MM_BUILD_TESTS)
    message(STATUS
        "MM_BUILD_TESTS has no effect: MOTION MATCHING's classes are GDCLASS "
        "types that require a running Godot engine to instantiate, so they "
        "cannot be exercised by a standalone linked executable. Run the real "
        "test suite instead:\n"
        "  godot --headless --path demo/ --script ../tests/gdscript/run_tests.gd\n"
        "See tests/README.md for details."
    )
endif()
